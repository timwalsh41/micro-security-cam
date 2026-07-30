# Slice 3 — FreeRTOS + Live MJPEG Streaming

## Overview

Transition from cooperative polling to FreeRTOS tasks. Add live MJPEG streaming to web browsers. Full plan covering 3 sub-phases.

## Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Streaming server | `AsyncWebServer` replaces Arduino `WebServer` entirely | Concurrent connections, no `handle_client` polling, no static trampolines |
| `AviWriter` I/O | Format-only (no I/O dependency) | `VideoController` writes formatted bytes to SD directly. Clean separation. |
| `capture_photo` during recording | Command fails with `INVALID_STATE` | Instruct user to "stop recording first" — simple and predictable |
| `capture_photo` when idle | Direct I/O from calling context (main loop) | `capture_task` doesn't exist when idle; no concurrency concern |
| `SdController` access | Extracted to composition root, injected into both `VideoController` and remaining setup consumers | Clean ownership, both contexts access same instance |
| `PeripheralManager` | Dissolved — `LedStateMachine`, `SdController`, `GpioHal` at composition-root scope | Main loop (LED) and `capture_task` (CS) both need GPIO access |
| LED arbitration | Main loop checks `sd_active` mutex (`xSemaphoreTake` with 0 timeout) before writing GPIO 21 | FreeRTOS mutex with priority inheritance for dual-core safety |
| Camera `fb_count` | 2 (for DMA pipelining) | Frame capture in one DMA buffer while previous frame writes to SD |

## Phase Dependency

```
Phase 3A (AsyncWebServer) ──► Phase 3C (Streaming)
                                    ↑
Phase 3B (FreeRTOS + AVI) ──────────┘
```

Phases 3A and 3B are independent and can be implemented in either order or in parallel. Phase 3C requires both 3A (HTTP endpoint) and 3B (`capture_task` frame delivery).

---

## Phase 3A — AsyncWebServer Migration

Goal: Replace Arduino `WebServer` with `ESPAsyncWebServer`. No functional change visible to the user — same status page, same control commands. Foundation laid for streaming.

### What changes

#### `platformio.ini`
- Add `me-no-dev/ESP Async WebServer@^1.2.3` and `me-no-dev/AsyncTCP@^1.1.1` to `lib_deps` for `seeed_xiao_esp32s3` environment
- Native environment uses mock — no library dependency needed

#### `include/hal/web_server_hal.h` — Refactor interface
- Remove `handle_client()` — no longer needed
- Change `on(uri, method, handler)` to accept `std::function<void(RequestRef&)>` — `AsyncWebServer` supports captures natively, no more plain function pointer requirement
- Remove `uri()`, `method()`, `arg()`, `header()`, `auth_header()` — accessed through the request object passed to handler, not via singleton
- Keep `begin()`, `stop()`, `send()` variants
- Remove `send_header()` and `send_content()` (these were no-op stubs)
- Define `RequestRef` as a thin abstraction over `AsyncWebServerRequest*`
- Add `create_stream_writer()` to `RequestRef` — returns a `StreamWriter*` for the request's connection. `WebServerHalImpl` returns an `AsyncWebStreamWriter`; mock returns a `MockStreamWriter`.

#### `include/hal/web_server_hal_impl.h` — New implementation
- Wraps `AsyncWebServer`
- `on()` registers routes with captures
- `send()` delegates to `request->send()`
- No static trampolines, no `MAX_ROUTES` constraint
- No `handle_client()` method

#### `include/web_server_service.h` — Route registration update
- Remove static instance pointer and trampoline methods
- `init()` registers routes using lambdas that capture `this`
- `handle_index()` called directly by lambda

#### `include/web_command_transport.h` — Eliminate static trampolines
- Remove static instance pointer and static trampoline methods
- `register_routes()` registers `/status` and `/control` using lambdas with captures
- Still references `CommandHandler& m_handler` for dispatch

#### `test/mocks/native_mocks/mock_web_server_hal.h` — Update mock
- Align interface with new `WebServerHal`
- Record registered routes with captured lambdas
- Provide `simulate_request(uri, method, ...)` to invoke captured handler for testing

#### `src/main.cpp` — Remove `handle_client()` from loop()
- `web_server_service.handle_client()` removed
- Main loop simplifies

