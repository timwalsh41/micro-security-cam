# ESP32 Photo Camera

A still photo and MJPEG video camera for the **Seeed XIAO ESP32S3 Sense**, capturing JPEG images and recording video to a MicroSD card. An LED provides visual status feedback.

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

- [PlatformIO CLI](https://docs.platformio.org/en/latest/core/installation.html) (`pio` command)
- Python 3 with [uv](https://docs.astral.sh/uv/) (for the IntelliSense workaround scripts)
- Activate the PlatformIO environment before running commands:
  ```bash
  source ~/.platformio/penv/bin/activate
  ```

## Build & Flash

```bash
# Build firmware for the ESP32 target
pio run -e seeed_xiao_esp32s3

# Flash to the connected board
pio run -e seeed_xiao_esp32s3 --target upload

# Open serial monitor (115200 baud)
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

## Configuration

The firmware reads an INI-style config file (`/config.ini`) from the SD card at startup. If the file is missing or empty, sensible defaults are used.

### Example Config File

```ini
[wifi]
ssid=MyNetwork
password=MyPassword
static_ip=192.168.1.100
gateway=192.168.1.1
subnet_mask=255.255.255.0

[ap]
ssid=ESP32-Cam
password=FallbackPass
ap_ip=192.168.4.1
ap_gateway=192.168.4.1
ap_subnet_mask=255.255.255.0

[web]
port=80
password=WebPass
max_streams=4

[camera]
capture_interval_ms=20000
resolution=VGA
jpeg_quality=10
grab_mode=0xFF

[video]
fps=10
resolution=VGA
clip_duration_s=60
max_file_size_mb=0
jpeg_quality=10
audio_enabled=false
mic_gain=2
```

### Sections and Keys

| Section | Key | Type | Default | Description |
|---------|-----|------|---------|-------------|
| `[wifi]` | `ssid` | string | `""` | WiFi network name (station mode) |
| `[wifi]` | `password` | string | `""` | WiFi network password |
| `[wifi]` | `static_ip` | string | `""` | Static IP address for station mode |
| `[wifi]` | `gateway` | string | `""` | Gateway address (required with static_ip) |
| `[wifi]` | `subnet_mask` | string | `""` | Subnet mask (required with static_ip) |
| `[ap]` | `ssid` | string | `""` | Access point name (fallback) |
| `[ap]` | `password` | string | `""` | Access point password |
| `[ap]` | `ap_ip` | string | `""` | Static IP for AP mode (requires ap_gateway & ap_subnet_mask) |
| `[ap]` | `ap_gateway` | string | `""` | Gateway address for AP mode |
| `[ap]` | `ap_subnet_mask` | string | `""` | Subnet mask for AP mode |
| `[web]` | `port` | integer | `80` | HTTP server port (1–65535) |
| `[web]` | `password` | string | `""` | Web interface password |
| `[web]` | `max_streams` | integer | `4` | Max concurrent streams (1–4) |
| `[camera]` | `capture_interval_ms` | integer | `20000` | Time between captures in ms |
| `[camera]` | `resolution` | string | `VGA` | Still photo resolution (see table below) |
| `[camera]` | `jpeg_quality` | integer | `10` | Still photo JPEG quality (0–63, lower = better) |
| `[camera]` | `grab_mode` | hex | `0xFF` | Camera grab mode (0x00 = when empty, 0x01 = latest, 0xFF = hardware-recommended default) |
| `[video]` | `fps` | integer | `10` | Video frame rate (1–30) |
| `[video]` | `resolution` | string | `VGA` | Frame resolution (see table below) |
| `[video]` | `clip_duration_s` | integer | `60` | Max clip length in seconds (0 = no limit) |
| `[video]` | `max_file_size_mb` | integer | `0` | Max clip file size in MB (0 = no limit) |
| `[video]` | `jpeg_quality` | integer | `10` | JPEG quality (0–63, lower = better) |
| `[video]` | `audio_enabled` | bool | `false` | Enable audio recording |
| `[video]` | `mic_gain` | integer | `2` | Microphone gain (1–8) |

Comments (`#` or `;`) and blank lines are ignored. Section and key names are case-insensitive. Unknown sections and keys are silently ignored.

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

1. The camera first attempts to connect to the configured STA (station) WiFi network.
2. If STA connection fails within 30 seconds, it falls back to AP (access point) mode with the configured AP SSID/password.
3. The IP address is logged at startup and available via the serial monitor.

### Endpoints

| Method | Path | Auth | Description |
|--------|------|------|-------------|
| GET | `/` | No | Built-in HTML status page |
| GET | `/status` | Yes | JSON status (recording state, frame count, file size) |
| GET | `/control?cmd=start` | Yes | Start video recording |
| GET | `/control?cmd=stop` | Yes | Stop video recording |
| GET | `/control?cmd=photo` | Yes | Capture a still photo |

### Authentication

If a web password is configured in `config.ini`, all control endpoints require HTTP Basic authentication. If no password is set, authentication is disabled.

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

## Video Recording

The firmware records MJPEG video to the SD card in standard AVI containers.

### How It Works

- Frames are captured from the camera at the configured FPS and written to an AVI file.
- Clips are automatically rotated when `clip_duration_s` or `max_file_size_mb` is reached.
- Files are stored in date-stamped directories: `/YYYYMMDD/YYYYMMDD_HHMMSS_<WxH>_<FPS>fps.avi`.
- The complete AVI is buffered in RAM and written to SD in a single flush at clip end, ensuring valid AVI files.

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
uv run script/python/camera_cli.py control [start | stop | photo]

# Set camera clock to current system time
uv run script/python/camera_cli.py set-time
```

Requires Python 3.13+ with [`uv`](https://docs.astral.sh/uv/). No external dependencies.

## Documentation

- [System Design](docs/SYSTEM_DESIGN.md) — Architecture, HAL pattern, and component overview
- [Debugging](docs/DEBUGGING.md) — USB-JTAG setup, GDB workflow, and VSCode debug integration
