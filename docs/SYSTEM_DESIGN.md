# System Design

This document describes the architecture and key components of the ESP32 Photo Camera project.

## Overview

This project is a still photo camera built on the **Seeed XIAO ESP32S3 Sense**, which includes an onboard OV2640 camera and MicroSD card slot. The firmware captures JPEG images at a configurable interval and saves them to the SD card, while an LED provides visual status feedback.

The design follows a layered architecture that separates hardware access from application logic, enabling thorough unit testing on a native host machine without any hardware dependencies.

## Architecture

```
┌─────────────────────────────────────────────────┐
│            Application  (main.cpp)              │
│  setup() / loop() — wires components, runs      │
│  the capture-and-save cycle                     │
└─────────────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────┐
│           Service / Controller Layer            │
│  CameraController       LedStateMachine         │
│  SdWriteController      PinArbitrator           │
│  TimeService            LoggingService          │
│  PeripheralManager  (facade / composition root) │
└─────────────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────┐
│        Hardware Abstraction Layer (HAL)         │
│  CameraHal       SdCardHal      LogBackend      │
│  GpioHal         TimeHal                         │
└─────────────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────┐
│   Arduino Libraries & ESP32 Hardware            │
│  esp_camera   SD.h   GPIO   Serial              │
└─────────────────────────────────────────────────┘
```

### Key Design Principles

- **Dependency Injection**: All service-layer classes accept HAL interfaces through constructor parameters, making them decoupled from any specific hardware implementation.
- **Single Responsibility**: Each class owns exactly one concern — the LED state machine handles only LED timing, the SD write controller handles only the CS protocol, etc.
- **Tick-Driven State Machines**: LED and SD card control are implemented as non-blocking state machines advanced by periodic `tick()` calls from the main loop.
- **Testability via Abstractions**: Abstract HAL interfaces allow full unit testing of service-layer logic on a native host (macOS/Linux) using mock implementations, with no ESP32 hardware required.

---

## Hardware Abstraction Layer (HAL)

The HAL provides abstract interfaces that wrap Arduino/ESP32 hardware calls. Each interface has a concrete implementation for the ESP32 target and a mock implementation for native tests.

### HAL Interfaces

| Interface | File | Responsibility |
|-----------|------|----------------|
| `CameraHal` | `include/hal/camera_hal.h` | Camera initialization, frame capture/release, PSRAM detection. Returns `CameraInitResult`, `CameraFrame`. |
| `SdCardHal` | `include/hal/sd_card_hal.h` | SD card initialization, file open/write/close. Returns `SdInitResult`. File operations contract: must only be called when CS is asserted. |
| `GpioHal` | `include/hal/gpio_hal.h` | Thin forwarding wrappers for `pinMode()` and `digitalWrite()`. No logic — pure indirection. |
| `TimeHal` | `include/hal/time_hal.h` | Forwarding functions for Arduino `millis()`, `delay()`, and microsecond delays. Defines the `millis_t` type alias. |
| `LogBackend` | `include/hal/log_backend.h` | Abstract interface for log output destinations. Receives fully formatted log lines from `LoggingService`. |

### Concrete ESP32 Implementations

All concrete HAL implementations live in `src/hal/` and directly call Arduino/ESP32 library functions:

| Implementation | Wraps |
|---------------|-------|
| `CameraHalImpl` | `esp_camera_*` functions, `esp_camera_init()`, `esp_camera_fb_get()`/`esp_camera_fb_return()` |
| `SdCardHalImpl` | Arduino `SD` library (`SD.begin()`, `File` open/write/close) |
| `GpioHal` | Arduino `pinMode()`, `digitalWrite()` |
| `SerialLogBackend` | Arduino `Serial.println()` |

### Mock Implementations (Native Tests)

Mock HAL classes live in `test/mocks/native_mocks/` and provide configurable return values plus call tracking:

| Mock | Tracks | Configurable |
|------|--------|-------------|
| `MockCameraHal` | `init_call_count`, `capture_call_count`, `release_call_count`, `deinit_call_count` | `init_result`, `capture_result`, `initialized_state`, `psram_available` |
| `MockSdCardHal` | `init_call_count`, `open_call_count`, `write_call_count`, `close_call_count` | `init_result`, `open_result`, `write_result` |
| `MockLogBackend` | `write_call_count`, `last_message`, `messages` (vector of all captured lines) | N/A — captures output for assertion |
| Mock Arduino | N/A | Stub `String`, `HardwareSerial`, `millis()`, `delay()` for native compilation |

