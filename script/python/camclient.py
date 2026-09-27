#!/usr/bin/env python3
"""HTTP transport client for the ESP32 security camera web server.

Single definition of the camera's HTTP transport (paths, query
parameter formats, Basic auth, request timeouts, and response
parsing). Both the CLI (`camera_cli.py`) and the end-to-end test
suite (`test/e2e/`) use this module so there is exactly one place
that knows how to talk to the device.

Only the Python standard library is used (urllib / http.client),
matching the dependency-free profile of the CLI.
"""
import base64
import http.client
import json
import socket
import time
import urllib.error
import urllib.parse
import urllib.request

# =============================================================
# Connection settings — defaults match the flashed device config
# =============================================================
DEFAULT_HOST = "192.168.39.101"   # ESP32 IP address (STA or AP mode)
DEFAULT_PORT = 80                 # Web server port (config.ini [web] section)
DEFAULT_PASSWORD = "webpass"      # Web password (config.ini [web] section).
                                  # Empty string if no password is configured.

# Per-request timeout in seconds.
DEFAULT_TIMEOUT = 10.0

# Keys of the /status telemetry payload. Used to recognize the
# firmware's unknown-command fallback (an unknown `cmd` maps to
# GET_STATUS and returns this telemetry instead of an error).
STATUS_KEYS = frozenset(
    [
        "recording",
        "streaming",
        "sd",
        "frames",
        "file_size",
        "connected_streams",
        "sd_free_bytes",
        "photos",
        "videos",
    ]
)


class CamClientError(Exception):
    """Transport-level failure (unreachable device, malformed response)."""


class CamCommandError(CamClientError):
    """The device answered with an error result envelope."""

    def __init__(self, message, code=None):
        super().__init__(message)
        self.code = code


class CamEndpointUnavailable(CamClientError):
    """The requested endpoint is absent from the flashed firmware.

    Unknown web commands currently fall back to the status telemetry
    payload instead of an error, so absence is detected by recognizing
    that fallback (or an error envelope) in the response body.
    """


class CamResponse:
    """Raw HTTP response: status code, body bytes, and headers."""

    def __init__(self, status, body, headers=None):
        self.status = status
        self.body = body
        self.headers = headers or {}

    def header(self, name):
        """Case-insensitive header lookup, or None when absent."""
        name = name.lower()
        for key, value in self.headers.items():
            if key.lower() == name:
                return value
        return None

    def text(self):
        """Body decoded as UTF-8 (errors replaced, not raised)."""
        return self.body.decode("utf-8", errors="replace")

    def json(self):
        """Body parsed as JSON, or None when it is not valid JSON."""
        try:
            return json.loads(self.text())
        except json.JSONDecodeError:
            return None