### Files modified (11)
1. `platformio.ini` — add `AsyncWebServer` + `AsyncTCP` lib_deps
2. `include/hal/web_server_hal.h` — major refactor
3. `include/hal/web_server_hal_impl.h` — rewrite
4. `src/hal/web_server_hal_impl.cpp` — rewrite
5. `include/web_server_service.h` — remove trampolines
6. `src/web_server_service.cpp` — update route registration
7. `include/web_command_transport.h` — remove trampolines
8. `src/web_command_transport.cpp` — update route registration
9. `test/mocks/native_mocks/mock_web_server_hal.h` — update interface
10. `test/native/test_web_server_service/` — update
11. `test/native/test_web_command_transport/` — update
12. `src/main.cpp` — remove `handle_client()` from loop()

### Documentation updates

#### `docs/SYSTEM_DESIGN.md`
- Update `WebServerHal` interface documentation — remove `handle_client()`, `uri()`, `method()`, `arg()`, `header()`, `auth_header()`, `send_header()`, `send_content()`. Document new `RequestRef` type and `std::function`-based `on()` signature.
- Update `WebServerHalImpl` section to reflect `AsyncWebServer` wrapper instead of Arduino `WebServer`.
- Update `WebServerService` section — remove static trampoline documentation.
- Update `WebCommandTransport` section — remove static trampoline documentation.
- Update architecture diagram notes to remove `handle_client()` from the `loop()` description.
- Remove `MAX_ROUTES` mention from `WebServerHalImpl` documentation.

#### `README.md`
- The endpoints table is unchanged (same routes, same behavior) — no update needed for Phase 3A.
- No user-facing changes in this phase, so `README.md` remains as-is.

### Tests
- `test_web_server_service/`: Route registration, auth, index handler via mock `simulate_request`
- `test_web_command_transport/`: Dispatch via simulated request, response format
- All existing native tests continue passing unchanged

### Verification
- Browser to camera IP → status page loads (identical to before)
- `/status` and `/control` commands work identically
- No `handle_client()` in loop()
- No static trampolines remain

---

## Phase 3B — VideoController Refactor: FreeRTOS + Incremental AVI + Photo

Goal: Replace `VideoController::tick()` with FreeRTOS `capture_task`. Per-frame SD writes with RAM index. `TAKE_PHOTO` saves real `.jpg` files. Clip rotation ~50ms versus 4-8s previously.

### What changes

#### New files
1. `include/hal/timer_hal.h` — Timer interface
2. `src/hal/timer_hal_impl.h` — ESP32 header
3. `src/hal/timer_hal_impl.cpp` — ESP32 hardware timer impl
4. `test/mocks/native_mocks/mock_timer_hal.h` — Mock for native tests

##### `include/hal/timer_hal.h`
- Interface: `init(interval_us, isr)`, `set_frequency(interval_us)`, `start()`, `stop()`
- `TimerHalImpl` wraps ESP32-S3 `timer_group` APIs, ISR via `timer_isr_callback_add()`
- `MockTimerHal` provides `trigger()` method that calls registered ISR synchronously

#### `include/sd_controller.h` — Add synchronous methods
- `bool write_sync(path, data, len, mode)` — blocking CS sequence
- `bool read_sync(path, buffer, buffer_size, bytes_read)` — synchronous read
- `bool create_dir_sync(path)` — synchronous mkdir
- `void set_mutex(SemaphoreHandle_t mutex)` — inject `sd_active` mutex
- Existing `tick()` + non-blocking methods remain for any polling consumers

#### `include/video_controller.h` — Major refactor
- Constructor changes from `(CameraController&, PeripheralManager&, TimeService&, LoggingService&)` to `(CameraController&, SdController&, TimeService&, LoggingService&, TimerHal&)`
- Public API preserved: `start_recording()`, `stop_recording()`, `state()`, `is_recording()`, `frames_recorded()`, `file_size()`
- NEW: `capture_photo(path)` — synchronous photo, returns false when recording
- NEW: `set_stream_service(svc)` — optional `StreamService` reference (Phase 3C)

#### `src/video_controller.cpp` — Implementation changes
- `capture_task_entry()` — FreeRTOS task entry point
- `capture_task` loop: `ulTaskNotifyTake` → `on_frame_capture()`
- `frame_isr()` — `IRAM_ATTR`, `vTaskNotifyGiveFromISR()` on `m_capture_task_handle`
- `on_frame_capture()` flow: capture frame → AVI chunk → SD write sync → optional stream delivery → release frame → check clip limits → auto-rotate
- `start_recording()`: create mutex → open clip → create `capture_task` → start timer ISR
- `stop_recording()`: set atomic stop flag → `capture_task` closes clip → wait on done semaphore
- `capture_photo()`: if not `IDLE` → return false; else capture + SD write sync + return true
- Clip close: stop timer → close AVI (RAM index) → write index to SD → `seek(0)` → overwrite header

