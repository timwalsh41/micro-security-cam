# ESP32 Photo Camera

A still photo camera demo for the **Seeed XIAO ESP32S3 Sense**, capturing JPEG images at a configurable interval and saving them to a MicroSD card. An LED provides visual status feedback.

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

## Debugging

The project supports on-chip debugging via the ESP32-S3 built-in USB-JTAG bridge. See [docs/Debugging.md](docs/Debugging.md) for the full GDB workflow and PlatformIO debug configuration.

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
- [Debugging](docs/Debugging.md) — USB-JTAG setup, GDB workflow, and VSCode debug integration
