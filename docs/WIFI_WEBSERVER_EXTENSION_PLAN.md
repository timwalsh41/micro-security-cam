# WiFi and Web Server Extension Plan

## Background

This project currently has a clear split between application code, service-layer logic, and hardware abstraction layers. That structure is a good fit for adding network features without turning the firmware into a tangle of direct Arduino calls. The next extension adds three related capabilities:

1. Load WiFi configuration from a file on the SD card.
2. Connect to a configured WiFi network, or fall back to AP mode if that network is unavailable.
3. Host captured images over HTTP so the camera can be used as a lightweight image server.

The design goal is to preserve the current patterns: raw hardware access stays behind HAL interfaces, behavior and state live in small service/controller classes, and native tests continue to validate the logic with mocks instead of real hardware. The config file format should stay simple enough to parse on-device without bringing in a heavy dependency, so INI-style key/value sections are the right fit.

## Implementation Approach

The implementation should follow the existing architecture rather than layering new logic directly into `main.cpp`.

- SD file access should be generalized from write-only to full file read/write support.
- WiFi should be wrapped in a HAL and controlled by a service that owns connection and fallback behavior.
- The web server should live at the service layer and use a small HTTP API surface, ideally the built-in Arduino `WebServer` library.
- Config loading should happen early in startup, before WiFi and web services are initialized.
- Tests should be added for the new service logic and for the expanded SD mock behavior.

## Step-by-Step Plan

### 1. Generalize the SD controller

Rename `SdWriteController` to `SdController` and expand it so it can manage both reads and writes through the same chip-select timing protocol. The current controller already owns the important SD timing rules, so that logic should remain centralized rather than being duplicated for reads.

Key changes:

- Rename the controller type and update all call sites, tests, and docs.
- Expand `hal::SdCardHal` so it supports opening files for reading as well as writing.
- Keep the existing CS arbitration model intact so SD and LED sharing on the same pin continues to work correctly.
- Update the native mock SD HAL to track read operations, open modes, and file content fixtures.

Suggested API direction:

- `SdController::read_file(...)` for simple blocking reads used by config loading.
- `SdController::write_file(...)` retained for image capture writes.
- `hal::SdCardHal` methods should remain minimal and focused on file access, not higher-level parsing.

### 2. Add an INI config loader

Create a small `ConfigService` that reads a fixed config file, such as `/config.ini`, from the SD card during startup.

Why INI:

- It is human-readable and easy to edit.
- It can be parsed with a very small custom parser.
- It supports future growth through sections like `[wifi]`, `[ap]`, `[web]`, and `[camera]` without becoming complicated.
- It avoids the footprint and complexity of JSON/YAML libraries on an embedded target.

Implementation notes:

- Use flat `key=value` entries inside sections.
- Support comments and blank lines.
- Treat missing keys as defaults rather than fatal errors.
- Keep parsing single-pass and dependency-free.

Likely config shape:

```ini
[wifi]
ssid=MyNetwork
password=MyPassword

[ap]
ssid=ESP32_Camera
password=FallbackPass

[web]
port=80
password=WebPass

[camera]
capture_interval_ms=20000
```

### 3. Add a WiFi HAL and WiFi service

Add a `WiFiHal` interface that wraps Arduino WiFi calls, then build a `WiFiService` on top of it. The HAL keeps the hardware dependency narrow; the service owns startup policy and fallback behavior.

Proposed HAL surface:

- `WiFi.mode(...)`
- `WiFi.begin(...)`
- `WiFi.softAP(...)`
- `WiFi.disconnect(...)`
- `WiFi.status()`
- `WiFi.localIP()`
- `WiFi.softAPIP()`

Proposed service behavior:

- Load WiFi settings from the parsed config.
- Try station mode for a bounded timeout.
- If the configured network is unavailable, start AP mode using configured AP credentials.
- Expose status queries so startup and the web server can react to the selected mode.

The service should stay deterministic: one STA connection window, then AP fallback. If a later requirement needs reconnect-on-background behavior, that can be added as a controlled extension without changing the config format.

### 4. Add the web server service

Create a service-layer web component that owns route registration and response generation. It should not own WiFi state; it should simply assume network availability and serve content over HTTP. To preserve the option of swapping backends later, put a thin adapter interface between the service and the concrete Arduino web server implementation.

Library choice:

- Prefer the built-in Arduino `WebServer` library behind a small adapter interface.
- Avoid `ESPAsyncWebServer` unless the route set grows enough to justify the extra dependency and complexity.

Initial feature set:

- Serve a simple image index or listing page.
- Serve captured JPEG files directly from SD.
- Protect access with a password.

Implementation notes:

- Stream image files rather than loading them fully into memory.
- Keep route logic small and explicit.
- Use built-in auth support if possible so the implementation stays compact.
- If route handling needs test seams, wrap the server in a narrow adapter rather than pushing HTTP logic down into HALs.
- Keep the adapter narrow: lifecycle, route registration, request handling, auth checks, text responses, and file streaming are enough for the first version.

### 5. Rewire startup and composition

Update `main.cpp` so startup follows the new boot order:

1. Initialize the SD subsystem.
2. Load config from the SD card.
3. Start WiFi in STA mode.
4. Fall back to AP mode if the configured network is unavailable.
5. Start the web server after WiFi is ready.
6. Continue camera capture and image storage as the current loop already does.

The current `PeripheralManager` should remain the composition root for LED and shared-pin SD behavior. It does not need to become the owner of WiFi or HTTP unless that simplifies wiring later; those pieces fit more naturally as separate services at the application layer.

### 6. Expand tests and mocks

Add native tests first, because the current architecture is designed to make that the cheapest and fastest validation path.

Test areas:

- `SdController` read/write sequencing and pin arbitration.
- INI config parsing and default handling.
- WiFi STA success, STA timeout, and AP fallback.
- Web route registration, password protection, and SD-backed file serving.

Mock updates:

- Extend `MockSdCardHal` to support read operations and content fixtures.
- Add a `MockWiFiHal` for deterministic connection/fallback testing.
- Add a thin web-server adapter mock if the HTTP layer needs one for route assertions.

### 7. Update documentation

Refresh the docs once the code shape is clear.

Primary doc updates:

- `README.md` should explain the new startup flow, config file example, WiFi fallback behavior, how to access images, and the fact that the web layer is intentionally behind a small backend adapter.
- `docs/SYSTEM_DESIGN.md` should show the added WiFi, config, and web server layers, plus the generalized SD controller.

The documentation should reflect the actual patterns used in the codebase, not a separate theoretical design.

## Key Tradeoffs

- INI is the best balance of simplicity and maintainability for embedded config.
- Built-in `WebServer` is a better fit than a heavier async web server for the initial scope.
- A dedicated `WiFiHal` keeps Arduino dependencies isolated and mockable.
- Generalizing the SD controller is preferable to adding a separate read-only path, because the CS protocol is shared.
- Password protection should be applied from the start so the web server does not grow an insecure default.

## Suggested Library Choices

- Arduino `WebServer` for HTTP serving.
- Existing Arduino WiFi support wrapped behind a new HAL.
- No JSON/YAML library for config parsing unless the config schema grows much larger than expected.

## Expected Outcome

After this work, the camera should be able to boot from an SD-backed config file, join a known WiFi network when available, expose an AP when it is not, and host captured images over a protected browser interface. The implementation should remain aligned with the current project architecture and remain testable on the native host.