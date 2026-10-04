"""E2E: config get/set over the web transport on real hardware."""
import camclient

from conftest import parse_ini_values


def test_config_get_value_round_trip(client, guard):
    """A set value reads back, and the guard restores the original."""
    key = "camera.jpeg_quality"
    original = client.config_get_value(key)
    changed = "63" if original != "63" else "10"

    guard.set_config(key, changed)
    assert client.config_get_value(key) == changed


def test_config_set_value_range_rejection(client, guard):
    """Out-of-range values are refused and leave the key unchanged."""
    key = "web.max_streams"
    original = client.config_get_value(key)
    resp = client.control_raw(
        "config_set_value", extra={"key": key, "value": "5"}
    )
    assert resp.status == 200
    data = resp.json()
    assert data.get("result") == "error", \
        f"out-of-range value was accepted: {data!r}"
    assert client.config_get_value(key) == original


def test_config_get_value_unknown_key_rejected(client):
    """Reading an unknown key is an INVALID_ARGS error, not a crash."""
    resp = client.control_raw(
        "config_get_value", extra={"key": "nosuch.key"}
    )
    assert resp.status == 200
    assert resp.json().get("result") == "error"


def test_config_set_value_unknown_key_rejected(client):
    """Writing an unknown key is an INVALID_ARGS error, not a crash."""
    resp = client.control_raw(
        "config_set_value", extra={"key": "nosuch.key", "value": "1"}
    )
    assert resp.status == 200
    assert resp.json().get("result") == "error"


def test_config_full_ini_round_trip(client, guard):
    """The full INI dump parses into the same key the value API sees."""
    values = parse_ini_values(client.config())
    assert "camera.jpeg_quality" in values
    assert values["camera.jpeg_quality"] == \
        client.config_get_value("camera.jpeg_quality")
