"""E2E: authentication behavior on real hardware.

The device under test has a configured web password, so the suite
exercises rejection with missing and wrong credentials. The
no-password auth matrix (config/file routes refused outright when no
password is configured) is acknowledged as a skip: testing it would
require disturbing the device's configured password.
"""
import pytest

import camclient


@pytest.fixture(scope="module")
def anonymous(client):
    """A client that sends no Authorization header at all."""
    return client.with_credentials(None)


@pytest.fixture(scope="module")
def wrong_password(client):
    """A client with the wrong password."""
    return client.with_credentials("definitely-not-the-password")


def test_status_rejects_missing_credentials(anonymous):
    """/status without credentials is a 401 with a Basic challenge."""
    resp = anonymous.get("/status")
    assert resp.status == 401
    assert resp.header("www-authenticate") is not None, \
        "401 response carries no WWW-Authenticate Basic challenge"


def test_status_rejects_wrong_password(wrong_password):
    """/status with a wrong password is a 401 with a Basic challenge."""
    resp = wrong_password.get("/status")
    assert resp.status == 401
    assert resp.header("www-authenticate") is not None


def test_control_rejects_missing_credentials(anonymous):
    """/control without credentials is a 401."""
    resp = anonymous.control_raw("photo")
    assert resp.status == 401


def test_stream_rejects_missing_credentials(anonymous):
    """/stream without credentials is a 401 (unauthorized or refusal)."""
    resp = anonymous.get("/stream", timeout=10.0)
    assert resp.status == 401


def test_update_rejects_missing_credentials(anonymous):
    """POST /update without credentials is refused."""
    resp = anonymous.post_form("/update", {"config": "[camera]\n"})
    assert resp.status in (401, 403), \
        f"unauthenticated update returned {resp.status}"


def test_files_reject_missing_credentials(anonymous):
    """File routes require credentials even with a configured password."""
    for path in ("/files", "/logs?start=0"):
        resp = anonymous.get(path)
        assert resp.status == 401, f"{path} returned {resp.status}"


def test_no_password_auth_matrix_not_exercised(client):
    """The no-password matrix is a known, acknowledged hole.

    With no password configured the firmware refuses config and file
    routes outright. Exercising that state would require disturbing
    the device's configured password (and a reboot, since auth uses a
    register-time snapshot), so it is skipped rather than built
    around.
    """
    pytest.skip(
        "no-password auth matrix would require disturbing the device's "
        "configured web password; not exercised by this suite"
    )
