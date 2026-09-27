"""E2E: photo counter telemetry on real hardware.

Sorted after the other camera modules (04b) so the slow counter poll
does not delay record/stream coverage in the early post-boot window.
"""
import time

import pytest


def test_photo_increments_photos_counter(client, guard, capture_ready):
    """The photos counter increments after a capture (device caches it)."""
    if capture_ready:
        pytest.skip(capture_ready)
    status = client.status()
    if not status["sd"]:
        pytest.skip("photo capture requires the SD card")
    before = status["photos"]
    path = guard.take_photo()

    # The SD stats panel caches counters for one minute; poll until the
    # cache refreshes rather than asserting against stale data.
    deadline = time.monotonic() + 75.0
    while time.monotonic() < deadline:
        photos = client.status()["photos"]
        if photos > before:
            return
        time.sleep(5.0)
    pytest.fail(
        f"photos counter did not increment within 75s "
        f"(before={before}, path={path})"
    )
