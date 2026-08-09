# Slice 3B Review Fix Plan

## Context

- Independent review of commit `83bb74a` ("Implemented slice 3B - VideoController refactor + FreeRTOS") returned a **Changes requested** verdict, driven by one Critical bug (clip header patch truncates the file) plus several Major/Minor items.
- All 321 native tests pass and the `seeed_xiao_esp32s3` build succeeds on the reviewed commit; the failures are hardware-visible and masked by mock semantics.
- This plan is the agreed remediation. It covers issues 1-10 from the review report, test coverage extensions (native + hardware), style-guide nits, and the implementation order.

## Approved decisions

| Issue | Decision |
|---|---|
| 1 | Add non-truncating `READ_WRITE` open mode (`"r+"`) for the header patch; upgrade `MockSdCardHal` to model open-mode semantics; regression tests |
| 2 | **Option A — synchronous `stop_recording()`** with a bounded join on the capture task (restores plan's original "wait on done semaphore" semantics) |
| 3 | `ICaptureScheduler::start()` returns `bool`; second start while running fails and creates no task |
| 4 | Serial counter (`_1`, `_2`, ...) for filename collisions, shared via a `PathUtils` helper |
| 5 | Addressed by 2 — remove the never-used `STOPPING` state |
| 6 | Use standard library `gmtime_r` in a shared `TimeFormatter` utility; delete duplicated calendar math |
| 7 | Remove dead `MockTimerHal` (only the scheduler could use it; scheduler is tested on real HW) |
| 8 | Delete the dead/broken `SdOperation::SEEK_WRITE` branch in `SdController::tick()` (real seek-writes live in `write_at_sync()`) |
| 9 | Use `std::atomic` for all cross-task shared data |
| 10 | Rename misleading test `test_no_frame_when_interval_not_elapsed` |

## Already applied in working tree (do not redo)

- `platformio.ini`: `seeed_xiao_esp32s3_test` → `test_filter = hardware/*`
- `include/capture_scheduler.h`: removed dangling empty docstring lines
- `include/capture_scheduler_esp32.h`: added `#include <functional>`

---

## Issue 1 — Header patch truncates entire clip (Critical)

**Root cause (verified at framework level):** `VideoController::close_clip()` → `write_at_offset()` → `SdController::write_at_sync()` opens with `FileOpenMode::WRITE` → `SdCardHalImpl::open()` maps it to `FILE_WRITE` = `"w"` → `fopen(path, "w")` truncates to 0 before the `seek(0)` header patch. Final clip would contain only the 224-byte patched header. Native tests pass because `MockSdCardHal::open()` only records the mode.

**Approach:**
1. `include/hal/sd_card_hal.h`: add `READ_WRITE` to `hal::FileOpenMode` — "Open for reading and writing; does not truncate; file must exist."
2. `src/hal/sd_card_hal.cpp`: map `READ_WRITE` → `"r+"` in `SdCardHalImpl::open()`. The Arduino FS API passes the mode string to `fopen` (`VFSFileImpl`), so `SD.open(path, "r+")` works with no framework change. `"r+"` preserves content and `fseek`+`fwrite` write in place. Safe here because the clip file always exists by `close_clip()` time.
3. `src/sd_controller.cpp`: `write_at_sync()` opens with `READ_WRITE` instead of `WRITE`.

**Tests:**
- Upgrade `MockSdCardHal` with an in-memory file store (`std::map<std::string, std::vector<uint8_t>>` + open position) implementing **real mode semantics**: `WRITE` truncates on open; `APPEND` preserves and writes at end; `READ_WRITE` preserves and writes at seek position; `READ` reads from position; add `exists(path)`. Existing knobs (`open_result`, `write_result`, call counts) remain — additive, no existing suite breaks.
- `test/native/test_sd_controller`: `test_write_at_sync_preserves_existing_content` — write N bytes via `write_sync(APPEND)`, then `write_at_sync(path, 0, hdr)` → size unchanged, first bytes replaced. **Must fail against the old `WRITE` implementation** (true regression test).
- `test/native/test_video_controller`: `test_clip_file_intact_after_close` — record a short clip through `close_clip()`, assert mock file `size == header + chunks + index` and first bytes equal the patched header.

---

## Issue 2 — Synchronous `stop_recording()` (Option A)

**Current behavior:** `stop_recording()` sets `m_stop_clip` and calls `m_scheduler.stop()`; state stays `RECORDING` until the next frame callback runs `close_clip()`. Consequences: `GET_STATUS` reports `recording:true` after the command; immediate `start_recording()` fails; a stale notification after close can run `capture_and_write_frame()` with reset writer state and append garbage after `idx1` (corrupt AVI); clip left unfinalized if power dies in the window.

**Approach:**
1. `CaptureSchedulerEsp32`:
   - Constructor creates a binary **exit semaphore** (`xSemaphoreCreateBinary()`).
   - `stop()`: set atomic stop flag → `m_timer.stop()` → `xTaskNotifyGive(m_task)` → `xSemaphoreTake(m_exit_sem, timeout)` with a generous timeout (e.g. 1000 ms). On timeout, fail safe: state remains `RECORDING` so the next frame still closes the clip.
   - Task exit branch: give the exit semaphore (task context, not ISR) then `vTaskDelete(nullptr)`.
   - After the join, set `m_task = nullptr` so restart works and `start()` can never leak a second task.
2. `MockCaptureScheduler::stop()` mirrors the contract: invoke the stored callback once synchronously (the "final frame"), then mark stopped — native tests exercise the same path as hardware.
3. `VideoController::stop_recording()` stays "set flag → `m_scheduler.stop()`" but now returns with state `IDLE` on success.
4. **State guard (belt-and-suspenders, required regardless):** add `if (m_state != RecordingState::RECORDING) return;` at the top of `capture_and_write_frame()` — kills the corruption race in every scenario (stale ISR notification, double-stop, post-stop calls).
5. Remove `STOPPING` from `RecordingState` (it becomes unobservable; fixes issue 5).
6. Update `stop_recording()` docstring in `include/video_controller.h` (currently describes old async behavior).

**Tests (native, `test_video_controller`):**
- Update stop tests: after `stop_recording()` state is immediately `IDLE`, no extra `capture_one_frame()` needed (stop tests currently inject a frame to process the stop).
- `test_stale_callback_after_stop_ignored` — after stop, an extra `on_frame_capture()`/callback invocation writes nothing (state guard).
- Restart-after-stop: `start_recording()` succeeds immediately after `stop_recording()`.

**Hardware (`test/hardware/test_capture_scheduler_esp32`):** `stop()` blocks until final callback completes and task exits (see Hardware coverage below).

---

## Issue 3 — Double-start protection

**Approach:**
1. `ICaptureScheduler::start()` returns `bool` — `false` if already running. Document: "Returns false if the scheduler is already started."
2. `CaptureSchedulerEsp32::start()`: `if (m_task != nullptr) return false;` before touching the timer or creating a task. With the synchronous stop (issue 2) clearing `m_task`, restart works naturally.
3. `MockCaptureScheduler`: add `bool start_result{true}`; second `start()` while `m_started` returns `false` and does **not** overwrite the stored callback.
4. `VideoController::start_recording()`: if `m_scheduler.start(...)` returns `false` → close the just-opened clip, set `IDLE`, log, return `false` (defense in depth).

**Tests (native):**
- `test_double_start_rejected` — second `start()` returns false; original callback still invoked on trigger.
- `test_start_recording_fails_when_scheduler_busy` — mock `start_result=false` → `start_recording()` false, state `IDLE`, no dangling clip.
- `test_restart_after_stop_succeeds` — start → stop → start returns true.

**Hardware (`test/hardware/test_capture_scheduler_esp32`):** double-start returns false and callback rate stays at one task's worth (no duplicate frames).

---

## Issue 4 — Filename collision serial counter

**Approach:**
1. Add `bool exists(const char* path)` to `hal::SdCardHal`; implement in `SdCardHalImpl` via `SD.exists()`; add to `MockSdCardHal` (file store lookup).
2. `SdController::exists_sync(path)` — CS assert/deassert around the check (all SD I/O must go through the controller's pin arbitration).
3. New `include/path_utils.h` / `src/path_utils.cpp`: `make_unique_path(base_path, exists_fn, out, out_size)` — appends `_1`, `_2`, ... up to a cap (e.g. 100); returns false if cap exceeded. Takes an `exists_fn` callback to stay decoupled from `SdController`.
4. `VideoController::open_clip()`: probe `exists_sync(candidate)` and uniquify before opening.
5. `SystemOrchestrator::handle_take_photo()`: same uniquification (photos currently use `WRITE`/truncate, so same-second photo collisions also overwrite).
6. Add `path_utils.cpp` to the `native` `build_src_filter` in `platformio.ini`.

**Tests (native):**
- New `test_path_utils` suite: no collision → base path; `_1`, `_2` suffixes; cap exceeded → false.
- `test_clip_name_unique_on_collision` (`test_video_controller`): pre-seed mock store with base path → clip uses `_1`, nothing overwritten.
- Orchestrator photo-collision test.

---

## Issue 6 — Standard library time formatting

**Rationale:** The hand-rolled math (`day_of_year/30`, `%365`, `1970 + secs/(365*86400)`) produces impossible dates (e.g. Feb 30). Both ESP32 newlib and macOS host provide `gmtime_r`, giving correct civil time (leap years) with no TZ dependency (UTC). `time_t` is 64-bit on both; `TimeService::unix_now()` returns `uint32_t` — cast is safe. Only called from task context, never ISR.

**Approach:**
1. New `include/time_formatter.h` / `src/time_formatter.cpp` — `class TimeFormatter` with static helpers: `format_date(uint32_t unix_seconds, char* out, size_t n)` → `YYYYMMDD`, `format_time(...)` → `HHMMSS`. Use `gmtime_r`, `snprintf`.
2. Refactor `VideoController::open_clip()` and `SystemOrchestrator::handle_take_photo()` to use it; delete duplicated math.
3. Add `time_formatter.cpp` to the `native` `build_src_filter` in `platformio.ini`.

**Tests (native, new `test_time_formatter` suite):**
- Fixed epoch vectors including leap years: `0 → 19700101`, `951782400 → 20000229` (leap day), `1710000000 → 20240309`, plus `HHMMSS` vectors and buffer-size truncation behavior.

---

## Issue 7 — Remove dead `MockTimerHal`

- Delete `test/mocks/native_mocks/mock_timer_hal.h` (verified zero references in `test/`, `src/`, `include/`).
- Remove the `MockTimerHal` row from `docs/SYSTEM_DESIGN.md` (~line 170).
- No replacement: the only consumer (scheduler) is exercised on real hardware.

---

## Issue 8 — Delete dead `SEEK_WRITE` branch

**Explanation of current state:** the real seek-write path is `SdController::write_at_sync()` (`src/sd_controller.cpp:268-276`) — direct `cs_assert → open → seek → write → close → cs_deassert`, bypassing the async state machine. The `SdOperation::SEEK_WRITE` branch inside `tick()`'s `IO_ACTIVE` state is legacy dead code — no caller queues it, and it's doubly broken: it re-opens the file after the outer `else if` already opened it (handle leak), and it uses `WRITE` mode (the same truncation bug).

**Approach:**
- Delete the `else if (m_operation == SdOperation::SEEK_WRITE)` branch in `SdController::tick()`.
- Delete the `SEEK_WRITE` entry from `SdOperation` in `include/sd_controller.h`.
- Remove `m_open_mode` member if it becomes unused (verify before deleting).
- Keep only the corrected `write_at_sync()`.

**Tests:** existing `test_sd_controller` suite continues to pass; the `write_at_sync` regression tests (issue 1) cover the surviving path. No new test for deleted code.

---

## Issue 9 — Atomic types for cross-task data

**Approach** (all are read/written across main task ↔ capture task/ISR; `std::atomic` is supported on Xtensa ESP32-S3 (`S32C1I`) and host):

| Member | Change |
|---|---|
| `CaptureSchedulerEsp32::m_stop_requested` | `volatile bool` → `std::atomic<bool>` |
| `CaptureSchedulerEsp32::m_task` | `TaskHandle_t` → `std::atomic<TaskHandle_t>` (relaxed load in `on_frame_capture()` ISR path) |
| `VideoController::m_stop_clip` | `bool` → `std::atomic<bool>` |
| `VideoController::m_frames_recorded` | `uint32_t` → `std::atomic<uint32_t>` (written by capture task, read by `GET_STATUS`) |
| `VideoController::m_file_size` | `uint32_t` → `std::atomic<uint32_t>` |
| `VideoController::m_state` | `RecordingState` → `std::atomic<RecordingState>` |

- Keep ISR-adjacent accesses relaxed with an explanatory comment.
- No new tests required for atomics themselves; covered by the issue 2/3 behavior tests.

---

## Issue 10 — Rename misleading test

- `test_no_frame_when_interval_not_elapsed` → `test_no_frame_without_scheduler_trigger` (matches its existing brief "No frame captured when capture is not triggered").
- Add a comment: true interval pacing is only exercisable on hardware (native mock is synchronous); pacing covered by `test/hardware/test_capture_scheduler_esp32`.

---

## Test coverage extensions

### Native
1. `MockSdCardHal` file-store upgrade (linchpin — unlocks issues 1/4 regression tests, photo-failure tests).
2. `test_sd_controller`: `write_at_sync` content preservation; `exists_sync`; extend `create_dir_sync` failure coverage.
3. `test_video_controller`: clip integrity after close; collision suffix; `capture_photo` with `write_result=false` → returns `false` and releases frame; `start_recording` when scheduler busy; immediate stop → `IDLE`; restart-after-stop; stale-callback-after-stop ignored.
4. `test_system_orchestrator`: TAKE_PHOTO SD-failure → error response, no path echo.
5. New suites: `test_time_formatter`, `test_path_utils`.

### Hardware (`test/hardware/`, on-device via `seeed_xiao_esp32s3_test`)
1. `test_capture_scheduler_esp32/`: timer-driven callbacks fire at ~interval (tolerance); **`stop()` blocks until final callback completes and task exits** (issue 2 regression); **double-start returns false without spawning a second task** (issue 3 regression); restart-after-stop works.
2. `test_timer_hal_impl/`: `init`/`start`/`stop`/`set_frequency`; ISR callback count over a known window with tolerance.
3. `test_sd_card_hal_impl/`: real-card open-mode semantics — `WRITE` truncates, `APPEND` preserves, **`READ_WRITE` seek+write preserves existing content** (issue 1 on-device regression). Guard with `TEST_IGNORE` if no card present.
4. End-to-end clip integrity: record ~1-2 s, stop, read clip back — assert `"RIFF"` magic, plausible sizes, `idx1` at tail.

Each suite is a subfolder under `test/hardware/` with a `test_` prefix and a single `main()` gathering its tests. `test_filter = hardware/*` (already applied) collects them.

---

## Style guide items (AGENTS.md)

Already applied in working tree:
- Removed dangling empty docstring lines in `capture_scheduler.h`.
- Added `#include <functional>` to `capture_scheduler_esp32.h` (include-what-you-use).

To apply during implementation:
- Update `stop_recording()` docstring in `video_controller.h` to describe synchronous behavior.
- Ensure all new files follow: `#ifdef` guards, Javadoc docstrings, Allman braces, `m_` member prefix, snake_case filenames, type-positioned `&`/`*`, aligned initializer lists, per-test function briefs.

---

## Documentation updates

- `docs/SYSTEM_DESIGN.md`:
  - Remove `MockTimerHal` row.
  - Document `READ_WRITE` mode in `SdCardHal`/`SdController` sections.
  - Update `VideoController` stop semantics to synchronous close.
  - Document `TimeFormatter` and `PathUtils` modules.
  - Remove `SdOperation::SEEK_WRITE` references if present.
- `platformio.ini`: add `time_formatter.cpp` and `path_utils.cpp` to `native` `build_src_filter`.

---

## Implementation order

1. (done) Filter + docstring + include nits — already in working tree
2. `MockSdCardHal` file-store upgrade
3. Issue 1 — `READ_WRITE` mode + `write_at_sync` fix + regression tests
4. Issue 8 — delete dead `SEEK_WRITE` branch + enum entry
5. Issue 6 — `TimeFormatter` (+ tests) → refactor both call sites
6. Issue 4 — `PathUtils` + `exists_sync` + collision tests
7. Issues 2/3/5/9 — scheduler sync stop + `bool start()` + atomics + state guard + remove `STOPPING` + updated/renamed tests
8. Issue 7 — remove `MockTimerHal`; Issue 10 — rename test; remaining docstring nits
9. Hardware test suites (`test/hardware/`)
10. `SYSTEM_DESIGN.md` + `platformio.ini` final pass
11. Full verification (below)

---

## Verification

```sh
source ~/.platformio/penv/bin/activate && pio test -e native
pio test -e seeed_xiao_esp32s3_test   # hardware suites; requires board + upload_port
pio run -e seeed_xiao_esp32s3         # embedded build must succeed
```

- All 321+ native tests pass (new suites and updated tests included).
- Hardware suites build for `seeed_xiao_esp32s3_test`; on-device runs when board attached.
- `seeed_xiao_esp32s3` firmware build succeeds (RAM/Flash within budget).
