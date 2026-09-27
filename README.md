# ESP32 Security Camera

A still photo and MJPEG video camera built on the Seeed XIAO ESP32S3 Sense. Provides video streaming, as well as JPEG images and MPEG video capture to a MicroSD card.

Built with the Arduino framework and PlatformIO.

## Hardware

- **Board**: Seeed XIAO ESP32S3 Sense (with onboard camera and MicroSD card slot)
- **Camera**: OV2640 module (included with the board)
- **Storage**: MicroSD card (FAT32 formatted)
- **LED**: GPIO 21 (active LOW; shared with the SD card chip-select pin)

## Project Structure

```
src/              Application source and HAL implementations
include/          Headers, including hal/ subdirectory for HAL interfaces
test/             Native and hardware test suites with mocks
  native/           Unit tests (run on host machine)
  hardware/         Integration tests (run on ESP32)
  mocks/            Mock HAL classes for native testing
docs/             Additional documentation
script/           Build helper scripts
```

## Prerequisites

- [PlatformIO CLI](https://docs.platformio.org/en/latest/core/installation.html)
- Python 3 with [uv](https://docs.astral.sh/uv/)
- Activate the PlatformIO environment before running commands, e.g.:
  ```bash
  source ~/.platformio/penv/bin/activate
  ```

## Build & Flash

```bash
# Build firmware for the ESP32 target
pio run -e seeed_xiao_esp32s3

# Flash to the connected board
pio run -e seeed_xiao_esp32s3 --target upload

# Open serial monitor
pio device monitor
```

## Running Tests

```bash
# Run all native unit tests (host machine, no hardware needed)
pio test -e native

# Run a specific test suite
pio test -e native -f native/test_camera_controller

# Run hardware integration tests (requires board connected via USB)
pio test -e seeed_xiao_esp32s3_test
```

### End-to-end regression suite

`test/e2e/` is a pytest suite that exercises the running device over
its real HTTP interface — coverage that complements the unit tests:
integration behavior of the web transport, SD-backed flows, photo /
recording / streaming on the actual camera, and download payload
validation, which host tests cannot observe.

The suite talks to the device with the same transport module as the
CLI (`script/python/camclient.py`). Flash before running: build from
the tip of your feature branch and flash manually, then run the suite
against it.

```bash
# Whole suite against the default device (192.168.39.101)
script/test-e2e.sh

# Subset / dry run / verbose
script/test-e2e.sh -k photo
script/test-e2e.sh --collect-only
script/test-e2e.sh -v
```

Configuration via environment:

| Variable | Meaning | Default |
|----------|---------|---------|
| `CAM_HOST` | device IP | `192.168.39.101` |
| `CAM_PASSWORD` | web password | repo CLI default |
| `CAM_EXPECT_GIT` | optional git identity the flashed build must report | unset |

Behavior notes:

- Tests run strictly sequentially; each request has a bounded timeout
  and the suite fails fast with a clear message when the device is
  unreachable.
- The suite snapshots device state at session start (wall clock,
  recording/streaming state, config keys it changes, file/log
  inventory) and restores it at teardown. The firmware exposes no
  delete endpoint, so media/log files the suite creates are
  inventoried and reported at the end rather than deleted.
- Build identity: when the firmware exposes `cmd=version` (companion
  change adding version/reboot endpoints), the suite compares it
  against `CAM_EXPECT_GIT` and fails loudly on a mismatch. Until
  then, absence prints one warning and continues.
- Reboot-dependent coverage (NVS persistence across reboot, the
  `after_set_time` cold-boot gating) is implemented and skips itself
  until the reboot endpoint is available, then activates without
  further changes.
- Known holes are skipped with explicit reasons rather than built
  around: SD-absent degraded mode needs the card physically removed,
  the no-password auth matrix would require disturbing the device's
  configured password, and exercising `/update` with `dest=sd` would
  rewrite `/config.ini` on the card.
- Keep this suite updated as features are added, with coverage that
  complements the unit tests rather than duplicating them.

## Configuration

The firmware ships with sensible defaults compiled in. At startup, the defaults are overlaid setting-by-setting from NVS (non-volatile flash storage, if a config was saved there), and then from the INI-style config file (`/config.ini`) on the SD card if present — **precedence: defaults < NVS < SD file**; a setting mentioned in a later source overrides earlier ones, settings not mentioned keep their earlier value.

Compiled-in defaults: WiFi settings blank (STA connection is skipped and the fallback AP starts immediately), AP SSID `MicroCam` with no password (open network), web port 80 with no password, and the camera/video defaults listed in the table below.

The config file is limited to 1280 bytes (content beyond this is ignored) and individual lines are limited to 128 characters.

### Managing configuration

Config changes made at runtime (serial `GET`/`SET` commands, i.e. `GET_CONFIG_VALUE`/`SET_CONFIG_VALUE`) apply to the **live in-RAM config only** — they are not persisted automatically. Every setting in the config is addressable by its `section.key` name: string settings accept any value that fits the field buffer, and numeric/boolean settings must satisfy the same constraints as the config file parser (e.g. `web.port` 1–65535, `web.max_streams` 1–4, `video.fps` 1–30, `video.mic_gain` 1–8, jpeg quality ≤ 63); out-of-range or unparseable values are rejected and leave the setting unchanged. To commit the current config, use `CONFIG_SAVE <dest>` (serial) or `config_save` with a `dest` parameter (web), where `dest` is `nvs` or `sd` (case-insensitive):

- `CONFIG_SAVE nvs` — writes the live config to NVS flash (survives reboots, no SD card needed).
- `CONFIG_SAVE sd` — writes the live config to `/config.ini` on the SD card.

`CONFIG_GET` (serial) or `cmd=config` (web) returns the full live config as INI text for inspection or editing. The web UI's config editor loads the config via `cmd=config` and saves via `POST /update`, but on initial device bringup there is no web password yet, so web config access stays locked and config editing is managed via the serial interface (`CONFIG_GET`/`CONFIG_SAVE`); web config commands are refused (`{"result":"error","code":5}`) until a web password is configured. NVS saves are tagged with a schema version; a saved config written by an incompatible firmware version is discarded at boot (no migration) and defaults are used instead.

A failed persistence write leaves the live RAM config intact, so the running configuration is never corrupted by a storage failure.

### Example Config File

```ini
[wifi]
ssid=MyNetwork
password=MyPassword
static_ip=192.168.1.100
gateway=192.168.1.1
subnet_mask=255.255.255.0

[ap]
ssid=MicroCam
password=FallbackPass
ap_ip=192.168.4.1
ap_gateway=192.168.4.1
ap_subnet_mask=255.255.255.0

[web]
port=80
password=WebPass
max_streams=4

[camera]
resolution=VGA
jpeg_quality=10
grab_mode=0xFF
rotation=0

[video]
fps=10
resolution=VGA
clip_duration_s=60
max_file_size_mb=0
jpeg_quality=10
audio_enabled=false
mic_gain=2

[logging]
sd_enabled=after_set_time
serial_enabled=true
serial_min_level=info
sd_min_level=info
retention_days=7
```

### Sections and Keys

| Section | Key | Type | Default | Description |
|---------|-----|------|---------|-------------|
| `[wifi]` | `ssid` | string | `""` | WiFi network name (station mode) |
| `[wifi]` | `password` | string | `""` | WiFi network password |
| `[wifi]` | `static_ip` | string | `""` | Static IP address for station mode |
| `[wifi]` | `gateway` | string | `""` | Gateway address (required with static_ip) |
| `[wifi]` | `subnet_mask` | string | `""` | Subnet mask (required with static_ip) |
| `[ap]` | `ssid` | string | `MicroCam` | Access point name (fallback) |
| `[ap]` | `password` | string | `""` | Access point password |
| `[ap]` | `ap_ip` | string | `""` | Static IP for AP mode (requires ap_gateway & ap_subnet_mask) |
| `[ap]` | `ap_gateway` | string | `""` | Gateway address for AP mode |
| `[ap]` | `ap_subnet_mask` | string | `""` | Subnet mask for AP mode |
| `[web]` | `port` | integer | `80` | HTTP server port (1–65535) |
| `[web]` | `password` | string | `""` | Web interface password |
| `[web]` | `max_streams` | integer | `4` | Max concurrent streams (1–4) |
| `[camera]` | `resolution` | string | `VGA` | Still photo resolution (see table below) |
| `[camera]` | `jpeg_quality` | integer | `10` | Still photo JPEG quality (0–63, lower = better) |
| `[camera]` | `grab_mode` | hex | `0xFF` | Camera grab mode (0x00 = when empty, 0x01 = latest, 0xFF = hardware-recommended default) |
| `[camera]` | `rotation` | integer | `0` | Image rotation in degrees (0 or 180; applied via sensor hmirror/vflip registers at camera init; 90/270 not supported) |
| `[video]` | `fps` | integer | `10` | Video frame rate (1–30) |
| `[video]` | `resolution` | string | `VGA` | Frame resolution (see table below) |
| `[video]` | `clip_duration_s` | integer | `60` | Max clip length in seconds (0 = no limit) |
| `[video]` | `max_file_size_mb` | integer | `0` | Max clip file size in MB (0 = no limit) |
| `[video]` | `jpeg_quality` | integer | `10` | JPEG quality (0–63, lower = better) |
| `[video]` | `audio_enabled` | bool | `false` | Enable audio recording |
| `[video]` | `mic_gain` | integer | `2` | Microphone gain (1–8) |
| `[logging]` | `sd_enabled` | string | `after_set_time` | SD logging mode: `true` (always), `false` (disabled), `after_set_time` (enabled only after `set_time` command) |
| `[logging]` | `serial_enabled` | bool | `true` | Enable serial logging |
| `[logging]` | `serial_min_level` | string | `info` | Minimum log level for serial output |
| `[logging]` | `sd_min_level` | string | `info` | Minimum log level for SD card output |
| `[logging]` | `retention_days` | integer | `7` | Number of days to retain SD log files (1–255) |

Comments (`#` or `;`) and blank lines are ignored. Section and key names are case-insensitive. Unknown sections and keys are silently ignored.

Boolean keys accept `true` or `false` (case-insensitive); any other value is parsed as `false`.

String keys like `sd_enabled` accept `true`, `false`, and `after_set_time` (case-insensitive).

## SD Card & Degraded Mode

The SD card is optional at boot. If no card is present, or card initialization fails, the system still comes up fully in **streaming-only degraded mode**: camera, WiFi, web server, live streaming, and command dispatch all work; recording, photo capture, and SD card logging are unavailable (serial logging continues to work).

In degraded mode:

- `TAKE_PHOTO` and `START_RECORDING` fail with return code `SD_UNAVAILABLE` (`0x04`; serial `ERR 04`, web `{"result":"error","code":4}`). This check takes precedence over other error conditions.
- `RECORD_STOP` remains a successful no-op.
- `GET /status` (and serial `STATUS`) reports the latched card state in the additive `"sd": false` field.
- `CONFIG_SAVE sd` fails with `SD_UNAVAILABLE`; use `CONFIG_SAVE nvs` instead.
- The file routes (`/files`, `/download`, `/logs`, `/log`) return HTTP `503` while the card is unavailable; the rest of the web UI (status, controls, preview, config editor) stays functional.

### SD Logging Mode

The `sd_enabled` configuration option in the `[logging]` section controls when SD card logging is activated:

- `true` — Always enabled (SD log files are written immediately if the card is present)
- `false` — Completely disabled
- `after_set_time` — Enabled only after the real-time clock has been set via the `set_time` command (default). This avoids writing meaningless timestamped log files before the device knows the current time.

When `after_set_time` is active, the device logs a message indicating that SD logging is deferred until the time is set. Once `set_time` is called, SD logging begins automatically.

Late SD insertion is **not** detected at runtime. To recover from degraded mode, insert a card (if missing) and power-cycle the device.

### Adding a New Config Section

1. Add a sub-struct to `ConfigData` in `include/ini_parser.h` with default values:

   ```c++
   struct NewConfig
   {
       char some_key[CONFIG_FIELD_MAX_LENGTH]{};
       uint32_t some_number{42};
   };
   ```

2. Add the sub-struct as a member of `ConfigData`:

   ```c++
   NewConfig new_section;
   ```

3. In `src/ini_parser.cpp`, add a new `SECTION_XXX` case to the `Section` enum and its matching in the section header parsing block.

4. Add key-matching branches inside the `switch (current_section)` block for your new section.

5. Add the section header and key=value lines to `IniParser::serialize()`.

6. Add tests in `test/native/test_ini_parser/` for both parsing and round-trip serialization.

7. Use the new config field in application code via `config_service.config().new_section.some_key`.

### Adding a New Key to an Existing Section

Follow steps 4–7 above (skip the struct and enum additions).

### Resolution mapping

| Config String | `framesize_t` | Resolution |
|--------------|---------------|------------|
| `QQVGA` | `FRAMESIZE_QQVGA` | 160×120 |
| `QVGA` | `FRAMESIZE_QVGA` | 320×240 |
| `VGA` | `FRAMESIZE_VGA` | 640×480 |
| `SVGA` | `FRAMESIZE_SVGA` | 800×600 |
| `XGA` | `FRAMESIZE_XGA` | 1024×768 |
| `HD` | `FRAMESIZE_HD` | 1280×720 |
| `UXGA` | `FRAMESIZE_UXGA` | 1600×1200 |

---

## Web Interface

When WiFi is configured, the camera provides a web interface for status and control.

### Connection

1. If a STA (station) SSID is configured, the camera first attempts to connect to that network.
2. With no STA SSID (the default) or when STA connection fails within 30 seconds, it falls back to AP (access point) mode with the configured AP SSID/password (default: `MicroCam`, open network).
3. The IP address is logged at startup and available via the serial monitor.

### Endpoints

| Method | Path | Auth | Description |
|--------|------|------|-------------|
| GET | `/` | No | Embedded single-page web UI (status, controls, file browser, log viewer, config editor) |
| GET | `/status` | Yes | JSON status (recording and streaming activity, SD availability, frame count, file size, SD free space, today's photo/video counts, uptime in milliseconds) |
| GET | `/control?cmd=start` | Yes | Start video recording |
| GET | `/control?cmd=stop` | Yes | Stop video recording |
| GET | `/control?cmd=photo` | Yes | Capture a still photo (also while recording or streaming) |
| GET | `/control?cmd=stream_start` | Yes | Start live preview (capture loop on) |
| GET | `/control?cmd=stream_stop` | Yes | Stop live preview |
| GET | `/control?cmd=stream_list` | Yes | List connected stream clients (JSON) |
| GET | `/control?cmd=config` | Yes | Full live config as INI text (requires a configured web password) |
| GET | `/control?cmd=config_get_value&key=<section.key>` | Yes | Read a single config value (requires a configured web password) |
| GET | `/control?cmd=config_set_value&key=<section.key>&value=<val>` | Yes | Set a single config value in the live config (requires a configured web password) |
| GET | `/control?cmd=config_save&dest=nvs\|sd` | Yes | Persist the live config to NVS or the SD card (requires a configured web password) |
| GET | `/control?cmd=files&dir=<path>&start=<n>` | Yes | Directory listing JSON (requires a configured web password) |
| GET | `/control?cmd=version` | Yes | Firmware version as JSON: `{"version":"v0.0.1-<githash>"}` (same build string logged at boot) |
| GET | `/control?cmd=reboot` | Yes | Restart the device ~0.5 s after the response is delivered (requires a configured web password) |
| GET | `/files?dir=<path>&start=<n>` | Yes | Directory listing JSON — same handler as `cmd=files`, direct route (file-access policy below) |
| GET | `/download?path=<file>` | Yes | Download a file from the SD card (file-access policy below) |
| GET | `/logs?start=<n>` | Yes | Log directory listing JSON (file-access policy below) |
| GET | `/log?file=<YYYYMMDD.log>` | Yes | View a system log file — serves the last 16 KB as text (file-access policy below) |
| POST | `/update` | Yes | Bulk config update: urlencoded `config` field with INI text plus optional `dest=nvs\|sd` (default `nvs`) — overlays onto the live config and persists; INI text over 1280 bytes is rejected with `413` (config policy below) |
| GET | `/stream` | Yes | Live MJPEG stream (requires `stream_start`) |

### Live Streaming

The camera can serve a live MJPEG stream to web browsers. Streaming reuses the same capture loop as recording, so it works at the `[video]` resolution/framerate. To view the stream:

1. Start preview: `GET /control?cmd=stream_start`. Recording and preview are independent: either can run alone or both together, and stopping one does not disturb the other.
2. Open `http://<camera-ip>/stream` directly in a browser (Basic auth works on direct navigation; for the embedded `<img>` preview in the web UI, the browser's cached per-origin credentials are reused after the initial sign-in).

The number of concurrent viewers is limited by `max_streams` (1–4).

**Practical limits** (based on the ESP32-S3's ~20–30 Mbps real-world WiFi throughput, OV2640 encode time, and 8 MB PSRAM):

| Resolution | Recommended FPS | Concurrent viewers |
|-----------|-----------------|--------------------|
| VGA (640×480) | 10–15 | 2–4 (recommended default) |
| SVGA/XGA | low | 1–2 |
| HD/UXGA | n/a | effectively unavailable (PSRAM + encode-time constrained) |

### Authentication

If a web password is configured (in `config.ini` on the SD card or in a config saved to NVS), all control endpoints require HTTP Basic authentication. If no password is set, authentication is disabled (except config and file routes — see below).

Config commands (`cmd=config`, `cmd=config_save`) require a configured web password regardless: with no password set, the web interface refuses them with `UNAUTHORIZED` (web `{"result":"error","code":5}`), and config access stays on the serial interface (`CONFIG_GET`/`CONFIG_SAVE`), which is always available. `cmd=reboot` follows the same restricted-on-web policy: with no password set it is refused with `UNAUTHORIZED`, so an open access point can never be restarted remotely.

File routes (`/files`, `/download`, `/logs`, `/log`) also require a configured web password: with none set they are refused outright with `401`, since file access is never offered unauthenticated. With a password configured they use HTTP Basic authentication. Downloads are restricted to files inside the date directories and log files under `/logs` — `/config.ini` (which holds credentials) is never downloadable. The clip currently being recorded cannot be downloaded until it closes, and recordings may briefly dip below the configured FPS while a download is running (both share the SD card's chip-select).

## Video Recording

The firmware records MJPEG video to the SD card in standard AVI containers.

### How It Works

- Frames are captured from the camera at the configured FPS and written to an AVI file.
- Clips are automatically rotated when `clip_duration_s` or `max_file_size_mb` is reached.
- Files are stored in date-stamped directories: `/YYYYMMDD/YYYYMMDD_HHMMSS_<WxH>_<FPS>fps.avi`.
- Frames are written to SD incrementally, one AVI chunk per frame; at clip end the header is patched in place.

### Capabilities

Nominal recording capabilities for the OV2640 camera on the ESP32S3 board, per documentation on the [GitHub project page](https://github.com/limengdu/SeeedStudio-XIAO-ESP32S3-Sense-camera/blob/main/record_video_decorder/ESP32-CAM_MJPEG2SD/README.md) from SEEED studio. The encoding/storage should be able to keep up with the camera frame rate up to UXGA, where their tests showed a maximum of 10 fps.

| Frame Size | OV2640 camera max fps | Detection time ms |
|------------|-----------------------|-------------------|
| 96X96 | 50 | 15 | 
| QQVGA | 50 | 20 | 
| QCIF | 50 | 30 | 
| HQVGA | 50 | 40 | 
| 240X240 | 50 | 55 | 
| QVGA | 50 | 70 | 
| CIF | 50 | 110 | 
| HVGA | 50 | 130 |
| VGA | 25 | 80 | 
| SVGA | 25 | 120 |
| XGA | 12.5 | 80 |
| HD | 12.5 | 20 |
| SXGA | 12.5 | 00 |
| UXGA | 12.5 | 50 |

### LED Feedback During Recording

| State | LED Pattern | Meaning |
|-------|-------------|---------|
| Recording | Fast blink (200ms on / 200ms off) | Video being recorded |
| Stopping | Single pulse (500ms) | Clip boundary — old clip closing, new clip opening |
| Idle | Photo capture pulse or off | Not recording video |

## CLI Tool

The [`script/python/camera_cli.py`](script/python/camera_cli.py) script provides a command-line interface to the camera's web server via its HTTP endpoints.

Edit the `HOST`, `PORT`, and `PASSWORD` variables at the top of the script to match your device configuration.

```bash
# Get camera status (JSON)
uv run script/python/camera_cli.py status

# Send a control command
uv run script/python/camera_cli.py control [start | stop | photo | stream_start | stream_stop | stream_list]

# Get the full live config as INI text
uv run script/python/camera_cli.py config

# Read / set a single config value (live RAM config; not persisted)
uv run script/python/camera_cli.py config-get wifi.ssid
uv run script/python/camera_cli.py config-set wifi.ssid MyNetwork

# Persist the live config to NVS flash or the SD card
uv run script/python/camera_cli.py config-save nvs
uv run script/python/camera_cli.py config-save sd

# Set camera clock to current system time
uv run script/python/camera_cli.py set-time
```

Requires Python 3.13+ with [`uv`](https://docs.astral.sh/uv/). No external dependencies.

## Debugging

The project supports on-chip debugging via the ESP32-S3 built-in USB-JTAG bridge. See [docs/DEBUGGING.md](docs/DEBUGGING.md) for the full GDB workflow and PlatformIO debug configuration.

## VSCode IntelliSense for Tests

### The Problem

This project has two different `Arduino.h` implementations — the real ESP32 framework header (used for firmware builds) and a mock header (used for native unit tests). VSCode's C/C++ extension can only resolve one at a time, which causes incorrect IntelliSense: test files see ESP32-specific definitions instead of the mock stubs, producing false errors.

### The Solution

The workaround uses a per-file compile database approach:

1. **`.vscode/generate_compile_commands.py`** scans `test/native/` for test files and generates a `compile_commands.json` with compiler flags that route each test file to the mock `Arduino.h` (via `-Itest/mocks/native_mocks`).

2. **`.vscode/patch_intellisense.py`** adds a `compileCommands` reference to the PlatformIO configuration in `c_cpp_properties.json`, so the C/C++ extension picks up the per-file entries.

The C/C++ extension applies compile database entries on a **per-file basis**:
- Test files have entries in `compile_commands.json` → they use the mock `Arduino.h` flags.
- Source files (`src/`) have no entries → they fall back to the PlatformIO config's include paths → they use the real ESP32 `Arduino.h`.

This means both contexts get correct IntelliSense with zero manual switching.

### Running the Workaround

```bash
# One command to regenerate and run tests:
./script/fix_test_intellisense.sh

# Or run the steps individually:
uv run python .vscode/generate_compile_commands.py
uv run python .vscode/patch_intellisense.py
pio test -e native
```

Since PlatformIO regularly rewrites the `c_cpp_properties.json` (e.g. on project changes, updates to `platformio.ini`, etc.), **the `fix_test_intellisense.sh` script must be re-run as needed** to ensure the configurations are up to date.

## Documentation

- [System Design](docs/SYSTEM_DESIGN.md) — Architecture, HAL pattern, and component overview
- [Debugging](docs/DEBUGGING.md) — USB-JTAG setup, GDB workflow, and VSCode debug integration
