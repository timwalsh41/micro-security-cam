# Phase 1: Standalone Video Recording

## Overview

Extend the ESP32_photo_camera's layered OOP framework with MJPEG video recording in AVI containers, porting core patterns from the ESP32-CAM_MJPEG2SD (record_video_decorder) reference implementation. This is a standalone, video-only phase — no audio, no streaming, no WiFi dependency. The existing cooperative polling architecture is preserved.

## Background

The ESP32 camera driver produces one JPEG frame at a time via `esp_camera_fb_get()`. There is no "video mode" — video recording is simply grabbing still frames at a steady cadence and wrapping them in an AVI container where each frame is a complete, independent JPEG image (Motion JPEG). Audio recording is a separate hardware pipeline (I2S/PDM microphone) that will be integrated in Phase 2.

The ESP32-CAM_MJPEG2SD reference project already implements a production-grade MJPEG video recorder with FreeRTOS tasks, audio, streaming, and motion detection. This plan extracts the relevant patterns — AVI container formatting, frame buffering, and SD write optimization — and integrates them into the existing photo camera's architecture without adopting the FreeRTOS task model (deferred to Phase 2).

## Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| **Scope** | Video-only recording to SD | Matches existing standalone capture pattern; no WiFi/network dependency |
| **Audio** | Deferred to Phase 2 | Requires I2S DMA with an independent FreeRTOS task; adds significant memory overhead |
| **Streaming** | Deferred to Phase 2 | Requires concurrent capture + HTTP serving; motivates the FreeRTOS task model |
| **Concurrency** | Cooperative polling (`tick()`) | Matches existing architecture; highly testable; sufficient for video-only at ≤15 FPS |
| **Platform** | Portable HAL abstraction | No platform-specific code outside HAL implementations; enables future porting |
| **File format** | Standard AVI with MJPEG | Universally playable; proven by reference implementation; no custom codecs needed |

## Architecture

### Data Flow

```
┌──────────────────────────────────────────────────────────────┐
│  Main Loop (setup/loop)                                      │
│  • PeripheralManager::tick()     — LED + SD CS state machine │
│  • VideoController::tick()       — Frame pacing + recording  │
│  • (existing photo cycle)        — Still capture on interval │
└──────────────────────────────────────────────────────────────┘
         │                              │
         ▼                              ▼
┌─────────────────────┐    ┌───────────────────────────┐
│  PeripheralManager   │    │  VideoController          │
│  • LedStateMachine   │    │  • State machine (IDLE/   │
│  • SdController      │    │    RECORDING/STOPPING)    │
│  • PinArbitrator     │    │  • Frame pacing via       │
│                     │    │    elapsed time check     │
└─────────────────────┘    │  • File naming + dirs     │
                           └─────┬─────┬─────┬────────┘
                                 │     │     │
                    ┌────────────┘     │     └────────────┐
                    ▼                  ▼                  ▼
           ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐
           │ CameraCtrl   │  │ AviWriter    │  │ SdWriteBuffer    │
           │ • capture()  │  │ • open()     │  │ • buffer_frame() │
           │ • get_frame()│  │ • write()    │  │ • flush()        │
           │ • release()  │  │ • close()    │  │                  │
           └──────────────┘  └──────────────┘  └──────────────────┘
                │                   │                    │
                ▼                   ▼                    ▼
           ┌──────────┐      (pure byte     ┌──────────────────┐
           │ CameraHal│       formatting,    │   SdController   │
           │ (esp_cam)│       no I/O)        │ • write_file()   │
           └──────────┘                      └──────────────────┘
                                                    │
                                                    ▼
                                             ┌──────────────┐
                                             │  SdCardHal   │
                                             │  (SD_MMC)    │
                                             └──────────────┘
```

### Key Architectural Points

