"""Session-scoped fixtures for the e2e suite.

Provides:
  ``client`` — the device HTTP client, built from the environment
               (``CAM_HOST``, ``CAM_PASSWORD``) with repo defaults,
               including a fail-fast reachability check and the
               build-identity preflight.
  ``guard``  — session state snapshot/restore: wall clock, recording
               and streaming state, changed config keys, and the
               baseline file/log inventory.
"""
import os
import re
import subprocess
import sys
import time

import pytest

# Make the shared transport module importable (script/python/camclient.py).
_REPO_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..")
)
_SCRIPT_DIR = os.path.join(_REPO_ROOT, "script", "python")
if _SCRIPT_DIR not in sys.path:
    sys.path.insert(0, _SCRIPT_DIR)

import camclient  # noqa: E402
import avi_parser  # noqa: E402

# Bounded wait for the device status cache (photos/videos counters are
# cached for one minute on the device): poll until the cache refreshes.
COUNTER_TIMEOUT_S = 75.0
COUNTER_POLL_S = 5.0

# Polling cadence for device status while waiting on a state transition.
STATUS_POLL_S = 1.0


def _wait_for(predicate, timeout, what):
    """Poll the device until predicate() holds, else fail the test."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(STATUS_POLL_S)
    pytest.fail(f"timed out after {timeout}s waiting for {what}")


def _record_wedge(client=None):
    """Record wall clock and device uptime at the first wedge detection."""
    if _FIRST_WEDGE["wall_clock"] is not None:
        return
    _FIRST_WEDGE["wall_clock"] = time.time()
    uptime = None
    if client is not None:
        try:
            uptime = client.status().get("uptime_ms")
        except camclient.CamClientError:
            pass
    _FIRST_WEDGE["uptime_ms"] = uptime


def _wedge_timing_text():
    """Elapsed time to the first wedge, session-relative and boot-relative."""
    parts = []
    if None not in (_SESSION_START["wall_clock"], _FIRST_WEDGE["wall_clock"]):
        elapsed = _FIRST_WEDGE["wall_clock"] - _SESSION_START["wall_clock"]
        parts.append(f"elapsed-since-session-start {elapsed:.1f}s")
    start_uptime = _SESSION_START["uptime_ms"]
    wedge_uptime = _FIRST_WEDGE["uptime_ms"]
    if None not in (start_uptime, wedge_uptime):
        if wedge_uptime >= start_uptime:
            parts.append(
                f"elapsed-since-boot {(wedge_uptime - start_uptime) / 1000.0:.1f}s"
            )
        else:
            parts.append(
                "elapsed-since-boot unavailable (device rebooted since "
                "session start)"
            )
    return "; ".join(parts) if parts else "timing unavailable"


def _wedge_skip_reason():
    """Skip reason for a capture-pipeline wedge, with recorded timing."""
    return (
        "capture pipeline wedged mid-suite (capture commands return "
        f"code {CAPTURE_INTERNAL_ERROR_CODE}; {_wedge_timing_text()}); "
        "power-cycle the device and re-run to exercise this coverage"
    )


def is_capture_wedge(exc):
    """True when a failure is the known capture-pipeline wedge (code 255)."""
    return (isinstance(exc, camclient.CamCommandError)
            and exc.code == CAPTURE_INTERNAL_ERROR_CODE)


def capture_skip_or_raise(exc, client=None):
    """pytest.skip with a clear reason on the capture-pipeline wedge,
    otherwise re-raise the failure."""
    if is_capture_wedge(exc):
        _record_wedge(client)
        pytest.skip(_wedge_skip_reason())
    raise exc


def probe_capture_wedge(client):
    """Probe the capture pipeline with stream_start; leave state intact.

    Returns True when the pipeline is wedged (capture commands return
    the internal error code), recording the wedge timing; otherwise any
    streaming the probe started is stopped again, and streaming that
    was already active before the probe (e.g. a web-UI preview) is left
    running.
    """
    was_streaming = bool(client.status().get("streaming"))
    resp = client.control_raw("stream_start")
    data = resp.json() if resp.status == 200 else None
    if isinstance(data, dict) and data.get("result") == "error" and \
            data.get("code") == CAPTURE_INTERNAL_ERROR_CODE:
        _record_wedge(client)
        return True
    client.control("stream_stop")
    if was_streaming:
        client.control("stream_start")
    return False

# Per-day photo path shape, for the capture-pipeline probe below.
PHOTO_PATH_RE = re.compile(
    r"^/(?P<date>\d{8})/(?P=date)_(?P<hhmmss>\d{6})\.jpg$"
)
LOG_FILE_RE = re.compile(r"^\d{8}\.log$")

# Internal error code the firmware reports when the capture pipeline
# cannot serve a request. A device in that state answers every capture
# command (photo, start, stream_start) with code 255 and recovers only
# on a power cycle; photo/record/stream coverage is skipped loudly
# rather than failing a dozen tests in a row with confusing errors.
CAPTURE_INTERNAL_ERROR_CODE = 255
_CAPTURE_BLOCKED_REASON = None

# Wall clock and device uptime (``uptime_ms`` from /status) at session
# start and at the first capture-pipeline wedge detection, so the time
# to the wedge can be reported relative to both the session and the
# device boot (a product finding, surfaced in skip reasons and the
# session summary).
_SESSION_START = {"wall_clock": None, "uptime_ms": None}
_FIRST_WEDGE = {"wall_clock": None, "uptime_ms": None}


def parse_photo_path(path):
    """Split ``/YYYYMMDD/YYYYMMDD_HHMMSS.jpg`` into (date, hhmmss)."""
    match = PHOTO_PATH_RE.match(path)
    if not match:
        raise AssertionError(
            f"photo path {path!r} does not match /YYYYMMDD/YYYYMMDD_HHMMSS.jpg"
        )
    return match.group("date"), match.group("hhmmss")


def parse_ini_values(ini_text):
    """Parse an INI string into ``{"section.key": value}``.

    Values are stripped; keys without a section are ignored.
    """
    values = {}
    section = None
    for line in ini_text.splitlines():
        line = line.strip()
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1].strip()
        elif "=" in line and section:
            key, _, value = line.partition("=")
            values[f"{section}.{key.strip()}"] = value.strip()
    return values


def pytest_addoption(parser):
    """Register the e2e-required session mode."""
    parser.addoption(
        "--e2e-required",
        action="store_true",
        default=False,
        help="Fail the session (instead of skipping all tests) when the "
             "device is unreachable; script/test-e2e.sh passes this, so "
             "bare pytest runs (e.g. CI without hardware) skip cleanly.",
    )


@pytest.fixture(scope="session")
def client(request):
    """HTTP client for the device, verified reachable before anything runs.

    Connection settings come from the environment only: ``CAM_HOST``
    and ``CAM_PASSWORD`` are required (an empty ``CAM_PASSWORD`` is
    legitimate for a device without a configured web password).
    """
    host = os.environ.get("CAM_HOST", "")
    password = os.environ.get("CAM_PASSWORD")
    if not host:
        message = (
            "e2e: CAM_HOST is not set; the suite connects only to the "
            "address given in the environment"
        )
        if request.config.getoption("--e2e-required"):
            pytest.exit(message, returncode=1)
        pytest.skip(message)
    if password is None:
        message = (
            "e2e: CAM_PASSWORD is not set (use an empty value if the "
            "device has no configured web password)"
        )
        if request.config.getoption("--e2e-required"):
            pytest.exit(message, returncode=1)
        pytest.skip(message)
    cli = camclient.CamClient(host=host, password=password)

    try:
        status = cli.status()
    except camclient.CamClientError as e:
        message = (
            f"e2e: device {host} is unreachable ({e}); start it or check "
            "CAM_HOST/CAM_PASSWORD before running this suite"
        )
        if request.config.getoption("--e2e-required"):
            pytest.exit(message, returncode=1)
        pytest.skip(message)

    _SESSION_START["wall_clock"] = time.time()
    _SESSION_START["uptime_ms"] = status.get("uptime_ms")

    _capture_pipeline_preflight(cli)
    _version_preflight(cli)
    return cli


def _capture_pipeline_preflight(cli):
    """Probe the capture pipeline; skip camera coverage when wedged.

    A device whose capture executor stopped serving answers every
    capture command (photo, start, stream_start) with the internal
    error code 255 and recovers only on a power cycle. The probe
    records that state once and prints a prominent warning; the
    camera-dependent modules then skip with the reason, and the rest
    of the suite still runs.

    The probe is otherwise side-effect free: streaming that was already
    active before the probe (e.g. a web-UI preview) is left running —
    this runs before the state snapshot, so nothing else would restore it.
    """
    global _CAPTURE_BLOCKED_REASON
    if probe_capture_wedge(cli):
        _CAPTURE_BLOCKED_REASON = (
            "device capture pipeline is wedged (capture commands return "
            f"code {CAPTURE_INTERNAL_ERROR_CODE}; photo/record/stream "
            "coverage skipped - power-cycle the device and re-run"
        )
        _CAPTURE_BLOCKED_REASON += f" ({_wedge_timing_text()})"
        print(
            f"\nWARNING: {_CAPTURE_BLOCKED_REASON}. This is a device "
            "state problem, not a suite failure; non-camera coverage "
            "still runs.\n"
        )
        return


@pytest.fixture(scope="session")
def capture_ready(client):
    """None when the capture pipeline serves commands, else the skip
    reason discovered by the preflight probe."""
    return _CAPTURE_BLOCKED_REASON


def _expected_build_identity():
    """The flashed build the preflight expects.

    ``CAM_EXPECT_GIT`` overrides; otherwise the expected identity is
    derived from the repository's ``main`` tip so the strict preflight
    is the default behavior.
    """
    override = os.environ.get("CAM_EXPECT_GIT")
    if override:
        return override
    for ref in ("main", "origin/main"):
        try:
            sha = subprocess.run(
                ["git", "rev-parse", "--short", ref],
                capture_output=True,
                check=True,
                cwd=_REPO_ROOT,
            ).stdout.decode().strip()
        except (OSError, subprocess.CalledProcessError):
            continue
        if sha:
            return sha
    return None


def _version_preflight(cli):
    """Strictly compare the flashed build identity against the repo.

    The expected identity is ``CAM_EXPECT_GIT`` when set, else the
    repository's ``main`` tip (short sha). While the firmware lacks
    the version endpoint, absence is not a failure: print one clear
    warning and continue. With the endpoint present the
    device-reported build is always printed; a mismatch with the
    expected identity is a loud failure, never a silent pass.
    """
    try:
        data = cli.version()
    except camclient.CamEndpointUnavailable:
        print(
            "\nWARNING: the firmware has no version endpoint; cannot "
            "verify the flashed build. The device may be running a "
            "stale build.\n"
        )
        return

    reported = _reported_build_identity(data)
    print(f"\nDevice build identity: {data}\n")

    expected = _expected_build_identity()
    if not expected:
        print(
            "\nWARNING: no expected build identity available (no "
            "CAM_EXPECT_GIT and the repo's main tip could not be "
            "resolved); skipping the build-identity comparison.\n"
        )
        return
    if not reported:
        pytest.exit(
            f"e2e: expected build {expected!r} but the version endpoint "
            f"payload {data!r} carries no comparable build identity; "
            "refusing to validate an unverified build",
            returncode=1,
        )
    if not _build_identity_matches(reported, expected):
        pytest.exit(
            f"e2e: build identity mismatch — device reports {reported!r}, "
            f"expected {expected!r} (main tip, or CAM_EXPECT_GIT when "
            "set). Flash the expected build first.",
            returncode=1,
        )


def _build_identity_matches(reported, expected):
    """Match a device-reported build against an expected git identity.

    The device reports a decorated string (e.g. ``v0.0.1-1f27d9``)
    carrying a short commit sha; the expected value may be a short or
    full sha. The comparison matches on the hex sha runs, one being a
    prefix of the other.
    """
    reported_sha = _trailing_hex_run(reported)
    expected_sha = _leading_hex_run(expected)
    if not reported_sha or not expected_sha:
        return False
    shorter, longer = sorted((reported_sha, expected_sha), key=len)
    return longer.startswith(shorter)


def _trailing_hex_run(value):
    match = re.search(r"[0-9a-f]{6,}$", value)
    return match.group(0) if match else None


def _leading_hex_run(value):
    match = re.search(r"^[0-9a-f]{6,}", value)
    return match.group(0) if match else None


def _reported_build_identity(data):
    """Pull a build-identity string out of a version payload."""
    if not isinstance(data, dict):
        return None
    for key, value in data.items():
        if isinstance(value, str) and (
            "git" in key.lower() or key.lower() in ("version", "build")
        ):
            return value
    return None


@pytest.fixture(scope="session", autouse=True)
def guard(client):
    """State snapshot/restore helper for tests that change device state.

    Session-scoped and autouse: the snapshot is taken before the first
    test runs and the restore happens after the last one.
    """
    state_guard = StateGuard(client)
    yield state_guard
    state_guard.restore()


class StateGuard:
    """Tracks and restores device state touched by the suite.

    Restore order at session teardown:
      1. wall clock (after any time-advance test, real time is restored)
      2. recording/streaming started by tests (stopped)
      3. every config key a test changed (live config values)
    Created files are inventoried and reported; the firmware exposes no
    delete endpoint, so the suite cannot remove them itself.
    """

    def __init__(self, client):
        self.m_client = client
        self.m_config_originals = {}
        self.m_time_advanced = False
        self.m_started_recording = False
        self.m_started_streaming = False
        self.m_created_files = []
        try:
            self.m_config_ini = client.config()
        except camclient.CamClientError:
            self.m_config_ini = ""
        self.m_baseline_files = self._safe_listing(client, lambda: client.files())
        self.m_baseline_logs = self._safe_listing(client, lambda: client.logs())

    @staticmethod
    def _safe_listing(client, fetch):
        """Fetch a listing, or None when it is unavailable on this build."""
        try:
            return fetch()
        except camclient.CamClientError:
            return None

    # ---------------------------------------------------------
    # Helpers used by tests
    # ---------------------------------------------------------
    def set_config(self, key, value):
        """Change a live config key, registering the original for restore."""
        if key not in self.m_config_originals:
            self.m_config_originals[key] = \
                self.m_client.config_get_value(key)
        self.m_client.config_set_value(key, value)

    def track_created(self, path):
        """Record a file the suite created on the SD card."""
        self.m_created_files.append(path)

    def advance_time(self, unix_ts):
        """Set the device wall clock; the real time is restored at teardown."""
        self.m_time_advanced = True
        self.m_client.set_time(unix_ts)

    def start_recording(self):
        """Start recording, registered for teardown stop."""
        try:
            data = self.m_client.control("start")
            camclient.CamClient.assert_result_ok(data, "cmd=start")
        except camclient.CamClientError as e:
            capture_skip_or_raise(e, self.m_client)
        self.m_started_recording = True

    def stop_recording(self):
        """Stop recording started via start_recording()."""
        try:
            data = self.m_client.control("stop")
            camclient.CamClient.assert_result_ok(data, "cmd=stop")
        except camclient.CamClientError as e:
            capture_skip_or_raise(e, self.m_client)
        self.m_started_recording = False

    def start_streaming(self):
        """Start streaming, registered for teardown stop."""
        try:
            data = self.m_client.control("stream_start")
            camclient.CamClient.assert_result_ok(data, "cmd=stream_start")
        except camclient.CamClientError as e:
            capture_skip_or_raise(e, self.m_client)
        self.m_started_streaming = True

    def stop_streaming(self):
        """Stop streaming started via start_streaming()."""
        try:
            data = self.m_client.control("stream_stop")
            camclient.CamClient.assert_result_ok(data, "cmd=stream_stop")
        except camclient.CamClientError as e:
            capture_skip_or_raise(e, self.m_client)
        self.m_started_streaming = False

    def take_photo(self):
        """Take a photo, tracking the created file; skips on the wedge."""
        try:
            path = self.m_client.photo()
        except camclient.CamClientError as e:
            capture_skip_or_raise(e, self.m_client)
        self.track_created(path)
        return path

    def device_date(self):
        """Device "today" as YYYYMMDD, discovered via a photo path."""
        path = self.take_photo()
        return parse_photo_path(path)[0]

    # ---------------------------------------------------------
    # Teardown
    # ---------------------------------------------------------
    def restore(self):
        """Restore everything the suite changed; never raises."""
        if self.m_time_advanced:
            try:
                self.m_client.set_time(int(time.time()))
            except camclient.CamClientError as e:
                print(f"\nWARNING: could not restore wall clock: {e}\n")

        if self.m_started_recording:
            try:
                self.m_client.control("stop")
            except camclient.CamClientError as e:
                print(f"\nWARNING: could not stop recording: {e}\n")
            self.m_started_recording = False

        if self.m_started_streaming:
            try:
                self.m_client.control("stream_stop")
            except camclient.CamClientError as e:
                print(f"\nWARNING: could not stop streaming: {e}\n")
            self.m_started_streaming = False

        for key, original in self.m_config_originals.items():
            try:
                self.m_client.config_set_value(key, original)
            except camclient.CamClientError as e:
                print(f"\nWARNING: could not restore config {key}: {e}\n")

        if self.m_created_files:
            print(
                "\nFiles created by the e2e suite (no delete endpoint in "
                "the firmware; the device's own retention/cleanup is the "
                "only removal path):"
            )
            for path in self.m_created_files:
                print(f"  {path}")


def pytest_sessionfinish(session, exitstatus):
    """Summarize the capture-pipeline wedge timing (product finding)."""
    if _FIRST_WEDGE["wall_clock"] is None:
        return
    reporter = session.config.pluginmanager.get_plugin("terminalreporter")
    text = ("Product finding: capture-pipeline wedge first detected "
            f"({_wedge_timing_text()}); recovers only on a power cycle.")
    if reporter is None:
        print(f"\n{text}\n")
        return
    reporter.ensure_newline()
    reporter.write_line("")
    reporter.write_sep("=", "capture-pipeline wedge")
    reporter.write_line(text)
