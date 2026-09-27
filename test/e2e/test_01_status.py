"""E2E: /status invariants on real hardware."""
import camclient

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
}


def test_status_keys_are_exactly_the_telemetry_set(client):
    """/status returns exactly the documented telemetry keys."""
    status = client.status()
    assert set(status.keys()) == STATUS_KEYS


def test_status_value_types_and_ranges(client):
    """Telemetry values are the documented types in valid ranges."""
    status = client.status()
    for key in ("recording", "streaming", "sd"):
        assert isinstance(status[key], bool), f"{key} must be bool"
    for key in ("frames", "file_size", "connected_streams",
                "sd_free_bytes", "photos", "videos"):
        assert isinstance(status[key], int), f"{key} must be int"
        assert status[key] >= 0, f"{key} must be non-negative"


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
