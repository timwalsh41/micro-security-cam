"""E2E: live MJPEG streaming on real hardware."""
import time

import pytest

import camclient

STREAM_SPINUP_TIMEOUT_S = 10.0
STATUS_POLL_S = 1.0


def _wait_for(predicate, timeout, what):
    """Poll the device until predicate() holds, else fail the test."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(STATUS_POLL_S)
    pytest.fail(f"timed out after {timeout}s waiting for {what}")


def test_stream_lifecycle_and_multipart_frames(client, guard, capture_ready):
    """stream_start, an MJPEG read, stream_list, and stream_stop work end
    to end, with well-formed multipart JPEG parts on the wire."""
    if capture_ready:
        pytest.skip(capture_ready)
    guard.start_streaming()
    _wait_for(lambda: client.status()["streaming"],
              STREAM_SPINUP_TIMEOUT_S, "streaming flag to rise")

    frames = []
    try:
        for headers, jpeg in client.stream_frames(max_frames=3,
                                                  frame_timeout=10.0):
            frames.append((headers, jpeg))
            if len(frames) == 1:
                # A connected viewer is visible in the live telemetry
                # while the connection is still open.
                connected = client.status()["connected_streams"]
                assert connected >= 1, \
                    "no connected stream slot while reading /stream"
            if len(frames) >= 2:
                break  # closing the connection frees the viewer slot
    finally:
        guard.stop_streaming()

    assert len(frames) >= 2, \
        "expected the placeholder frame plus at least one real frame"
    for headers, jpeg in frames:
        assert headers["content-type"] == "image/jpeg"
        assert int(headers["content-length"]) == len(jpeg), \
            "Content-Length disagrees with the parsed part body"

    _wait_for(lambda: client.status()["connected_streams"] == 0,
              STREAM_SPINUP_TIMEOUT_S,
              "stream slot to free after stream_stop")


def test_stream_list_reports_zero_after_stop(client):
    """stream_list shows no active streams once preview is stopped."""
    listing = client.control("stream_list")
    camclient.CamClient.assert_result_ok(listing, "stream_list")
    assert listing["active_streams"] == 0


def test_stream_refused_without_stream_start(client):
    """/stream is refused with 409 while streaming has not been started."""
    status = client.status()
    if status["streaming"]:
        pytest.skip("streaming already active; cannot test the refusal")
    resp = client.get("/stream", timeout=10.0)
    assert resp.status == 409
    assert b"streaming not started" in resp.body