#### `include/peripheral_manager.h` — Dissolved
- `LedStateMachine`, `SdController`, `GpioHal` move to composition root
- `PeripheralManager` removed entirely (or gutted to empty if any setup code references it)

#### `src/main.cpp` — New composition root
- Globals: `gpio_hal`, `led_state_machine`, `sd_controller`, `sd_active_mutex`, `timer_hal`, `video_controller`
- `setup()`: `sd_controller.init_card()` directly, config via `sd_controller.read_sync()`, create mutex
- `loop()`: no `peripheral_manager.tick()`, no `video_controller.tick()`
- `loop()` LED: `led_state_machine.tick()` → `xSemaphoreTake(mutex, 0)` → `gpio_hal.write()` → `xSemaphoreGive(mutex)`

#### `include/system_orchestrator.h` — `handle_take_photo` updated
- Calls `m_video.capture_photo(path)` instead of capture+discard
- Path from `TimeService::unix_now()` or `now()` fallback: `/photos/YYYYMMDD_HHMMSS.jpg`
- Returns `INVALID_STATE` if recording

#### `include/avi_writer.h` — No interface change
- Keep format-only. `VideoController` tracks chunk file positions in `m_chunk_offsets[]`.
- At clip close: `avi_writer.close()` → get `patched_header()` + `index()` → write index to SD → `seek(0)` → overwrite header

#### `include/camera_controller.h` — `fb_count=2`
- `CameraController::init()` ensures `hal_config.fb_count = 2` for video recording mode

### Files modified (12+)
1. `include/hal/timer_hal.h` — NEW
2. `src/hal/timer_hal_impl.h` — NEW
3. `src/hal/timer_hal_impl.cpp` — NEW
4. `test/mocks/native_mocks/mock_timer_hal.h` — NEW
5. `include/sd_controller.h` — add sync methods + `set_mutex`
6. `src/sd_controller.cpp` — implement sync methods
7. `include/video_controller.h` — major refactor
8. `src/video_controller.cpp` — rewrite
9. `include/peripheral_manager.h` — remove/dissolve
10. `src/peripheral_manager.cpp` — remove/dissolve
11. `include/system_orchestrator.h` — `capture_photo` plumbing
12. `src/system_orchestrator.cpp` — implement `capture_photo`
13. `src/main.cpp` — new composition root
14. `test/native/test_video_controller/` — rewrite
15. `test/native/test_system_orchestrator/` — update `TAKE_PHOTO` test

### Documentation updates

#### `docs/SYSTEM_DESIGN.md`
- Remove `PeripheralManager` section entirely (or replace with migration note).
- Add `TimerHal` to the HAL interfaces table.
- Update `VideoController` section — document FreeRTOS `capture_task`, `frame_isr()`, task notification pattern, per-frame SD I/O, incremental AVI model, and `capture_photo()` API.
- Update `SdController` section — document synchronous methods (`write_sync()`, `read_sync()`, `create_dir_sync()`, `set_mutex()`) and the `sd_active` mutex injection.
- Update `CameraController` section — document `fb_count=2` for recording mode.
- Update architecture diagram — remove `PeripheralManager` and `PinArbitrator`, add `TimerHal`, show `SdController` and `LedStateMachine` at composition-root scope.
- Update `main.cpp` / composition root description — new globals, simplified `loop()`, LED arbitration via `sd_active` mutex.
- Update `SystemOrchestrator` section — document `capture_photo` delegation and `INVALID_STATE` when recording.
- Add a PlantUML sequence diagram for the video recording process, covering the spawning of the `capture_task`, the dual frame buffer ping-ponging, and the incremental write process
- Update the "Write Pattern" section under `ConfigService` if it references `PeripheralManager`.
- Remove `PinArbitrator` documentation (no longer needed — arbitration happens via mutex directly).

#### `README.md`
- Update "Video Recording / How It Works" — change from "complete AVI buffered in RAM, written at clip end" to "per-frame writes with RAM index, header patched at clip close".
- The LED feedback table remains accurate (no change to LED patterns).
- Remove references to `PeripheralManager` if any exist.

