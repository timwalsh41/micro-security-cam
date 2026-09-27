"""E2E: photo capture on real hardware."""
import time

import pytest

import camclient
from conftest import parse_photo_path


def test_photo_returns_dated_path(client, guard, capture_ready):
    """A photo command returns a /YYYYMMDD/YYYYMMDD_HHMMSS.jpg path."""
    if capture_ready:
        pytest.skip(capture_ready)
    status = client.status()
    if not status["sd"]:
        pytest.skip("photo capture requires the SD card")
    path = guard.take_photo()
    date, hhmmss = parse_photo_path(path)
    assert len(date) == 8 and len(hhmmss) == 6


def test_photo_file_is_a_real_jpeg(client, guard, capture_ready):
    """The captured photo downloads as a non-trivial JPEG."""
    if capture_ready:
        pytest.skip(capture_ready)
    status = client.status()
    if not status["sd"]:
        pytest.skip("photo capture requires the SD card")
    path = guard.take_photo()
    body = client.download(path)
    assert len(body) > 1000, f"suspiciously small photo ({len(body)} bytes)"
    assert body[0:2] == b"\xff\xd8", "photo missing JPEG SOI"
    assert body[-2:] == b"\xff\xd9", "photo missing JPEG EOI"