- **AviWriter is format-only** — it builds AVI byte streams in memory. No I/O, no HAL, no tasks. Composition with SD happens at the VideoController level via SdWriteBuffer.
- **SdWriteBuffer decouples buffering from I/O** — accumulates small JPEG frames into sector-aligned chunks before flushing through SdController. This minimizes the number of SD writes (the slowest operation).
- **VideoController is a service-layer class** — depends on CameraController, AviWriter, SdWriteBuffer, TimeService, and LoggingService. No direct HAL knowledge.
- **Existing photo capture cycle is unchanged** — video recording is purely additive to main.cpp.

## Implementation Steps

### Step 1: Add AVI Container Module

**New files:** `include/avi_writer.h`, `src/avi_writer.cpp`

A pure utility class that knows how to construct AVI files with MJPEG video streams.

**AVI file structure:**

```
┌─ AVI Header (310 bytes) ─────────────────────────┐
│  RIFF header                                     │
│  LIST - hdrl (header list)                       │
│    ├── Main AVI header (strl)                    │
│    ├── Video stream header (strh: MJPEG, FPS)    │
│    └── Video stream format (strf: BITMAPINFO)    │
├─ LIST - movi (frame data) ───────────────────────┤
│  For each frame:                                 │
│    ├── 0x00dc marker (4 bytes)                   │
│    ├── JPEG frame size (4 bytes)                 │
│    ├── JPEG frame data (N bytes)                 │
│    └── 0-3 bytes padding (DWORD alignment)       │
├─ idx1 (frame index) ────────────────────────────┤
│  Per frame:                                      │
│    ├── Chunk ID (0x00dc, 4 bytes)                │
│    ├── Flags (0x00000000, 4 bytes)               │
│    ├── Data offset (4 bytes)                     │
│    └── Data size (4 bytes)                       │
└──────────────────────────────────────────────────┘
```

**API design:**

```cpp
class AviWriter {
public:
  // Lifecycle
  bool open(const char* path, uint16_t fps, uint16_t width, uint16_t height);
  bool writeFrame(const uint8_t* jpegData, size_t jpegLen);
  bool close();  // Finalizes header with actual frame count and file size

  // State queries
  bool isOpen() const;
  uint32_t frameCount() const;
  uint32_t fileSize() const;

private:
  void buildHeader();         // Write 310-byte AVI header template
  void buildIndexEntry(uint32_t offset, uint32_t size);
  void updateHeader();        // Patch header with final stats at close
  void writeDwordAligned(const uint8_t* data, size_t len);
};
```

**Key implementation details:**
- Header template ported from `avi.cpp` in ESP32-CAM_MJPEG2SD (310 bytes with dynamic field patches at close)
- Frame index entries accumulated in RAM (PSRAM if available, else DRAM with frame-count-only fallback for large recordings)
- DWORD alignment: each frame chunk padded to 4-byte boundary (add 0-3 zero bytes after JPEG data)
- `close()` patches the RIFF size, total frame count, and index offset into the header, then writes the index

**Architecture fit:** Pure utility class. No HAL dependencies. Stateless between open/close cycles. Fully testable on native host with mock file I/O.

**Reference:** `avi.cpp` lines 1-300 (header generation), lines 300-500 (index building, frame chunk writing).

---

### Step 2: Add SD Write Buffering

**New files:** `include/sd_write_buffer.h`, `src/sd_write_buffer.cpp`

Accumulates small JPEG frames into a larger RAM buffer before flushing to SD in sector-aligned chunks.