### Tests — VideoController (native)
New `TestFixture` with `SdController` + `MockTimerHal`:
- Initial state `IDLE`, not recording, frames=0, file_size=0
- `start_recording()` → `RECORDING` state, creates task (mock tracking)
- `on_frame_capture()` → frame captured, AVI chunk formatted, SD written
- Incremental AVI: verify per-frame SD writes via `sd_hal` mock call count
- Clip rotation at duration limit: verify close + reopen
- `capture_photo()` when idle → JPEG bytes on SD
- `capture_photo()` when recording → returns false
- `stop_recording()` → clip closed, index + header patched on SD
- Frame drops when write exceeds frame period
- LED mutex interaction: main loop can read mutex state

### Verification (hardware)
- Recording FPS matches configured rate
- Files playable in VLC with correct dimensions/framerate
- Clip rotation gap ~50ms (not 4-8s)
- `TAKE_PHOTO` saves `.jpg`, path in response
- `TAKE_PHOTO` during recording → error response
- 10+ consecutive clips, memory stable
- LED flashes during recording, pauses between SD writes

---

## Phase 3C — Live MJPEG Streaming

Goal: Live camera view in browser. Recording + streaming simultaneously. Up to `max_streams` concurrent viewers.

### What changes

#### New files
1. `include/hal/stream_writer.h` — `StreamWriter` abstract interface
2. `include/async_web_stream_writer.h` — `AsyncWebStreamWriter` adapter
3. `src/async_web_stream_writer.cpp` — implementation
4. `test/mocks/native_mocks/mock_stream_writer.h` — mock for native tests
5. `include/stream_service.h` — `StreamService` class
6. `src/stream_service.cpp` — implementation
7. `test/native/test_stream_service/` — test suite

##### `include/stream_service.h`
`struct StreamSlot` with buffer, semaphore, `task_handle`, `StreamWriter* writer`
`class StreamService` with:
- `init(max_streams, slot_buffer_size)`
- `start_stream(StreamWriter* writer)` — creates `sustain_task`
- `stop_stream(slot_num)`
- `deliver_frame(data, len)` — called by `capture_task`
- `active_slot_count()`

`StreamWriter` (`include/hal/stream_writer.h`) is a minimal abstract interface:
```cpp
class StreamWriter {
    public:
        virtual ~StreamWriter() = default;
        virtual size_t write(const uint8_t* data, size_t len) = 0;
        virtual void close() = 0;
};
```

`AsyncWebStreamWriter` (`include/async_web_stream_writer.h`) adapts `AsyncClient*`:
```cpp
class AsyncWebStreamWriter : public StreamWriter {
    public:
        AsyncWebStreamWriter(AsyncClient* client);
        size_t write(const uint8_t* data, size_t len) override;
        void close() override;
    private:
        AsyncClient* m_client;
};
```

Created in the `/stream` route handler via `request.create_stream_writer()` and injected at `start_stream()`.

Flow: `request->send()` for chunked response → `writer->write(multipart_header)` → wait on semaphore → `writer->write(frame)` → `writer->write(boundary)`

#### `include/video_controller.h` — Add `set_stream_service`
- `void set_stream_service(StreamService* svc)`
- `on_frame_capture()` delivers frame to all active stream slots

#### Command codes
- Add `START_STREAM = 0x13`, `STOP_STREAM = 0x14`, `LIST_STREAMS = 0x15`

#### SystemOrchestrator — stream command handlers
- `handle_start_stream()`, `handle_stop_stream()`, `handle_list_streams()`

#### `src/main.cpp` — Wire StreamService
- Create `StreamService`, init with `max_streams` from config
- `video_controller.set_stream_service(&stream_service)`
- `/stream` route registered in `SystemOrchestrator::init()` via `WebServerHal::on()` — handler calls `request.create_stream_writer()` and passes the writer to `StreamService::start_stream()`

### Documentation updates

#### `docs/SYSTEM_DESIGN.md`
- Add `StreamService` class documentation — `StreamSlot` struct, `deliver_frame()`, `sustain_task` design, PSRAM slot buffers.
- Update `VideoController` section — document `set_stream_service()` and frame delivery to active stream slots.
- Add new `CommandCode` entries to the command dispatch section (`START_STREAM`, `STOP_STREAM`, `LIST_STREAMS`).
- Add `handle_start_stream()`, `handle_stop_stream()`, `handle_list_streams()` to `SystemOrchestrator` section.
- Update architecture diagram — add `StreamService` layer with arrow from `capture_task`.
- Add `max_streams` config key to config section.

