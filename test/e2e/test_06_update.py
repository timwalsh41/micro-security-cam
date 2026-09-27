"""E2E: POST /update bulk config overlay on real hardware."""
import pytest

import camclient


def test_update_overlays_live_config_and_persists(client, guard):
    """A partial INI overlay changes the live config; re-overlaying the
    session-start snapshot restores both the live config and the
    persisted (NVS) copy.
    """
    key = "camera.jpeg_quality"
    original = client.config_get_value(key)
    changed = "63" if original != "63" else "10"

    try:
        resp = client.update(f"[camera]\njpeg_quality={changed}\n")
        data = resp.json()
        assert resp.status == 200 and data.get("result") == "ok", \
            f"overlay rejected: HTTP {resp.status} {data!r}"
        assert client.config_get_value(key) == changed
    finally:
        # Re-overlay the session-start INI: restores the live config and
        # re-persists the pre-test values to NVS (the overlay's default
        # destination). Side effect: NVS now holds the pre-test live
        # config even if nothing was saved there before; the content is
        # identical to what the device was running.
        restore = client.update(guard.m_config_ini)
        assert restore.status == 200
    assert client.config_get_value(key) == original


def test_update_rejects_oversized_ini_with_413(client):
    """INI text over the form-buffer limit is rejected with 413."""
    oversized = "[camera]\n" + "x" * 1400 + "\n"
    resp = client.post_form("/update", {"config": oversized})
    assert resp.status == 413, \
        f"expected 413 for oversized INI, got {resp.status}: {resp.text()}"


def test_update_missing_config_parameter_rejected(client):
    """An /update without a config field is a 400, not a crash."""
    resp = client.post_form("/update", {})
    assert resp.status == 400
    assert b"missing 'config'" in resp.body


def test_update_without_credentials_refused(client):
    """Unauthenticated /update is refused (config carries credentials)."""
    anonymous = client.with_credentials(None)
    resp = anonymous.post_form("/update", {"config": "[camera]\n"})
    assert resp.status in (401, 403), \
        f"unauthenticated update returned {resp.status}"


def test_update_sd_destination_gating(client, guard):
    """dest=sd is refused with 503 only when no card is present.

    With a card present the suite does not exercise dest=sd with an
    arbitrary payload: it would rewrite /config.ini on the device's
    card, so the acceptance path is acknowledged as a known hole.
    """
    status = client.status()
    if status["sd"]:
        pytest.skip(
            "device has its SD card; exercising dest=sd would rewrite "
            "/config.ini on the card"
        )
    resp = client.update("[camera]\n", dest="sd")
    assert resp.status == 503
