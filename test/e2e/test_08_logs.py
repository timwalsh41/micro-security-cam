"""E2E: log listing, log tail, and date rotation/retention.

These tests only run when the device actually writes logs to the SD
card. A build with the SD log backend unwired never creates /logs, so
the module skips with a clear reason instead of failing.
"""
import calendar
import re
import time

import pytest

import camclient
from conftest import LOG_FILE_RE

# The suite advances device time in whole days for rotation/retention.
DAY_S = 86400


def _logs_listing(client):
    """Fetch the /logs listing, or None when it is unavailable."""
    resp = client.get("/logs?start=0")
    if resp.status != 200:
        return None
    return resp.json()


def _require_sd_logging(client, listing):
    """Skip when the build produces no SD logs at all."""
    if listing is None or listing["total"] == 0:
        pytest.skip(
            "no SD log files are being produced by this firmware build "
            "(/logs is empty or absent), so log coverage cannot run"
        )


def _date_unix(date_str):
    """Midnight-UTC unix timestamp of a YYYYMMDD date string."""
    return calendar.timegm(time.strptime(date_str, "%Y%m%d"))


def _date_str(unix_ts):
    """YYYYMMDD date string of a unix timestamp (UTC)."""
    return time.strftime("%Y%m%d", time.gmtime(unix_ts))


def _log_names(client):
    """Names of the log files currently on the card."""
    listing = _logs_listing(client)
    if listing is None:
        return None
    return [e["n"] for e in listing["entries"] if not e["d"]]


def test_logs_listing_shape(client):
    """The /logs listing carries the documented JSON shape."""
    listing = _logs_listing(client)
    _require_sd_logging(client, listing)
    assert listing["dir"] == "/logs"
    assert listing["start"] == 0
    for entry in listing["entries"]:
        assert set(entry.keys()) == {"n", "s", "d", "r"}
        if not entry["d"]:
            assert LOG_FILE_RE.match(entry["n"]), \
                f"unexpected entry in /logs: {entry['n']!r}"


def test_log_tail_is_formatted_text(client):
    """A log file's tail is formatted log text."""
    names = _log_names(client)
    if names is None or not names:
        pytest.skip(
            "no SD log files are being produced by this firmware build "
            "(/logs is empty or absent), so log coverage cannot run"
        )
    name = max(names)  # latest date
    text = client.log_file(name)
    assert text, f"log tail for {name} is empty"
    assert re.search(
        r"\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\] ", text
    ), f"log tail for {name} is not formatted log text: {text[:80]!r}"


def test_log_missing_file_404(client):
    """A well-formed but absent log file is a 404."""
    resp = client.get("/log?file=19990101.log")
    assert resp.status == 404


def test_log_bad_filename_refused(client):
    """Non-log filenames are refused with 403."""
    for bad in ("../../config.ini", "notes.txt", "202601011.log"):
        resp = client.get("/log?" + camclient.urllib.parse.urlencode(
            {"file": bad}))
        assert resp.status == 403, f"file={bad!r} returned {resp.status}"


def test_log_rotation_advances_to_a_new_date_file(client, guard, capture_ready):
    """Advancing device time starts a log file for the new date."""
    if capture_ready:
        pytest.skip(capture_ready)
    today = guard.device_date()
    advanced_date = _date_str(_date_unix(today) + 2 * DAY_S)

    # set_time itself produces a log line, which lands in the new
    # date's file after the rotation check runs.
    guard.advance_time(_date_unix(today) + 2 * DAY_S)
    try:
        names = _log_names(client)
        if names is None:
            pytest.skip(
                "no /logs listing after time advance: this firmware "
                "build does not write logs to the SD card"
            )
        guard.track_created(f"/logs/{advanced_date}.log")
        assert f"{advanced_date}.log" in names, \
            f"no log file for advanced date {advanced_date}: {names}"
    finally:
        # Restore immediately so later tests run at the real date.
        guard.advance_time(int(time.time()))


def test_log_retention_deletes_expired_suite_log(client, guard, capture_ready):
    """Retention removes a log file the suite backdated, leaving the
    device's own baseline logs untouched.

    Known hole, skipped rather than built around: when the card holds
    baseline logs older than today, the retention pass would delete
    them too, so the destructive assertion only runs when it cannot
    destroy pre-existing device data.
    """
    if capture_ready:
        pytest.skip(capture_ready)
    today = guard.device_date()
    today_unix = _date_unix(today)
    retention_days = int(client.config_get_value("logging.retention_days"))

    names = _log_names(client)
    if names is None or not names:
        pytest.skip(
            "no SD log files are being produced by this firmware build "
            "(/logs is empty or absent), so log coverage cannot run"
        )
    existing_dates = {name[:8] for name in names if LOG_FILE_RE.match(name)}
    if any(date < today for date in existing_dates):
        pytest.skip(
            "card holds baseline logs older than today; the retention "
            "pass would delete device data the suite cannot restore"
        )
    if today_unix - DAY_S < 0:
        pytest.skip("device clock is at the epoch; cannot backdate a log")

    # Backdate one day to make the suite create a log file for a date
    # that a later advance will expire. set_time's own log line lands
    # in the backdated file.
    backdated = _date_str(today_unix - DAY_S)
    guard.advance_time(today_unix - DAY_S)
    guard.advance_time(int(time.time()))
    try:
        names = _log_names(client)
        assert f"{backdated}.log" in names, \
            f"backdated log {backdated}.log was not created: {names}"

        # Advance by exactly retention_days: the cutoff lands on today,
        # so the backdated file expires while today's baseline survives.
        expiry = _date_str(today_unix + retention_days * DAY_S)
        guard.advance_time(today_unix + retention_days * DAY_S)
        names = _log_names(client)
        assert f"{backdated}.log" not in names, \
            f"expired log {backdated}.log survived retention"
        assert f"{today}.log" in names, \
            f"baseline log {today}.log was destroyed by retention"
        guard.track_created(f"/logs/{expiry}.log")
    finally:
        guard.advance_time(int(time.time()))