class CamClient:
    """HTTP client for the camera's web endpoints.

    All methods raise :class:`CamClientError` when the device cannot be
    reached or the response cannot be delivered; HTTP error statuses
    are returned as :class:`CamResponse` so callers (and tests) can
    assert on them.
    """

    def __init__(self, host=DEFAULT_HOST, password=DEFAULT_PASSWORD,
                 port=DEFAULT_PORT, timeout=DEFAULT_TIMEOUT):
        self.host = host
        self.password = password
        self.port = port
        self.timeout = timeout

    # ---------------------------------------------------------
    # Credentials helpers
    # ---------------------------------------------------------
    def with_credentials(self, password):
        """Return a shallow copy of this client with another password.

        ``password=None`` or an empty string sends no Authorization
        header at all.
        """
        return CamClient(host=self.host, password=password,
                         port=self.port, timeout=self.timeout)

    def _auth_header(self):
        """Build the Basic auth header value, or None when unauthenticated."""
        if not self.password:
            return None
        credentials = base64.b64encode(
            f"admin:{self.password}".encode()
        ).decode()
        return f"Basic {credentials}"

    # ---------------------------------------------------------
    # Raw requests
    # ---------------------------------------------------------
    def get(self, url_path, timeout=None):
        """GET a path, returning a CamResponse.

        Raises CamClientError on connection-level failure. HTTP error
        statuses (e.g. 401, 403, 404) come back as normal responses.
        """
        return self._request("GET", url_path, body=None, timeout=timeout)

    def post_form(self, url_path, fields, timeout=None):
        """POST a urlencoded form to a path, returning a CamResponse."""
        body = urllib.parse.urlencode(fields).encode("utf-8")
        return self._request("POST", url_path, body=body, timeout=timeout)

    def _request(self, method, url_path, body, timeout):
        url = f"http://{self.host}:{self.port}{url_path}"
        req = urllib.request.Request(url, data=body, method=method)
        auth = self._auth_header()
        if auth:
            req.add_header("Authorization", auth)
        if body is not None:
            req.add_header(
                "Content-Type", "application/x-www-form-urlencoded"
            )
        try:
            with urllib.request.urlopen(
                req, timeout=timeout if timeout is not None else self.timeout
            ) as resp:
                return CamResponse(resp.status, resp.read(), resp.headers)
        except urllib.error.HTTPError as e:
            # Error statuses are responses too; callers assert on them.
            return CamResponse(e.code, e.read(), e.headers)
        except (urllib.error.URLError, socket.timeout, OSError) as e:
            reason = getattr(e, "reason", None) or e
            error = CamClientError(f"connection failed: {reason}")
            error.reason = reason
            raise error from e

    # ---------------------------------------------------------
    # JSON helpers
    # ---------------------------------------------------------
    def get_json(self, url_path, timeout=None):
        """GET a path and parse the JSON body, raising on HTTP errors."""
        resp = self.get(url_path, timeout=timeout)
        if resp.status != 200:
            raise CamClientError(
                f"GET {url_path} failed: HTTP {resp.status} {resp.text()}"
            )
        data = resp.json()
        if data is None:
            raise CamClientError(
                f"GET {url_path} returned non-JSON body: {resp.text()!r}"
            )
        return data

    @staticmethod
    def assert_result_ok(data, what):
        """Raise CamCommandError when data is an explicit error envelope.

        Handlers that return a response payload (e.g. stream_list,
        files listings) send it without the result envelope, so a
        parsed body without a ``result`` key is treated as success.
        """
        if not isinstance(data, dict):
            raise CamClientError(f"{what}: unexpected non-object response")
        if "result" in data and data["result"] != "ok":
            raise CamCommandError(
                f"{what}: error envelope {data!r}", code=data.get("code")
            )

    # ---------------------------------------------------------
    # Endpoint wrappers (documented in README "Endpoints")
    # ---------------------------------------------------------
    def status(self):
        """GET /status — camera status telemetry JSON."""
        return self.get_json("/status")

    def control(self, cmd, extra=None):
        """GET /control?cmd=... — returns the JSON result envelope."""
        path = f"/control?cmd={urllib.parse.quote(cmd, safe='')}"
        if extra:
            path += "&" + urllib.parse.urlencode(extra)
        return self.get_json(path)

    def control_raw(self, cmd, extra=None, timeout=None):
        """GET /control?cmd=... without interpreting the response."""
        path = f"/control?cmd={urllib.parse.quote(cmd, safe='')}"
        if extra:
            path += "&" + urllib.parse.urlencode(extra)
        return self.get(path, timeout=timeout)

    def photo(self):
        """GET /control?cmd=photo — returns the saved photo path string."""
        resp = self.control_raw("photo")
        if resp.status != 200:
            raise CamClientError(
                f"photo failed: HTTP {resp.status} {resp.text()}"
            )
        body = resp.text().strip()
        data = resp.json()
        if isinstance(data, dict) and data.get("result") == "error":
            raise CamCommandError(
                f"photo failed: {data!r}", code=data.get("code")
            )
        if not body:
            raise CamClientError("photo returned an empty body")
        return body

    def config(self):
        """GET /control?cmd=config — full live config as INI text."""
        resp = self.control_raw("config")
        if resp.status != 200:
            raise CamClientError(
                f"config read failed: HTTP {resp.status} {resp.text()}"
            )
        return resp.text()

    def config_get_value(self, key):
        """GET /control?cmd=config_get_value&key=... — plain value string."""
        resp = self.control_raw("config_get_value", extra={"key": key})
        if resp.status != 200:
            raise CamClientError(
                f"config_get_value({key}) failed: "
                f"HTTP {resp.status} {resp.text()}"
            )
        return resp.text().strip()

    def config_set_value(self, key, value):
        """GET /control?cmd=config_set_value — set one live config value."""
        data = self.control(
            "config_set_value", extra={"key": key, "value": value}
        )
        self.assert_result_ok(data, f"config_set_value({key}={value})")
        return data

    def config_save(self, dest):
        """GET /control?cmd=config_save&dest=nvs|sd — persist the config."""
        data = self.control("config_save", extra={"dest": dest})
        self.assert_result_ok(data, f"config_save({dest})")
        return data

    def set_time(self, unix_ts):
        """GET /control?cmd=set_time&ts=... — set the device wall clock."""
        data = self.control("set_time", extra={"ts": str(int(unix_ts))})
        self.assert_result_ok(data, "set_time")
        return data

    def files(self, dir="", start=0, timeout=None):
        """GET /files?dir=&start= — directory listing JSON."""
        extra = {}
        if dir:
            extra["dir"] = dir
        if start:
            extra["start"] = str(start)
        path = "/files"
        if extra:
            path += "?" + urllib.parse.urlencode(extra)
        return self.get_json(path, timeout=timeout)

    def logs(self, start=0):
        """GET /logs?start= — log directory listing JSON."""
        path = "/logs"
        if start:
            path += f"?start={int(start)}"
        return self.get_json(path)

    def log_file(self, name):
        """GET /log?file=YYYYMMDD.log — log tail as text."""
        resp = self.get("/log?" + urllib.parse.urlencode({"file": name}))
        if resp.status != 200:
            raise CamClientError(
                f"log_file({name}) failed: HTTP {resp.status} {resp.text()}"
            )
        return resp.text()

    def download(self, path, timeout=60.0):
        """GET /download?path=... — file bytes from the SD card."""
        resp = self.get(
            "/download?" + urllib.parse.urlencode({"path": path}),
            timeout=timeout,
        )
        if resp.status != 200:
            raise CamClientError(
                f"download({path}) failed: HTTP {resp.status} {resp.text()}"
            )
        return resp.body

    def update(self, ini_text, dest=None):
        """POST /update — bulk config overlay (optionally persisted)."""
        fields = {"config": ini_text}
        if dest is not None:
            fields["dest"] = dest
        resp = self.post_form("/update", fields)
        if resp.status != 200:
            raise CamClientError(
                f"update failed: HTTP {resp.status} {resp.text()}"
            )
        return resp

    # ---------------------------------------------------------
    # Build identity (added by the companion firmware task)
    # ---------------------------------------------------------
    def version(self):
        """GET /control?cmd=version — build identity JSON.

        Returns the parsed version payload, or raises
        :class:`CamEndpointUnavailable` while the flashed firmware
        lacks the endpoint (its unknown-command fallback answers with
        the status telemetry).
        """
        resp = self.control_raw("version")
        if resp.status != 200:
            raise CamEndpointUnavailable(
                f"version endpoint returned HTTP {resp.status}"
            )
        data = resp.json()
        if _is_status_fallback(data) or _is_error_envelope(data):
            raise CamEndpointUnavailable(
                "version endpoint not present in the flashed firmware"
            )
        return data

    def reboot(self):
        """GET /control?cmd=reboot — restart the device.

        Raises :class:`CamEndpointUnavailable` while the flashed
        firmware lacks the endpoint. On success the connection may be
        dropped mid-reply by the restarting device, which is treated
        as a successful reboot request.
        """
        try:
            resp = self.control_raw("reboot", timeout=15.0)
        except CamClientError as e:
            if _is_connection_reset(e):
                return  # device dropped the connection while rebooting
            raise
        data = resp.json()
        if _is_status_fallback(data) or _is_error_envelope(data):
            raise CamEndpointUnavailable(
                "reboot endpoint not present in the flashed firmware"
            )
        return resp

    # ---------------------------------------------------------
    # MJPEG streaming
    # ---------------------------------------------------------
    def stream_frames(self, max_frames=3, frame_timeout=10.0):
        """Yield ``(headers, jpeg_bytes)`` parts from GET /stream.

        Parses the ``multipart/x-mixed-replace`` response with the
        firmware's ``CAM_BOUNDARY`` boundary. Stops after
        ``max_frames`` parts (the client closing the connection is the
        normal end of the iteration) and raises CamClientError when a
        part is malformed.
        """
        stream = self._open_stream()
        try:
            yield from _parse_multipart(
                stream, max_frames=max_frames, frame_timeout=frame_timeout
            )
        finally:
            stream.close()

    def _open_stream(self):
        """Open the /stream connection as a _StreamResponse.

        Uses http.client directly (instead of urllib) so the multipart
        parser can reach the underlying socket and enforce per-read
        timeouts on a response that never ends on its own.
        """
        conn = http.client.HTTPConnection(
            self.host, self.port, timeout=self.timeout
        )
        headers = {}
        auth = self._auth_header()
        if auth:
            headers["Authorization"] = auth
        try:
            conn.request("GET", "/stream", headers=headers)
            resp = conn.getresponse()
        except OSError as e:
            conn.close()
            raise CamClientError(f"stream connect failed: {e}") from e
        if resp.status != 200:
            body = resp.read().decode("utf-8", errors="replace")
            conn.close()
            raise CamClientError(
                f"stream open failed: HTTP {resp.status} {body}"
            )
        return _StreamResponse(conn, resp)


