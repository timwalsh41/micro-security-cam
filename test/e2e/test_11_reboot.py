"""E2E: reboot-dependent behavior (NVS persistence, cold-boot gating).

These tests reboot the device, which is only safe/possible once the
firmware exposes a reboot endpoint (companion task msc-e2e-api). They
skip automatically until it lands, then activate without further
changes.
"""
import time

import pytest

import camclient
from conftest import _log_sizes, _wait_until

# The device needs time to reconnect to WiFi after a reboot; a short
# outage right after the first successful response is common.
BOOT_TIMEOUT_S = 180.0
BOOT_POLL_S = 3.0

# Keys exercised for NVS persistence across a reboot. The card may
# carry a /config.ini whose contents overlay NVS at boot (documented
# precedence: defaults < NVS < SD file), which makes NVS content
# unobservable through a reboot for any key that file pins. The test
# therefore persists a set of keys in one save and requires at least
# one to round-trip: that proves the NVS write/load path end to end
# while tolerating SD-pinned keys.
PERSISTENCE_KEYS = ("camera.jpeg_quality", "logging.retention_days")


def _changed_value(key, original):
    """A value distinct from the original for round-trip comparison."""
    if key == "camera.jpeg_quality":
        return "63" if original != "63" else "10"
    return "10" if original != "10" else "5"


def _reboot_available(client):
    """True when the firmware answers cmd=reboot non-fallback."""
    try:
        client.reboot()
        return True
    except camclient.CamEndpointUnavailable:
        return False


@pytest.fixture(scope="module")
def reboot_available(client):
    """Skip the module until the reboot endpoint exists.

    Probing cmd=reboot on a firmware that has it reboots the device,
    so the probe doubles as the reboot and the fixture waits for the
    device to come back before handing control to the tests.
    """
    if not _reboot_available(client):
        pytest.skip(
            "reboot endpoint not available in the flashed firmware "
            "(msc-e2e-api); reboot-dependent tests activate automatically "
            "once it lands"
        )
    _wait_for_reboot(client)


def _wait_for_reboot(client):
    """Poll /status until the device answers stably after a reboot.

    A single early success does not mean the stack is settled; require
    two consecutive successful calls a poll interval apart.
    """
    deadline = time.monotonic() + BOOT_TIMEOUT_S
    consecutive = 0
    while time.monotonic() < deadline:
        try:
            client.status()
            consecutive += 1
            if consecutive >= 2:
                return
        except camclient.CamClientError:
            consecutive = 0
        time.sleep(BOOT_POLL_S)
    pytest.fail(f"device did not come back within {BOOT_TIMEOUT_S}s")


def test_config_persists_to_nvs_across_reboot(client, guard, reboot_available):
    """A config saved to NVS survives a reboot; at least one candidate
    key round-trips, and the restore leaves NVS clean."""
    originals = {}
    changed = {}
    for key in PERSISTENCE_KEYS:
        originals[key] = client.config_get_value(key)
        changed[key] = _changed_value(key, originals[key])
        guard.set_config(key, changed[key])
    client.config_save("nvs")

    try:
        try:
            client.reboot()
        except camclient.CamClientError:
            pass  # the connection may drop mid-request while rebooting
        _wait_for_reboot(client)
        # Re-establish the wall clock the reboot cleared.
        guard.advance_time(int(time.time()))

        survived = [key for key in PERSISTENCE_KEYS
                    if client.config_get_value(key) == changed[key]]
        assert survived, (
            "no candidate key survived the reboot: the NVS save/load path "
            "did not persist any of "
            f"{PERSISTENCE_KEYS} (originals={originals}, "
            f"changed={changed})"
        )
        for key in PERSISTENCE_KEYS:
            if key not in survived:
                print(
                    f"\nNOTE: {key} did not round-trip - pinned by the card's "
                    "config.ini, which overlays NVS at boot; NVS was proven "
                    f"via {survived}.\n"
                )
    finally:
        for key, original in originals.items():
            client.config_set_value(key, original)
        client.config_save("nvs")


def _new_or_grown(before, after):
    """True when the log inventory gained a file or grew an existing one.

    Pre-existing logs from earlier sessions are baseline, not evidence
    of writes since the snapshot.
    """
    if set(after) - set(before):
        return True
    return any(after[name] > size
               for name, size in before.items() if name in after)


def test_after_set_time_cold_boot_gating(client, guard, reboot_available):
    """With sd_enabled=after_set_time, SD logging stays off until the
    wall clock is set after a cold boot, then starts writing.

    The gate is verified against the log inventory, not against /logs
    being empty: the card may hold baseline logs from earlier
    sessions, so only new or grown files prove that writes happened
    (or, before set_time, that none did).
    """
    mode = client.config_get_value("logging.sd_enabled")
    if mode.lower() != "after_set_time":
        pytest.skip(
            f"sd_enabled is {mode!r}, not after_set_time; cold-boot "
            "gating not applicable"
        )
    if not client.status().get("sd"):
        pytest.skip("SD card unavailable; cold-boot gating not testable")

    # Snapshot the inventory before the reboot so the boot-time write
    # window is inside the checked span; drain a flush cadence first so
    # in-flight writes from the running session land in the baseline.
    time.sleep(3)
    pre_gate = _log_sizes(client)

    try:
        client.reboot()
    except camclient.CamClientError:
        pass
    _wait_for_reboot(client)

    # A fresh boot has no wall clock yet: the after_set_time gate must
    # hold SD logging off. Allow a couple of flush cadences for any
    # wrongly-buffered data to surface, then require the inventory to
    # be unchanged from the pre-reboot baseline.
    time.sleep(3)
    still = _log_sizes(client)
    if _new_or_grown(pre_gate, still):
        pytest.fail(
            "SD log data was written on a cold boot before set_time "
            "despite sd_enabled=after_set_time"
        )

    # Setting the wall clock opens the gate: log writes now land on the
    # card (set_time itself produces one).
    guard.advance_time(int(time.time()))
    if not _wait_until(lambda: _new_or_grown(still, _log_sizes(client)),
                       30.0):
        pytest.fail(
            "SD log writing never started after set_time: the "
            "firmware's SD log sink never writes to the card"
        )