**Buffer layout** (ported from `mjpeg2sd.cpp`'s `iSDbuffer` pattern):

```
┌─ iSDbuffer (RAMSIZE + CHUNK_HDR) × 2 ──────────────┐
│                                                      │
│  First RAMSIZE bytes   │  Second RAMSIZE bytes       │
│  ← write area 1 →     │  ← write area 2 →          │
│                        ↑ circular wrap point         │
│                                                      │
│  highPoint = current write offset within the buffer  │
│  When highPoint >= RAMSIZE, wrap to start            │
└──────────────────────────────────────────────────────┘

Constants:
  RAMSIZE = 8192      (8KB, multiple of SD sector size)
  CHUNK_HDR = 8       (AVI frame header: 4B marker + 4B size)
```

**API design:**

```cpp
class SdWriteBuffer {
public:
  explicit SdWriteBuffer(SdController& sd);

  // Buffer a frame's worth of AVI data (chunk header + JPEG data)
  bool bufferFrame(const uint8_t* aviChunk, size_t chunkLen);

  // Force flush remaining buffered data to SD
  bool flush();

  // Reset buffer state (e.g., on recording stop)
  void reset();

private:
  bool flushSector();  // Write one RAMSIZE-aligned block to SD
  // Double buffer in PSRAM or DRAM
  uint8_t m_buffer[(RAMSIZE + CHUNK_HDR) * 2];
  size_t m_highPoint;  // Current write offset
};
```

**Flush strategy:**
1. `bufferFrame()` appends the AVI chunk header + JPEG data to the buffer
2. When `m_highPoint >= RAMSIZE`, flush one sector-aligned block to SD via `SdController::write_file()`
3. Wrap write position to buffer start (circular pattern with CHUNK_HDR overlap for safe header writes)
4. `flush()` writes any remaining data in the buffer (called at clip end)

**Architecture fit:** Composes with SdController for actual I/O. No direct HAL access. Buffer allocation is configurable (PSRAM preferred, DRAM fallback).

**Reference:** `mjpeg2sd.cpp` `saveFrame()` function, `iSDbuffer` allocation pattern.

---

### Step 3: Extend Camera Controller for Continuous Capture

**Modify:** `include/camera_controller.h`, `src/camera_controller.cpp`

The camera controller currently supports single-shot capture (`capture_frame()` → `get_frame()` → `release_frame()`). For video, this cycle must work reliably at the target FPS with no frame drops from driver-level issues.

**Changes:**

1. **Add `grab_mode` to `CameraConfig`:**
   ```cpp
   struct CameraConfig {
     // ... existing fields ...
     int grab_mode = 0;  // 0 = default (grab when ready), 1 = CAMERA_GRAB_LATEST
   };
   ```
   When `grab_mode = 1`, the camera driver discards any frame that arrived while the previous frame was still being processed. This prevents stale frame delivery when the SD write occasionally stalls.

2. **Ensure rapid capture/release cycle works:**
   The existing `capture_frame()` / `get_frame()` / `release_frame()` API is already correct for video. The main concern is that the release → re-acquire cycle completes within the frame interval. Testing will verify this at target FPS.

3. **Apply `grab_mode` during `init()`:**
   ```cpp
   config.grab_mode = m_config.grab_mode;
   ```
   This is passed to `esp_camera_init()` which sets it on the camera driver.

**No HAL changes needed** — `CameraHal::capture()` and `release()` already map directly to `esp_camera_fb_get()` and `esp_camera_fb_return()`. The change is purely in the configuration struct and initialization.

**Architecture fit:** Small, backwards-compatible extension to an existing service. Default `grab_mode = 0` preserves current behavior.

---

### Step 4: Add Video Controller Service

**New files:** `include/video_controller.h`, `src/video_controller.cpp`

The central orchestrator that coordinates camera capture, AVI writing, and SD buffering into a recording session.

**State machine:**

```
  ┌─────────────────────────────────────────┐
  │                                         │
  ▼                                         │
IDLE ──── start_recording() ────► RECORDING ──── stop_recording() / max_frames / max_size
  ▲                                         │
  │              close complete             │
  └──────────── STOPPING ◄─────────────────┘
```

**API design:**

```cpp
class VideoController {
public:
  VideoController(CameraController& cam, TimeService& time, LoggingService& log);

  // Initialization
  bool init(const VideoConfig& config, SdController& sd);

  // Called from main loop
  void tick();

  // Control
  bool startRecording();
  bool stopRecording();
  bool isRecording() const;

  // State
  RecordingState state() const;
  uint32_t framesRecorded() const;
  uint32_t fileSize() const;

private:
  void openClip();
  void closeClip();
  void captureAndWriteFrame();
  void createDirectory(const char* path);

  // Dependencies (injected)
  CameraController& m_camera;
  TimeService& m_time;
  LoggingService& m_log;

  // Owned components
  AviWriter m_aviWriter;
  SdWriteBuffer m_sdBuffer;

  // State
  RecordingState m_state;
  uint32_t m_lastFrameTime;
  uint32_t m_framesRecorded;
  uint32_t m_fileSize;
  VideoConfig m_config;
};
```

**`tick()` implementation (cooperative frame pacing):**

```cpp
void VideoController::tick() {
  if (m_state != RECORDING) return;

  uint32_t now = m_time.now();
  uint32_t elapsed = TimeService::elapsed_ms(m_lastFrameTime, now);

  if (elapsed >= m_frameIntervalMs) {
    captureAndWriteFrame();
    m_lastFrameTime = now;

    // Check clip termination conditions
    if (m_config.clipDurationS > 0 &&
        m_framesRecorded >= m_config.fps * m_config.clipDurationS) {
      stopRecording();
    }
    if (m_config.maxFileSizeMB > 0 &&
        m_fileSize >= m_config.maxFileSizeMB * 1024UL * 1024UL) {
      stopRecording();
    }
  }
}
```

**`captureAndWriteFrame()` implementation:**

```cpp
void VideoController::captureAndWriteFrame() {
  auto result = m_camera.capture_frame();
  if (result.status != CameraCaptureStatus::OK) {
    m_log.warn("[Video] Frame capture failed");
    return;
  }

  CameraFrame frame = m_camera.get_frame();
  m_aviWriter.writeFrame(frame.data, frame.len);
  m_sdBuffer.bufferFrame(/* AVI chunk: header + JPEG data */, frame.len + CHUNK_HDR);
  m_camera.release_frame();

  m_framesRecorded++;
  m_fileSize += frame.len + CHUNK_HDR + padding;
}
```

**File naming:**

```
/SD/
├── /20260705/
│   ├── 20260705_143022_640x480_10fps.avi
│   ├── 20260705_144155_640x480_10fps.avi
│   └── ...
```

Pattern: `YYYYMMDD_HHMMSS_<WxH>_<FPS>fps.avi`

**Clip rotation:**
- When a clip reaches `clip_duration_s` or `max_file_size_mb`, `closeClip()` is called
- A new clip is opened immediately if `startRecording()` was not explicitly stopped
- This enables unattended recording that doesn't create unbounded files

**Architecture fit:** Service-layer class. Depends on CameraController, AviWriter, SdWriteBuffer, TimeService, LoggingService. No direct HAL knowledge. Fully injectable dependencies enable native testing with mocks.

---

### Step 5: Extend Configuration System

**Modify:** `include/ini_parser.h`, `src/ini_parser.cpp`, `include/config_service.h`, `src/config_service.cpp`

Add a `[video]` section to the INI config format.

**New config section:**

```ini
[video]
fps=10
resolution=VGA
clip_duration_s=60
max_file_size_mb=0
jpeg_quality=10
```

**New struct in config_data.h or config_service.h:**

```cpp
struct VideoConfig {
  uint16_t fps = 10;
  framesize_t resolution = FRAMESIZE_VGA;  // 640×480
  uint16_t clipDurationS = 60;             // 0 = no time limit
  uint16_t maxFileSizeMB = 0;              // 0 = no size limit
  uint8_t jpegQuality = 10;               // 0-63 (lower = better quality)
};
```

**Resolution mapping:**

| Config String | `framesize_t` | Resolution |
|--------------|---------------|------------|
| `QQVGA` | `FRAMESIZE_QQVGA` | 160×120 |
| `QVGA` | `FRAMESIZE_QVGA` | 320×240 |
| `VGA` | `FRAMESIZE_VGA` | 640×480 |
| `SVGA` | `FRAMESIZE_SVGA` | 800×600 |
| `XGA` | `FRAMESIZE_XGA` | 1024×768 |
| `HD` | `FRAMESIZE_HD` | 1280×720 |
| `UXGA` | `FRAMESIZE_UXGA` | 1600×1200 |

**Changes to IniParser:**

- `parse()`: Handle `fps`, `resolution`, `clip_duration_s`, `max_file_size_mb`, `jpeg_quality` keys under `[video]`
- `serialize()`: Write `[video]` section from `VideoConfig` struct
- Range validation: `fps` (1-30), `jpeg_quality` (0-63)

**Changes to ConfigService:**

- Add `VideoConfig video` field to `ConfigData`
- `load()` logs video config at INFO level: `[Config] Video: 10fps VGA, clip 60s, quality 10`

**Architecture fit:** Extends existing patterns. No new dependencies. Defaults are safe for immediate recording.

---

### Step 6: Update Main Loop and PeripheralManager

**Modify:** `src/main.cpp`

**Startup additions (after SD init and config load):**

```cpp
// Existing:
// 1. Init serial
// 2. Init PeripheralManager (GPIO, LED)
// 3. Blink LED 3x
// 4. Init camera
// 5. Init SD card
// 6. Load config.ini

// New:
// 7. Init VideoController with video config
VideoController videoController(cameraController, realTimeService, loggingService);
videoController.init(configService.config().video, peripheralManager.sdController());

// 8. Start recording (if configured to auto-start)
videoController.startRecording();
```

**Loop additions:**

```cpp
void loop() {
  peripheralManager.tick();       // Existing: LED + SD CS state machine (~5ms)
  videoController.tick();         // New: frame pacing + recording
  // Existing photo capture cycle remains unchanged
}
```

**LED feedback:**

```cpp
// In VideoController::startRecording():
m_peripheral.blink(200, 200);  // Fast blink during recording

// In VideoController::closeClip() (between clips):
m_peripheral.pulse_led(500);   // Brief pulse at clip boundary

// In VideoController::stopRecording():
m_peripheral.turn_led_off();   // LED off when idle
```

Note: The PeripheralManager reference needs to be passed to VideoController or the LED commands are issued at the application layer in main.cpp. The cleaner approach is to keep VideoController focused on recording and handle LED feedback in main.cpp based on `videoController.state()`.

---

### Step 7: Update Documentation and Add Tests

#### Documentation Updates

**`docs/SYSTEM_DESIGN.md`:**
- Add VideoController, AviWriter, and SdWriteBuffer to the service layer diagram
- Add data flow showing the recording pipeline
- Add `[video]` section to the config file documentation
- Document the recording state machine

**`README.md`:**
- Add video recording usage section
- Document config.ini `[video]` section with examples
- Describe LED feedback patterns during recording

**`TODO.md`:**
- Mark "Migrate to video" as in progress / Phase 1 complete
- Add Phase 2 items (audio, streaming, timelapse, web playback)

#### New Native Tests

**`test/native/test_avi_writer/`:**
- AVI header generation: correct RIFF structure, stream headers, dimensions, FPS
- Frame writing: correct chunk markers (0x00dc), size fields, DWORD alignment
- Index building: correct offsets and sizes per frame entry
- Close: header patching with final frame count and file size
- Edge cases: single frame, empty close, maximum frames
- Mock: in-memory buffer or temp file代替 real SD I/O

**`test/native/test_video_controller/`:**
- State machine: IDLE → RECORDING → STOPPING → IDLE transitions
- Frame pacing: `tick()` only captures when frame interval elapsed
- Clip rotation: auto-close at `clip_duration_s` or `max_file_size_mb`
- Config integration: different FPS/resolution/duration settings
- Mock: MockCameraController, MockSdController, MockTimeService

**`test/native/test_sd_write_buffer/`:**
- Buffer accumulation: frames buffered until RAMSIZE threshold
- Flush: correct sector-aligned writes to SD
- Double-buffer wrap: circular buffer doesn't corrupt data at wrap point
- Flush on demand: `flush()` writes remaining data correctly
- Mock: MockSdController tracking write calls and data

## Verification Checklist

| # | Test | Expected Result |
|---|------|-----------------|
| 1 | Run `pio test -e native` | All existing + new tests pass |
| 2 | Record 10s clip at VGA/10fps | AVI file plays in VLC with correct resolution, ~100 frames, 10s duration |
| 3 | Record 60s clip | Date-based folder created, file named `YYYYMMDD_HHMMSS_640x480_10fps.avi` |
| 4 | Clip rotation at 60s | First clip closed, second clip opened automatically |
| 5 | Config round-trip | Modify `[video]` in config.ini → parse → serialize → re-parse matches |
| 6 | LED feedback | Fast blink during recording, pulse at clip boundary, off when stopped |
| 7 | Memory stability | Record 5+ clips sequentially — no heap/PSRAM leaks |
| 8 | SD card full | Recording stops gracefully, last clip finalized (header valid) |
| 9 | Rapid start/stop | Start → stop within 1 second — valid AVI file produced |
| 10 | PSRAM absent | Recording works at reduced quality/frame count |
| 11 | Build | Clean compilation for `seeed_xiao_esp32s3` and `native` environments |
| 12 | ffprobe validation | `ffprobe output.avi` shows MJPEG stream, correct fps, frame count |

## Relevant Files

### ESP32_photo_camera (target — to modify)

| File | Action | Purpose |
|------|--------|---------|
| `include/avi_writer.h` | **New** | AVI container writer class declaration |
| `src/avi_writer.cpp` | **New** | AVI container writer implementation |
| `include/sd_write_buffer.h` | **New** | SD write buffer class declaration |
| `src/sd_write_buffer.cpp` | **New** | SD write buffer implementation |
| `include/video_controller.h` | **New** | Video controller service declaration |
| `src/video_controller.cpp` | **New** | Video controller service implementation |
| `include/camera_controller.h` | Modify | Add `grab_mode` to CameraConfig |
| `src/camera_controller.cpp` | Modify | Pass `grab_mode` to esp_camera_init |
| `include/ini_parser.h` | Modify | Add VideoConfig fields to ConfigData |
| `src/ini_parser.cpp` | Modify | Parse/serialize `[video]` section |
| `include/config_service.h` | Modify | Add VideoConfig struct |
| `src/config_service.cpp` | Modify | Log video config at startup |
| `src/main.cpp` | Modify | Init VideoController, add tick(), LED feedback |
| `docs/SYSTEM_DESIGN.md` | Modify | Document new modules and data flow |
| `README.md` | Modify | Video recording usage and config docs |
| `TODO.md` | Modify | Update project status |

### ESP32-CAM_MJPEG2SD (reference — read-only)

| File | Reference For |
|------|---------------|
| `avi.cpp` | AVI header template (310 bytes), index entry format, DWORD alignment |
| `mjpeg2sd.cpp` | `saveFrame()` buffering pattern, `iSDbuffer` double-buffer, `RAMSIZE` |
| `appGlobals.h` | Constants: `RAMSIZE=8192`, `FB_BUFFERS=12`, `CHUNK_HDR=8`, `MAX_JPEG=524288` |
| `camera_pins.h` | Pin definitions (reference only — HAL abstracts this) |

## Further Considerations

### 1. SD Write Overhead

The existing `SdController` enforces CS (chip-select) timing delays (`DEACTIVATE_TIME_MS` + `ACTIVATE_TIME_MS`) per write operation. This adds ~2-4ms overhead per SD flush.

**Phase 1 impact:** Negligible. At 8KB buffer size and 10 FPS with VGA JPEGs (~15-30KB each), a flush happens every ~300-800ms — roughly 1-3 flushes per second. The 2-4ms overhead per flush is well under 1% of frame time.

**Phase 2 consideration:** At higher FPS (20+) or larger resolutions (HD), the flush frequency increases. If profiling reveals this as a bottleneck, add a "streaming write" mode to SdController that bypasses the CS state machine for bulk data. The state machine was designed for the pin-arbitration with LED on GPIO21 — video bulk writes may benefit from dedicated SD access without the arbitration overhead.

### 2. Resolution Sharing Between Photo and Video

The `[camera]` and `[video]` config sections both effectively set camera resolution. Two approaches:

**Option A — Separate configs, video wins during recording:**
- Photo resolution from `[camera]` section, video resolution from `[video]` section
- When recording starts, switch to video resolution
- When recording stops, restore photo resolution
- Pro: Each mode uses optimal settings. Con: Resolution switch causes a brief camera re-init.

**Option B — Shared resolution:**
- Single resolution setting used for both photo and video
- Simpler, no re-init needed. Con: May not be optimal for both modes.

**Recommendation:** Option A, implemented in VideoController. The camera controller already supports `deinit()` + `init()` with a new config, so resolution switching is straightforward. The re-init takes ~100ms which is acceptable at clip boundaries.

### 3. PSRAM-Less Operation

The AviWriter's frame index buffer grows with frame count (16 bytes per frame). For a 10-minute recording at 10 FPS = 6,000 frames × 16 bytes = 96KB. This fits in DRAM on ESP32S3.

However, without PSRAM:
- Camera frame buffers are limited to 1 (vs 12 with PSRAM), reducing capture throughput
- JPEG quality must be lower (larger quality number) to produce smaller frames
- The `CameraConfig` defaults already handle this (fb_count=1, quality=12 without PSRAM)

**Fallback strategy for the index:**
- With PSRAM: Buffer full index in PSRAM, write at close
- Without PSRAM: Use a frame-count-only approach — write index entries incrementally during recording (append to file before close), or store only the count and rebuild index on demand. The AVI format remains valid either way — just slower to seek.

### 4. Frame Pacing Jitter

Phase 1 uses time-based polling in `tick()`, which introduces timing jitter depending on:
- Main loop execution time (peripheral_manager.tick(), other housekeeping)
- SD flush duration (blocking write_file)
- Camera capture latency (~30-100ms depending on resolution)

**Expected jitter:** ±5-15ms at 10 FPS (100ms frame interval). This is visually imperceptible in playback and acceptable for security camera / monitoring use cases.

**When to upgrade:** If the project needs consistent frame rates at higher FPS (20+) or if streaming is added (Phase 2), migrate to hardware timer ISR + FreeRTOS task notification (matching the reference implementation's `frameISR` + `captureTask` pattern). The AviWriter and SdWriteBuffer modules are task-agnostic — they work identically in both models.

### 5. Large Recording Sessions

For long recording sessions (hours/days), additional considerations:

- **File size limits:** `max_file_size_mb` prevents unbounded files. Default 0 (no limit) is fine for short sessions; set to 200-500MB for extended recording.
- **Clip rotation:** Automatic clip splitting at duration/size boundaries keeps files manageable and provides natural recovery points if power is lost (only the current clip is lost).
- **SD card free space:** No automatic cleanup in Phase 1. A future enhancement could delete oldest clips when free space drops below a threshold (matching the reference implementation's `MAX_SD_FREE` behavior).
- **Index memory:** Very long clips (30+ minutes at 10 FPS = 18,000+ frames) consume ~288KB for the index. Ensure PSRAM is available, or use the incremental index write strategy.

### 6. Testing Strategy Emphasis

The existing test infrastructure (native host tests with mock HALs) is the project's greatest strength. Phase 1 adds three new test suites that should be written alongside (or even before) the implementation:

- **AviWriter tests** are purely format validation — no hardware, no timing. Use a byte array or temp file as the output target. Verify every byte of the AVI header.
- **VideoController tests** exercise state transitions and timing logic. Mock the camera (fixed-size fake JPEG data), mock time (controlled `now()` values), mock SD (no-op writes).
- **SdWriteBuffer tests** verify buffering math — fill with known data, assert correct flush boundaries and alignment.

TDD is strongly encouraged for the AviWriter, since the AVI format is byte-exact and any off-by-one in header offsets or DWORD alignment will produce unplayable files.
