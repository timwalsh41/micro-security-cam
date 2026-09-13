#!/usr/bin/env python3
"""ESP32 Security Camera CLI.

Command-line tool for interacting with the ESP32 camera web server
via its HTTP command transport (``/status`` and ``/control`` endpoints).
"""
import argparse
import base64
import json
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

# =============================================================
# Connection settings — customise for your device
# =============================================================
DEFAULT_HOST = "192.168.39.101"   # ESP32 IP address (STA or AP mode)
PORT = 80                 # Web server port (from config.ini [web] section)
PASSWORD = "webpass"      # Web password (from config.ini [web] section).
                          # Set to "" if no password is configured.

# =============================================================
HOST = DEFAULT_HOST
BASE_URL = f"http://{HOST}:{PORT}"


def _build_request(url_path: str) -> urllib.request.Request:
    """Build an HTTP request, adding Basic Auth if a password is set."""
    url = f"{BASE_URL}{url_path}"
    req = urllib.request.Request(url)
    if PASSWORD:
        credentials = base64.b64encode(
            f"admin:{PASSWORD}".encode()
        ).decode()
        req.add_header("Authorization", f"Basic {credentials}")
    return req


def _send(url_path: str) -> int:
    """Send an HTTP GET and print the JSON response.

    Returns 0 on success, 1 on failure.
    """
    req = _build_request(url_path)
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            body = resp.read().decode()

            # Some handlers return their payload verbatim with a JSON
            # content type (e.g. photo returns the saved file path as
            # plain text). The firmware only emits these bare bodies for
            # successful dispatch; errors come back as the JSON envelope
            # below, so a non-empty non-JSON body is a success payload.
            try:
                data = json.loads(body)
            except json.JSONDecodeError:
                if body.strip():
                    print(body.strip())
                    return 0
                print("Error: empty response body", file=sys.stderr)
                return 1

            # If the response has extra fields beyond result/code
            # (e.g. status telemetry), pretty-print everything.
            extra_keys = set(data.keys()) - {"result", "code"}
            if extra_keys:
                print(json.dumps(data, indent=2))
            elif data.get("result") == "ok":
                print("OK")
            else:
                print(f"Error (code {data.get('code', '?')})")

            # A 200 response with a valid JSON body is a success. The
            # "result" field is only emitted by the firmware's fallback
            # path (e.g. start/stop); /status returns telemetry
            # without a "result" key, so it must not be treated as a
            # failure just because that key is absent.
            return 1 if data.get("result") == "error" else 0

    except urllib.error.HTTPError as e:
        err_body = e.read().decode()
        print(f"HTTP {e.code}: {err_body}", file=sys.stderr)
        return 1
    except urllib.error.URLError as e:
        print(f"Connection failed: {e.reason}", file=sys.stderr)
        return 1
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1


def _cmd_status() -> int:
    """GET /status — returns camera status JSON."""
    return _send("/status")


def _cmd_control(args) -> int:
    """GET /control?cmd=<value> — dispatches a command."""
    return _send(f"/control?cmd={args.cmd}")


def _cmd_set_time() -> int:
    """GET /control?cmd=set_time&ts=<unix_timestamp> — sets system time.

    Uses the current wall-clock time as the Unix timestamp.
    Requires firmware with SET_TIME web support
    (see the ``set_time`` mapping in ``map_cmd_string()`` and ``handle_control()``
    in ``web_command_transport.cpp``).
    """
    unix_ts = int(time.time())
    return _send(f"/control?cmd=set_time&ts={unix_ts}")


def _cmd_config() -> int:
    """GET /control?cmd=config — returns the full live config as INI text."""
    return _send(f"/control?cmd=config")


def _cmd_config_get(args) -> int:
    """GET /control?cmd=config_get_value&key=<key> — read one setting."""
    key = urllib.parse.quote(args.key, safe="")
    return _send(f"/control?cmd=config_get_value&key={key}")


def _cmd_config_set(args) -> int:
    """GET /control?cmd=config_set_value&key=<key>&value=<value>.

    Sets one setting in the live in-RAM config. The change is not
    persisted until a `config-save` is issued.
    """
    key = urllib.parse.quote(args.key, safe="")
    value = urllib.parse.quote(args.value, safe="")
    return _send(f"/control?cmd=config_set_value&key={key}&value={value}")


def _cmd_config_save(args) -> int:
    """GET /control?cmd=config_save&dest=<dest> — persist the live config.

    The destination is case-insensitive: `nvs` writes the live config to
    NVS flash, `sd` writes it to /config.ini on the SD card.
    """
    dest = args.dest.lower()
    return _send(f"/control?cmd=config_save&dest={dest}")


def main() -> int:
    """Parse arguments and dispatch the requested command."""
    parser = argparse.ArgumentParser(
        description="ESP32 Security Camera CLI",
    )
    parser.add_argument(
        "--ip",
        default=DEFAULT_HOST,
        help=f"ESP32 IP address (default: {DEFAULT_HOST})",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    subparsers.add_parser("status", help="Get camera status")

    ctrl = subparsers.add_parser(
        "control", help="Send a control command (start, stop, photo, "
                        "stream_start, stream_stop, stream_list)",
    )
    ctrl.add_argument(
        "cmd",
        choices=["start", "stop", "photo",
                 "stream_start", "stream_stop", "stream_list"],
        help="Command to execute",
    )

    subparsers.add_parser(
        "set-time",
        help="Set system time from current datetime ",
    )

    subparsers.add_parser(
        "config",
        help="Get the full live configuration as INI text",
    )

    config_get = subparsers.add_parser(
        "config-get",
        help="Read a single configuration value (section.key)",
    )
    config_get.add_argument("key", help="Setting path, e.g. wifi.ssid")

    config_set = subparsers.add_parser(
        "config-set",
        help="Set a single configuration value in the live config",
    )
    config_set.add_argument("key", help="Setting path, e.g. wifi.ssid")
    config_set.add_argument(
        "value",
        nargs="?",
        default="",
        help="New value (omit to blank the setting)",
    )

    config_save = subparsers.add_parser(
        "config-save",
        help="Persist the live config to NVS or the SD card",
    )
    config_save.add_argument(
        "dest",
        choices=["nvs", "sd", "NVS", "SD"],
        help="Destination: nvs (flash) or sd (/config.ini on the card)",
    )

    args = parser.parse_args()

    global HOST, BASE_URL
    HOST = args.ip
    BASE_URL = f"http://{HOST}:{PORT}"

    if args.command == "status":
        return _cmd_status()
    elif args.command == "control":
        return _cmd_control(args)
    elif args.command == "set-time":
        return _cmd_set_time()
    elif args.command == "config":
        return _cmd_config()
    elif args.command == "config-get":
        return _cmd_config_get(args)
    elif args.command == "config-set":
        return _cmd_config_set(args)
    elif args.command == "config-save":
        return _cmd_config_save(args)
    return 1


if __name__ == "__main__":
    sys.exit(main())