---

## Service Layer Classes

### TimeService

**File**: `include/time_service.h`

Abstract interface for time acquisition. Provides `now()` (current milliseconds), `delay_ms()`, and `delay_us()`, plus a static `elapsed_ms()` utility that correctly handles 32-bit millisecond rollover using unsigned arithmetic.

`RealTimeService` delegates to the `TimeHal` functions (`hal::get_millis()`, `hal::delay_ms()`). A mock implementation returns a programmable value for deterministic testing.

### LedStateMachine

**File**: `include/led_state_machine.h`

A pure state machine with no hardware side effects. Operates in four modes:

- **OFF** — LED permanently off.
- **ON** — LED permanently on.
- **PULSE_ON** — LED on for a fixed duration, then off.
- **BLINK** — LED alternates on/off at configurable intervals.

The `tick()` method advances the state machine and returns the desired LED state (`true` = on, `false` = off). Callers are responsible for applying the result to a GPIO pin. Uses an injected `TimeService` for all timing, enabling deterministic testing with mock time.

### SdWriteController

**File**: `include/sd_write_controller.h`

Manages the SD card write lifecycle with a chip-select (CS) timing protocol. Owns a state machine that sequences CS assertion and de-assertion around file I/O:

```
IDLE → PRE_DEACTIVATE_CS → ACTIVATE_CS → WRITING → STOP_WRITE → POST_DEACTIVATE_CS → IDLE
```

The `tick()` method returns an `SdSignal` (`IDLE`, `CS_ACTIVE`, or `CS_INACTIVE`) that describes what the shared pin should do. When `IDLE`, the LED's request wins. When `CS_ACTIVE` or `CS_INACTIVE`, the SD card overrides the LED.

Provides both non-blocking (`start_write()` + repeated `tick()`) and blocking (`write_file()` with timeout) write APIs. Hardware I/O is delegated to an injected `SdCardHal`.

### PinArbitrator

**File**: `include/pin_arbitrator.h`

A static utility class that resolves conflicts when the LED and SD chip-select share the same physical pin (GPIO 21 on this board). Both LED-on and CS-active are active-LOW.

Arbitration rules:
- `SdSignal::IDLE` → LED's request is granted.
- `SdSignal::CS_ACTIVE` → Pin forced LOW (CS active).
- `SdSignal::CS_INACTIVE` → Pin forced HIGH (CS inactive).

### LoggingService

**File**: `include/logging_service.h`

Syslog-style structured logging with timestamp, subsystem name, and severity level. Output format:

```
[00001234] [CAMERA] [INFO ] Photo saved successfully
```

Provides convenience methods (`debug()`, `info()`, `warn()`, `error()`) with printf-style variadic formatting. Delegates output to a pluggable `LogBackend`.

---

## CameraController

**Files**: `include/camera_controller.h`, `src/camera_controller.cpp`

### Responsibility

Manages the camera lifecycle: initialization, frame capture, and resource release.

### Dependencies

- `CameraHal &` — abstract camera hardware interface (injected via constructor).
- `CameraConfig` — configuration parameters with defaults tuned for the XIAO ESP32S3 (PSRAM-aware quality and frame buffer settings).

### Lifecycle

```
init() → capture_frame() → get_frame() → release_frame()
```

The controller tracks state via `m_initialized` and `m_frame_acquired` flags. Calling `capture_frame()` without first releasing the previous frame is handled gracefully.

### Configuration

During `init()`, the controller queries PSRAM availability via the HAL and selects appropriate quality and frame buffer settings from the `CameraConfig`. Two constructors allow either default or custom configuration.

### Testability

Fully testable on native via `MockCameraHal`, which provides configurable return values and call tracking for every HAL method.

---

## PeripheralManager

**Files**: `include/peripheral_manager.h`, `src/peripheral_manager.cpp`

### Responsibility

Acts as the **composition root and facade** for all peripheral sub-components. Wires together the LED, SD card, and pin arbitration logic. Contains no state machine logic of its own — all behavior is delegated.

### Owned Components