#### `README.md`
- Add `/stream` GET endpoint to the Web Interface endpoints table.
- Add `max_streams` to the configuration table under the `[web]` section.
- Add a brief "Live Streaming" paragraph describing the MJPEG stream endpoint.

### Files modified (10+)
1. `include/stream_service.h` — NEW
2. `src/stream_service.cpp` — NEW
3. `test/native/test_stream_service/test_stream_service.cpp` — NEW
4. `include/video_controller.h` — add `set_stream_service`
5. `src/video_controller.cpp` — `on_frame_capture()` delivers to stream
6. `include/command_handler.h` — add stream `CommandCode` entries
7. `include/system_orchestrator.h` — add stream handlers
8. `src/system_orchestrator.cpp` — implement stream handlers
9. `src/main.cpp` — wire `StreamService`, register `/stream` route
10. `docs/SYSTEM_DESIGN.md` — update architecture
11. `README.md` — add `/stream` endpoint and `max_streams` config

### Tests — StreamService (native)
- `MockStreamWriter` captures written data for verification
- `init(4)` → 4 slots allocated
- `deliver_frame()` with no active slots → no crash
- Single stream: `start_stream(writer)` → deliver → `writer->write()` called with frame data
- Multiple streams: deliver to 2+ active slots, each `MockStreamWriter` receives data
- `stop_stream()` → slot inactive, `writer->close()` called
- Buffer overflow: frame > `slot_buffer_size` → dropped (logged)
- Timeout: `sustain_task` waits 1.2s and cleans up
- Max streams: try 5th when `max=4` → returns false
- PSRAM: slot buffers allocated on PSRAM if available
- `AsyncWebStreamWriter` tested on hardware only (requires `AsyncClient`)

### Verification (hardware)
- Browser to `/stream` → live MJPEG feed
- Recording + streaming simultaneously
- 2+ concurrent streams
- Stream reconnection: reload → new stream, old cleans up
- `max_streams=0` → stream endpoint returns error
- Timeout: unplug client → stream terminates within 2s

---

## Summary of all files

### New files (7)
1. `include/hal/timer_hal.h`
2. `src/hal/timer_hal_impl.h`
3. `src/hal/timer_hal_impl.cpp`
4. `test/mocks/native_mocks/mock_timer_hal.h`
5. `include/stream_service.h`
6. `src/stream_service.cpp`
7. `test/native/test_stream_service/test_stream_service.cpp`

### Modified files (18-22 depending on test granularity)
1. `platformio.ini`
2. `include/hal/web_server_hal.h`
3. `include/hal/web_server_hal_impl.h`
4. `src/hal/web_server_hal_impl.cpp`
5. `include/web_server_service.h`
6. `src/web_server_service.cpp`
7. `include/web_command_transport.h`
8. `src/web_command_transport.cpp`
9. `include/sd_controller.h`
10. `src/sd_controller.cpp`
11. `include/video_controller.h`
12. `src/video_controller.cpp`
13. `include/peripheral_manager.h`
14. `src/peripheral_manager.cpp`
15. `include/system_orchestrator.h`
16. `src/system_orchestrator.cpp`
17. `include/command_handler.h`
18. `src/main.cpp`
19. `test/mocks/native_mocks/mock_web_server_hal.h`
20. `test/native/test_video_controller/`
21. `test/native/test_system_orchestrator/`
22. `test/native/test_web_server_service/`
23. `test/native/test_web_command_transport/`
24. `docs/SYSTEM_DESIGN.md`

---

## Verification checklist (end-to-end)

After all 3 phases:
- [ ] Web UI loads at camera IP (same as before)
- [ ] `/status`, `/control` commands work
- [ ] Recording starts/stops correctly
- [ ] FPS matches configured rate
- [ ] AVI files playable in VLC
- [ ] Clip rotation takes ~50ms (not seconds)
- [ ] `TAKE_PHOTO` saves `.jpg` during idle, errors during recording
- [ ] `/stream` shows live MJPEG in browser
- [ ] Recording + streaming simultaneously
- [ ] 2+ concurrent streams stable
- [ ] 10+ sequential clips, no memory leak
- [ ] LED behavior correct (blinks during recording, off when idle)
- [ ] All native tests pass
- [ ] All hardware tests pass
