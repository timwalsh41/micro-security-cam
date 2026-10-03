"""E2E: /status invariants on real hardware."""
import camclient

# Core telemetry keys; additional keys (e.g. uptime) may be added by
# newer firmware and must not break the invariants below.
STATUS_KEYS = {
    "recording",
    "streaming",
    "sd",
    "frames",
    "file_size",
    "connected_streams",
    "sd_free_bytes",
    "photos",
    "videos",
    "version",
    "temperature_c",
    "width",
    "height",
    "stream_frames",
}


def test_status_required_keys_present(client):
    """/status carries the documented telemetry keys."""
    status = client.status()
    assert STATUS_KEYS <= set(status.keys()), \
        f"missing telemetry keys: {STATUS_KEYS - set(status.keys())}"


def test_status_value_types_and_ranges(client):
    """Telemetry values are the documented types in valid ranges."""
    status = client.status()
    for key in ("recording", "streaming", "sd"):
        assert isinstance(status[key], bool), f"{key} must be bool"
    for key in ("frames", "file_size", "connected_streams",
                "sd_free_bytes", "photos", "videos", "width", "height",
                "stream_frames"):
        assert isinstance(status[key], int), f"{key} must be int"
        assert status[key] >= 0, f"{key} must be non-negative"
    assert status["width"] > 0 and status["height"] > 0, \
        "frame dimensions must be positive"
    assert isinstance(status["temperature_c"], (int, float)), \
        "temperature_c must be numeric"
    assert -40.0 <= status["temperature_c"] <= 120.0, \
        f"temperature_c out of range: {status['temperature_c']}"


def test_status_version_matches_version_command(client):
    """The status panel's version string is the build identity from
    the version endpoint (both render version.h)."""
    status = client.status()
    version_payload = client.version()
    assert isinstance(status["version"], str) and status["version"], \
        "status version must be a non-empty string"
    assert status["version"] == version_payload["version"], \
        "status version disagrees with the version endpoint"


def test_status_connected_streams_matches_stream_list(client):
    """connected_streams agrees with the stream_list command."""
    status = client.status()
    listing = client.control("stream_list")
    camclient.CamClient.assert_result_ok(listing, "stream_list")
    assert listing["active_streams"] == status["connected_streams"]


def test_status_photos_and_videos_consistent_with_sd(client):
    """Per-day media counters are zero whenever SD is unavailable."""
    status = client.status()
    if not status["sd"]:
        assert status["photos"] == 0
        assert status["videos"] == 0