- `RealTimeService` — time source for all sub-components.
- `LedStateMachine` — pure LED state machine logic.
- `SdCardHalImpl` — SD card HAL implementation (CS on GPIO 21).
- `SdWriteController` — SD write controller with CS timing protocol.

### Key Methods

- **`init()`** — Configures GPIO, initializes the LED state machine to OFF.
- **`init_sd_card()`** — Initializes SD card hardware via the SdWriteController.
- **`tick()`** — The central method, called periodically (~5 ms) from the main loop:
  1. Ticks the `LedStateMachine` → gets desired LED state.
  2. Ticks the `SdWriteController` → gets the `SdSignal`.
  3. Feeds both into `PinArbitrator::resolve()` → gets the final pin state.
  4. Applies the result to GPIO 21 via `GpioHal`.
- **LED convenience**: `turn_led_on()`, `turn_led_off()`, `pulse_led(ms)`, `blink(on_ms, off_ms)` — delegate to `LedStateMachine::set_mode()`.
- **SD convenience**: `write_file(path, data, len, timeout_ms)` — delegates to `SdController::write_file()`.

### Shared Pin Protocol

GPIO 21 is shared between the LED (active LOW) and the SD card chip-select (active LOW). The `tick()` method resolves conflicts through the `PinArbitrator` every cycle, ensuring correct behavior whether the SD card is idle or actively sequencing a transaction.

---

## Application Layer

**File**: `src/main.cpp`

The `setup()` function initializes Serial, the PeripheralManager, camera, and SD card in sequence. The camera initialization is PSRAM-aware. An LED blink pattern provides visual feedback during startup.

The `loop()` function runs on a 5 ms tick for the PeripheralManager and a configurable interval (default 20 seconds) for camera captures. Each capture cycle:

1. `camera_ctrl.capture_frame()` — acquire a frame from the camera.
2. `camera_ctrl.get_frame()` — access the JPEG data.
3. `peripheral_manager.write_file()` — write to SD with a 1-second timeout.
4. `camera_ctrl.release_frame()` — release the buffer back to the camera.

---

## Testing Strategy

### Environments

| Environment | Platform | Purpose | Test Filter |
|------------|----------|---------|-------------|
| `native` | Host (macOS/Linux) | Unit tests for all pure logic | `native/*` |
| `seeed_xiao_esp32s3_test` | ESP32 | Hardware integration tests | `hardware_tests` |

### Test Structure

Tests live under `test/` in environment-specific subfolders:

```
test/
  native/                           ← Native unit tests (Unity framework)
    test_camera_controller/
    test_led_state_machine/
    test_logging_service/
    test_pin_arbitrator/
    test_sd_write_controller/
    test_time_service/
  hardware/                         ← ESP32 integration tests
  mocks/
    native_mocks/                   ← Mock HAL classes + mock Arduino
    embedded_mocks/                 ← Mocks for ESP32 test env
    test_utils/                     ← Shared test utilities
```

Each test suite is a subfolder with a `test_` prefix containing one or more implementation files, but always a single `main()` function that gathers and runs all tests. Tests use the Unity framework.

### Running Tests

```bash
source ~/.platformio/penv/bin/activate

# Run all native unit tests
pio test -e native

# Run a specific test suite
pio test -e native -f native/test_camera_controller

# Run hardware integration tests (requires board connected)
pio test -e seeed_xiao_esp32s3_test
```

### Testing Guidelines

- **Logical/control-level components** (state machines, arbitrators, time calculations) → test in the `native` environment with mock HALs.
- **Components that interface directly with hardware** → test in the `seeed_xiao_esp32s3_test` environment.
- **Mocks and shared test utilities** → place in the appropriate subfolder under `test/mocks/`.

---

## PlatformIO Environments

| Environment | Platform | Purpose | Key Flags |
|------------|----------|---------|-----------|
| `seeed_xiao_esp32s3` | espressif32 | Production firmware build & flash | `-DCAMERA_MODEL_XIAO_ESP32S3` |
| `seeed_xiao_esp32s3_debug` | espressif32 | Debug build with USB-JTAG | Adds `-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1 -DDEBUG_MODE` |
| `seeed_xiao_esp32s3_test` | espressif32 | Hardware integration tests | Extends target env; `lib_extra_dirs = test/mocks` |
| `native` | native | Host unit tests | `-DUNIT_TESTING -I include -I test` |
