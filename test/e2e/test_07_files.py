"""E2E: file listing and download policy on real hardware."""
import pytest

import camclient
from conftest import parse_photo_path


def test_files_root_listing_shape(client):
    """The root listing carries the documented JSON shape."""
    listing = client.files()
    assert set(listing.keys()) == {
        "dir", "start", "total", "skipped_long_names",
        "entries", "more", "next",
    }
    assert listing["dir"] == "/"
    assert listing["start"] == 0
    assert isinstance(listing["total"], int)
    assert listing["total"] == len(listing["entries"])
    for entry in listing["entries"]:
        assert set(entry.keys()) == {"n", "s", "d", "r"}
        assert isinstance(entry["s"], int) and entry["s"] >= 0
        assert isinstance(entry["d"], bool)
        assert isinstance(entry["r"], bool)


def test_files_root_contains_media_and_config(client):
    """The root listing shows at least one date directory."""
    listing = client.files()
    date_dirs = [e["n"] for e in listing["entries"] if e["d"]]
    assert date_dirs, "no date directories on the card root"


def test_files_explicit_dir_matches_root(client):
    """An explicit dir=/ listing is identical to the default listing."""
    explicit = client.files(dir="/")
    default = client.files()
    assert explicit == default, (
        "explicit dir=/ listing differs from the default listing; "
        "on a device with a stale build the dir parameter may be ignored "
        "or misrouted"
    )


def test_files_date_dir_listing(client, guard, capture_ready):
    """Listing the photo date directory shows the media files."""
    if capture_ready:
        pytest.skip(capture_ready)
    date = guard.device_date()
    listing = client.files(dir=f"/{date}")
    assert listing["dir"] == f"/{date}"
    assert listing["total"] > 0, f"date dir /{date} unexpectedly empty"
    names = [e["n"] for e in listing["entries"]]
    assert any(n.endswith(".jpg") for n in names), \
        f"no photos in /{date} listing: {names}"


def test_files_cmd_route_date_dir(client, guard, capture_ready):
    """cmd=files with a date dir lists that directory (CLI transport)."""
    if capture_ready:
        pytest.skip(capture_ready)
    date = guard.device_date()
    listing = client.control("files", extra={"dir": f"/{date}"})
    assert listing["dir"] == f"/{date}", (
        f"cmd=files ignored the dir parameter (listed {listing['dir']!r}); "
        "this indicates a stale firmware build"
    )
    assert listing["total"] > 0


def test_files_disallowed_paths_rejected(client):
    """Path-shape violations are refused with 403, never listed."""
    for bad in (".", "/..", "/logs_extra", "//", "/x/"):
        resp = client.get("/files?" + camclient.urllib.parse.urlencode(
            {"dir": bad}))
        assert resp.status == 403, f"dir={bad!r} returned {resp.status}"
    resp = client.get("/files?dir=/nonexistentdate123")
    assert resp.status == 403, "non-date directory was allowed"


def test_files_missing_date_dir_404(client):
    """A well-formed but absent date directory is a 404."""
    resp = client.get("/files?dir=/19990101")
    assert resp.status == 404


def test_download_config_ini_refused(client):
    """/config.ini is never downloadable (it holds credentials)."""
    resp = client.get("/download?path=/config.ini")
    assert resp.status == 403
    assert b"path not allowed" in resp.body


def test_download_missing_file_404(client):
    """An allowed-shaped but absent file is a 404."""
    resp = client.get("/download?path=/19990101/19990101_000000.jpg")
    assert resp.status == 404


def test_download_traversal_refused(client):
    """Traversal and backslash paths are refused with 403."""
    for bad in ("/19700101/../config.ini",
                "/19700101//19700101_000000.jpg",
                "/19700101\\x.jpg",
                "/"):
        resp = client.get("/download?" + camclient.urllib.parse.urlencode(
            {"path": bad}))
        assert resp.status == 403, f"path={bad!r} returned {resp.status}"


def test_download_active_clip_refused_while_recording(client, guard, capture_ready):
    """The clip being recorded is refused with 409 until it closes."""
    if capture_ready:
        pytest.skip(capture_ready)
    from test_03_record import _shift_hhmmss

    status = client.status()
    if not status["sd"]:
        pytest.skip("download policy requires the SD card")
    assert not status["recording"], "device already recording before test"

    photo_path = guard.take_photo()
    date, hhmmss = parse_photo_path(photo_path)

    guard.start_recording()
    try:
        candidates = [
            f"/{date}/{date}_{_shift_hhmmss(hhmmss, k)}.avi"
            for k in range(4)
        ]
        seen_codes = []
        refused = False
        for path in candidates:
            resp = client.get(
                "/download?" + camclient.urllib.parse.urlencode(
                    {"path": path})
            )
            seen_codes.append((path, resp.status))
            if resp.status == 409:
                assert b"clip still recording" in resp.body
                refused = True
                break
            if resp.status == 200:
                break  # recording already stopped; nothing to assert
        if not refused:
            pytest.fail(
                f"no candidate clip was refused while recording: "
                f"{seen_codes}; clip naming may have changed"
            )
    finally:
        if client.status()["recording"]:
            guard.stop_recording()