# =============================================================
# Multipart/x-mixed-replace streaming support
# =============================================================
class _StreamResponse:
    """Streaming HTTP response with close() and deadline-bounded reads.

    Wraps an http.client connection/response pair; read timeouts are
    enforced by adjusting the socket timeout before each read.
    """

    def __init__(self, conn, resp):
        self._conn = conn
        self._resp = resp

    def close(self):
        """Close the connection (the device frees the viewer slot)."""
        try:
            self._conn.close()
        except OSError:
            pass

    def read(self, max_len, remaining):
        """Read up to max_len bytes within a remaining-time budget."""
        if self._conn.sock is not None:
            self._conn.sock.settimeout(max(remaining, 0.1))
        try:
            return self._resp.read(min(max_len, 65536))
        except socket.timeout:
            raise CamClientError(
                "stream stalled: no bytes within the frame timeout"
            ) from None
        except OSError as e:
            raise CamClientError(f"stream read failed: {e}") from e


def _parse_multipart(stream, max_frames, frame_timeout):
    """Yield well-formed ``(headers, body)`` parts from a stream response.

    Parts are framed as:
      ``--CAM_BOUNDARY\\r\\n`` headers ``\\r\\n\\r\\n`` body ``\\r\\n``
    with each header carrying a Content-Length. Any deviation raises
    CamClientError.
    """
    boundary_line = b"--CAM_BOUNDARY"
    buf = b""
    frames = 0
    deadline = time.monotonic() + frame_timeout * max(max_frames, 1)

    while frames < max_frames:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise CamClientError(
                "stream timed out waiting for multipart frames"
            )
        chunk = stream.read(65536, remaining)
        if not chunk:
            raise CamClientError(
                "stream closed before the expected number of frames"
            )
        buf += chunk

        while frames < max_frames:
            start = buf.find(boundary_line)
            if start < 0:
                # Keep a tail in case a boundary straddles chunks.
                buf = buf[-len(boundary_line):]
                break
            header_start = start + len(boundary_line)
            # The final closing boundary has no headers.
            if buf[header_start:header_start + 2] == b"--":
                return
            header_end = buf.find(b"\r\n\r\n", header_start)
            if header_end < 0:
                buf = buf[start:]
                break
            header_text = buf[header_start:header_end].decode(
                "utf-8", errors="replace"
            )
            headers = {}
            for line in header_text.split("\r\n"):
                if ":" in line:
                    name, _, value = line.partition(":")
                    headers[name.strip().lower()] = value.strip()

            if headers.get("content-type") != "image/jpeg":
                raise CamClientError(
                    f"stream part is not image/jpeg: {headers!r}"
                )
            try:
                length = int(headers["content-length"])
            except (KeyError, ValueError):
                raise CamClientError(
                    f"stream part missing Content-Length: {headers!r}"
                ) from None

            body_start = header_end + 4
            available = len(buf) - body_start
            if available < length + 2:  # body + trailing \r\n
                buf = buf[start:]
                break
            jpeg = buf[body_start:body_start + length]
            _assert_wellformed_jpeg(jpeg)
            yield headers, jpeg
            frames += 1
            buf = buf[body_start + length + 2:]

    if frames == 0:
        raise CamClientError("stream produced no frames")


def _assert_wellformed_jpeg(data):
    """Raise CamClientError unless data is a syntactically complete JPEG."""
    if len(data) < 4 or data[0:2] != b"\xff\xd8":
        raise CamClientError("stream part does not start with JPEG SOI")
    if data[-2:] != b"\xff\xd9":
        raise CamClientError("stream part does not end with JPEG EOI")


# =============================================================
# Fallback detection helpers
# =============================================================
def _is_status_fallback(data):
    """True when a parsed body is the status telemetry fallback."""
    return isinstance(data, dict) and set(data.keys()) <= STATUS_KEYS


def _is_error_envelope(data):
    """True when a parsed body is a {"result": ...} result envelope."""
    return isinstance(data, dict) and "result" in data


def _is_connection_reset(error):
    """True when a CamClientError is a dropped/reset connection."""
    text = str(error).lower()
    return (
        "connection reset" in text
        or "broken pipe" in text
        or "timed out" in text
        or "remote end closed" in text
        or "incomplete" in text
    )
