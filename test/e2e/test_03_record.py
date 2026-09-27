"""E2E: video recording on real hardware, including AVI clip validation."""
import time

import pytest

import avi_parser
import camclient
from conftest import _wait_for, parse_photo_path

# Device status polling while a recording spins up.
SPINUP_TIMEOUT_S = 10.0

# The clip path is /<date>/<date>_<HHMMSS>_<WxH>_<FPS>fps.avi in
# device time; the firmware exposes no "current clip path" query and
# the dir-parameterized listing is unavailable, so the suite records a
# photo immediately before starting to learn the device clock and then
# probes a window of candidate start seconds built from the live
# video config (resolution dimensions per the camera controller's
# table, plus the serial suffix the path uniquifier appends). The
# window spans the whole spin-up bound the same test tolerates plus a
# second of slack, so a slow-but-healthy spin-up is still located.
CANDIDATE_SECONDS = int(SPINUP_TIMEOUT_S) + 2
RESOLUTION_DIMENSIONS = {
    "QQVGA": (160, 120),
    "QVGA": (320, 240),
    "VGA": (640, 480),
    "SVGA": (800, 600),
    "XGA": (1024, 768),
    "HD": (1280, 720),
    "SXGA": (1280, 1024),
    "UXGA": (1600, 1200),
}


def _shift_hhmmss(hhmmss, offset):
    """Return HHMMSS shifted by whole seconds (wrapping within the day)."""
    total = int(hhmmss[0:2]) * 3600 + int(hhmmss[2:4]) * 60 \
        + int(hhmmss[4:6]) + offset
    total %= 86400
    return f"{total // 3600:02d}{total % 3600 // 60:02d}{total % 60:02d}"


def _record_short_clip(client, guard):
    """Record a few seconds of video, returning (clip_bytes, file_size)."""
    status = client.status()
    if not status["sd"]:
        pytest.skip("recording requires the SD card")
    assert not status["recording"], "device already recording before test"

    photo_path = guard.take_photo()
    date, hhmmss = parse_photo_path(photo_path)

    guard.start_recording()
    try:
        _wait_for(lambda: client.status()["recording"],
                  SPINUP_TIMEOUT_S, "recording flag to rise")
        time.sleep(4.0)
        _wait_for(lambda: client.status()["frames"] > 0,
                  SPINUP_TIMEOUT_S, "recorded frames to appear")
        file_size = client.status()["file_size"]
        assert file_size > 0, "recording produced a zero-length file"
    finally:
        guard.stop_recording()
    _wait_for(lambda: not client.status()["recording"],
              SPINUP_TIMEOUT_S, "recording flag to fall")

    clip_path, clip = _fetch_completed_clip(client, date, hhmmss, file_size)
    guard.track_created(clip_path)
    return clip, file_size


def _candidate_clip_paths(client, date, hhmmss):
    """Build candidate clip paths for the just-recorded clip."""
    dims = RESOLUTION_DIMENSIONS.get(
        client.config_get_value("video.resolution").upper()
    )
    if dims is None:
        pytest.fail(
            f"unknown video.resolution; cannot derive clip naming "
            f"from {RESOLUTION_DIMENSIONS.keys()}"
        )
    width, height = dims
    fps = client.config_get_value("video.fps")
    paths = []
    for k in range(CANDIDATE_SECONDS):
        stem = f"/{date}/{date}_{_shift_hhmmss(hhmmss, k)}_{width}x{height}_{fps}fps"
        paths.append(stem + ".avi")
        paths.append(stem + "_1.avi")
    return paths


def _fetch_completed_clip(client, date, hhmmss, file_size):
    """Download the just-completed clip by probing candidate paths.

    The size snapshot is taken while the device may still be appending
    to the clip, so a candidate at least as large as the snapshot is
    accepted (exact matches preferred); the AVI structural checks in
    the sibling tests validate the payload itself.
    """
    candidates = _candidate_clip_paths(client, date, hhmmss)
    exact = None
    grown = None
    for path in candidates:
        try:
            body = client.download(path)
        except camclient.CamClientError:
            continue
        if len(body) == file_size:
            exact = (path, body)
            break
        if len(body) >= file_size and grown is None:
            grown = (path, body)
    if exact or grown:
        return exact or grown
    pytest.fail(
        "could not locate the completed clip among "
        f"{candidates} (expected >= {file_size} bytes); "
        "this may indicate a stale firmware build or a naming change"
    )


def test_record_start_stop_flips_flag(client, guard, capture_ready):
    """start/stop flip the recording flag and produce a completed clip."""
    if capture_ready:
        pytest.skip(capture_ready)
    clip, _ = _record_short_clip(client, guard)
    assert clip[0:4] == b"RIFF"


def test_recording_produces_valid_avi(client, guard, capture_ready):
    """A completed clip downloads as a real AVI with video frames."""
    if capture_ready:
        pytest.skip(capture_ready)
    clip, _ = _record_short_clip(client, guard)
    parsed = avi_parser.parse_avi(clip)
    assert parsed["riff_size"] == len(clip) - 8, \
        "RIFF size field disagrees with the downloaded byte count"
    assert parsed["frames"] > 0, "AVI contains no video frames"


def test_mic_recording_audio_stream_format(client, guard, capture_ready):
    """A clip recorded with audio carries a 16 kHz/16-bit/mono stream.

    The mic service is initialized at boot only when
    ``video.audio_enabled`` is set; a build booted without audio
    records video-only clips, acknowledged here as a skip rather than
    papered over.
    """
    if capture_ready:
        pytest.skip(capture_ready)
    try:
        guard.set_config("video.audio_enabled", "true")
    except camclient.CamCommandError as e:
        pytest.skip(
            f"video.audio_enabled could not be set ({e}); the flashed "
            "firmware rejects config_set_value, so audio recording "
            "coverage needs a fresh build"
        )
    clip, _ = _record_short_clip(client, guard)
    parsed = avi_parser.parse_avi(clip)
    if parsed["audio"] is None:
        pytest.skip(
            "recorded clip has no audio stream: the mic service is "
            "initialized at boot only when video.audio_enabled is set; "
            "boot the device with audio enabled to exercise this coverage"
        )
    assert parsed["audio"] == {"channels": 1, "rate": 16000, "bits": 16}, \
        f"unexpected audio stream format: {parsed['audio']}"
