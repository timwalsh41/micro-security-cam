# Phase 2: Network, Audio, and Streaming

## Overview

Layer WiFi connectivity, web-based control, live MJPEG streaming, audio recording, motion detection, and timelapse onto the standalone video recording foundation built in Phase 1. This phase transitions the architecture from cooperative polling to a FreeRTOS task model to support concurrent operations (recording + streaming + audio capture). It also introduces the first network-facing features: a web UI for configuration, remote control, live preview, and file browsing.

Phase 2 is organized into six sub-phases (2A–2F) with clear dependency ordering. Each sub-phase is independently shippable and testable.

## Background

Phase 1 delivered standalone MJPEG video recording to SD card using the existing cooperative `tick()`-based polling architecture. The camera controller grabs one JPEG frame at a time via `esp_camera_fb_get()`, the AviWriter wraps frames in an AVI container, and the SdWriteBuffer batches writes for SD performance.

Phase 2 addresses the remaining capabilities identified in the project roadmap:

- **WiFi connectivity** — required for web UI, streaming, and remote control
- **Web server** — configuration, file browsing, recording control, live preview
- **Live streaming** — MJPEG stream to web browsers in real time
- **Audio recording** — I2S/PDM microphone capture synchronized with video
- **Motion detection** — camera-based frame-differencing algorithm that auto-triggers recording
- **Timelapse** — low-FPS recording mode for long-duration capture

The reference implementation (ESP32-CAM_MJPEG2SD) uses FreeRTOS tasks with hardware timer ISRs, semaphores for frame sharing, and independent mic/stream tasks. Phase 2 adopts this architecture because concurrent consumers (recording + streaming + audio) cannot be serviced by a single-threaded polling loop without blocking one or more pipelines.

## Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| **Concurrency model** | FreeRTOS tasks (captureTask, sustainTask, micTask) | Required for simultaneous recording + streaming + audio without mutual blocking |
| **Frame pacing** | Hardware timer ISR with task notification | Precise frame rates (±0.1ms jitter) vs ±5-15ms polling jitter |
| **WiFi** | HAL abstraction + STA with AP fallback | Portable, testable, follows existing architecture |
| **Web server** | Arduino WebServer behind adapter interface | Built-in, lightweight, swappable later |
| **Streaming format** | HTTP multipart/x-mixed-replace (MJPEG) | Universally supported by browsers; no client-side JS needed |
| **Audio codec** | 16-bit PCM mono at 16kHz | Simple, no encoder dependency; AVI container handles it natively |
| **Motion detection** | Camera JPEG→RGB→96×96 downscale→pixel diff→ROI→threshold | Uses camera itself (no separate sensor); proven algorithm from reference; lightweight for ESP32 |
| **Feature management** | Runtime-only via `config.ini` | All features always compiled in; enabled/disabled at boot via config sections |
| **Task count** | 3 primary + 1–4 streaming | Capture (pri 6), Sustain (pri 5), Mic (pri 2), main loop for lifecycle |

## Architecture

### Task Topology

```
Hardware Timer ISR (Timer 3, 0.1ms resolution)
  Fires at configured FPS → vTaskNotifyGive(captureHandle)
         │ task notification
         ▼
captureTask (Priority 6, 4KB stack)
  • Waits on task notification from ISR
  • Captures camera frame via CameraController
  • Copies to streamBuffer[] if streaming active
  • Runs camera-based motion detection if enabled
  • Buffers frame via AviWriter + SdWriteBuffer if recording
  • Signals frameSemaphore[] for each active stream
         │
         ├──► streamBuffer[i] → frameSemaphore[i] → sustainTask[i] → HTTP
         ├──► AviWriter → SdWriteBuffer → PeripheralManager → SdController → SD card
         └──► MotionDetector → motionStatus flag

micTask (Priority 2, 4KB stack)
  • Waits on task notification from startAudio()
  • Reads I2S DMA buffer (1024 samples per read)
  • Applies gain amplification
  • Writes PCM to temporary WAV file on SD
  • Stops when finishAudio() sets flag

main.cpp loop()
  • PeripheralManager::tick() — LED + SD CS state machine
  • WebServer::handleClient() — HTTP request handling
  • Application lifecycle: start/stop recording, config reload
```

### Data Flow: Recording + Streaming + Audio

```
Camera ──► captureTask ──┬──► streamBuffer[i] ──► frameSemaphore[i] ──► sustainTask[i] ──► HTTP
                         ├──► AviWriter ──► SdWriteBuffer ──► PeripheralManager ──► SdController ──► SD card
                         └──► MotionDetector ──► motionStatus flag

I2S Mic ──► micTask ──► gain ──► WAV file ──► (at close) ──► AviWriter audio merge ──► SD card
```

### New HAL Interfaces

| HAL Interface | Responsibility | Key Methods |
|---------------|---------------|-------------|
| **WiFiHal** | WiFi mode, connect, status | `mode()`, `begin()`, `softAP()`, `status()`, `localIP()`, `softAPIP()`, `disconnect()` |
| **MicHal** | I2S microphone driver | `init(config)`, `start()`, `stop()`, `read(buffer, len)`, `deinit()` |
| **WebServerHal** | HTTP server lifecycle + request/response plumbing | `begin(port)`, `on(route, handler)`, `handleClient()`, `stop()` |
| **TimerHal** | Hardware timer ISR | `init(intervalUs, isr)`, `setFrequency()`, `start()`, `stop()` |

### Runtime Feature Management

All features are always compiled in. Subsystems that should not run are simply not initialized at boot — their `enabled` config flag causes the service to skip `init()` and remain dormant. This avoids `#ifdef` sprawl and ensures a single build artifact works for all configurations.

---

## Sub-Phases

### Phase 2A: WiFi Connectivity and Web Server Foundation

**Goal:** Connect to WiFi (or fall back to AP mode), serve a basic web UI for configuration, file browsing, and recording control.

**Depends on:** Phase 1

#### Step 2A.1: Add WiFi HAL and Service

**New files:**
- `include/hal/wifi_hal.h` — Abstract interface: `mode()`, `begin()`, `softAP()`, `status()`, `localIP()`, `softAPIP()`, `disconnect()`, `isConnected()`
- `include/hal/wifi_hal_impl.h` — ESP32 implementation wrapping Arduino WiFi library
- `include/wifi_service.h` / `src/wifi_service.cpp` — Service layer: STA with AP fallback, bounded timeout, IP logging

**Behavior:** Attempt STA connection for `wifiTimeoutSecs` (default 30s). If fails → start AP mode with configured credentials. Log final state.

#### Step 2A.2: Add WebServer HAL and Service

**New files:**
- `include/hal/web_server_hal.h` — Abstract adapter around HTTP server. Thin interface for lifecycle (`begin()`, `on()`, `handleClient()`, `stop()`) and request/response forwarding (method, URI, query args, headers, status codes, content types, text responses, chunked transfer). Exists primarily to enable native unit testing of route logic via MockWebServerHal.
- `include/hal/web_server_hal_impl.h` — Arduino `WebServer` adapter
- `include/web_server_service.h` / `src/web_server_service.cpp` — Route registration, handler logic, static file serving. This is where all application-level HTTP logic lives (auth checks, JSON responses, file listing, control commands).

**Initial routes:**

| Route | Method | Purpose |
|-------|--------|---------|
| `/` | GET | Index page (from SD `/data/` or built-in HTML) |
| `/web/*` | GET | Static files from SD `/data/` (HTML, JS, CSS, SVG, ICO) |
| `/status` | GET | JSON status (recording state, SD space, config) |
| `/control` | GET | Control commands (record on/off, still capture, reset) |
| `/update` | POST | Bulk config updates from web UI |

**Authentication:** Basic auth using `[web]` section credentials. Checked in every handler.

#### Step 2A.3: Web UI — Basic HTML Interface

**SD card files:** `/data/index.html`, `/data/common.js`

**Initial pages:**
- Status dashboard (recording state, SD space, WiFi IP, FPS)
- File browser (list AVI files with download links)
- Controls (start/stop recording, still capture)
- Config editor (view/edit config.ini values)

#### Step 2A.4: Update Startup Sequence

**Modify:** `src/main.cpp`

```
New boot order:
1-6. (existing: serial, PeripheralManager, LED, camera, SD, config)
7. Init VideoController
8. Init WiFiService ← NEW
9. Init WebServerService ← NEW
10. Start web server
11. Start recording (if configured)
```

**New loop addition:** `webServerService.handleClient()`

#### Step 2A.5: Tests

- `test_wifi_service`: STA success, STA timeout → AP fallback, disconnect
- `test_web_server_service`: Route registration, auth enforcement, status JSON, control commands
- Mocks: `MockWiFiHal`, `MockWebServerHal`

---

### Phase 2B: FreeRTOS Task Architecture Upgrade

**Goal:** Transition from cooperative polling to FreeRTOS tasks for the capture pipeline.

**Depends on:** Phase 2A

#### Step 2B.1: Add Timer HAL

**New files:** `include/hal/timer_hal.h`, `include/hal/timer_hal_impl.h`

Abstract interface: `init(intervalUs, isr)`, `setFrequency()`, `start()`, `stop()`

Implementation wraps ESP32 hardware timer (`timer_group`, `timer_isr_callback_add`).

#### Step 2B.2: Refactor VideoController into FreeRTOS CaptureTask

**Modify:** `include/video_controller.h`, `src/video_controller.cpp`

Replace `tick()` polling with a dedicated FreeRTOS task:

```cpp
static void captureTask(void* param) {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // Wait for ISR
    ctrl->onFrameCapture();                    // Capture + process
  }
}
```

Timer ISR signals the task at configured FPS:
```cpp
static void IRAM_ATTR frameISR() {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  vTaskNotifyGiveFromISR(captureHandle, &xHigherPriorityTaskWoken);
  if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}
```

**`onFrameCapture()` steps:**
1. Capture frame via CameraController
2. If streaming active → copy to `streamBuffer[i]`, give `frameSemaphore[i]`
3. If motion detection enabled → run MotionDetector on camera frame
4. If recording active → write via AviWriter + SdWriteBuffer
5. Release frame

#### Step 2B.3: Frame Buffer Allocation for Streaming

```cpp
uint8_t* streamBuffer[MAX_STREAMS];      // 512KB each, in PSRAM
size_t streamBufferSize[MAX_STREAMS];     // 0 = buffer free, >0 = frame ready
SemaphoreHandle_t frameSemaphore[MAX_STREAMS];  // Binary semaphores
```

**Capture → Stream synchronization:**
- Producer (captureTask): write frame → set size → give semaphore
- Consumer (sustainTask): take semaphore → read frame → clear size
- If buffer full (slow consumer), frame is skipped

#### Step 2B.4: Tests

- Frame notification → capture → buffer flow
- Semaphore give/take pattern
- Timer ISR simulation

---

### Phase 2C: Live MJPEG Streaming

**Goal:** Serve a live MJPEG stream to web browsers via HTTP multipart response.

**Depends on:** Phase 2B

#### Step 2C.1: Add /sustain Endpoint and Stream Service

**New files:** `include/stream_service.h`, `src/stream_service.cpp`

**HTTP response format:**
```
HTTP/1.1 200 OK
Content-Type: multipart/x-mixed-replace;boundary=--myboundary

--myboundary
Content-Type: image/jpeg
Content-Length: 54321

[JPEG data]
--myboundary
Content-Type: image/jpeg
...
```

**sustainTask (one per concurrent stream):**
```cpp
while (isStreaming[taskNum]) {
  if (xSemaphoreTake(frameSemaphore[taskNum], pdMS_TO_TICKS(1200))) {
    httpd_resp_send_chunk(req, header, headerLen);
    httpd_resp_send_chunk(req, streamBuffer[taskNum], streamBufferSize[taskNum]);
    httpd_resp_send_chunk(req, boundary, boundaryLen);
    streamBufferSize[taskNum] = 0;
  } else {
    break;  // Timeout → terminate stream
  }
}
```

#### Step 2C.2: Configuration

```ini
[web]
max_streams=4     ; 1-4 concurrent stream slots (512KB PSRAM each)
```

#### Step 2C.3: Tests

- Stream lifecycle: connect → frame delivery → disconnect
- Timeout handling (1200ms no frame → terminate)
- Concurrent streams
- Buffer overflow (slow consumer → frame skip, not block)

---

### Phase 2D: Audio Recording

**Goal:** Record audio from the XIAO ESP32S3's built-in PDM microphone, synchronized with video, merged into the AVI file.

**Depends on:** Phase 2B; can develop in parallel with Phase 2C

#### Step 2D.1: Add Microphone HAL

**New files:** `include/hal/mic_hal.h`, `include/hal/mic_hal_impl.h`

```cpp
struct MicConfig {
  int sampleRate = 16000;
  int bitsPerSample = 16;
  int channels = 1;           // Mono
  int dmaBufCount = 4;
  int dmaBufLen = 1024;
  int wsPin = 42;             // XIAO ESP32S3 PDM CLK
  int sdPin = 41;             // XIAO ESP32S3 PDM DAT
  bool usePdm = true;
};
```

Implementation wraps `i2s_driver_install()`, `i2s_set_pin()`, `i2s_read()`. PDM enabled via `I2S_MODE_PDM`.

#### Step 2D.2: Add MicService

**New files:** `include/mic_service.h`, `src/mic_service.cpp`

**API:**
- `startCapture()` — Open `/current.wav`, write header template, wake micTask
- `stopCapture()` — Signal stop, wait for task, finalize WAV header, close file
- `hasWavFile()` / `mergeWavIntoAvi()` — For AVI integration at clip close

**micTask loop:**
1. Read 1024 samples from I2S (`i2s_read()`)
2. Apply gain: `sample * gain` (constrained to int16_t range)
3. Write to `/current.wav` on SD
4. Repeat until `m_doCapture == false`

**WAV header (44 bytes):** PCM format, mono, 16kHz, 16-bit. Updated with actual sizes at finalize.

**Board auto-configuration:**
```cpp
#if CAMERA_MODEL_XIAO_ESP32S3
  PDM: wsPin=42, sdPin=41, usePdm=true
#elif CAMERA_MODEL_ESP32S3_EYE
  I2S: sckPin=41, wsPin=42, sdPin=2, usePdm=false
#endif
```

#### Step 2D.3: Audio–AVI Merge

At clip close, after all video frames are written:
1. Update AVI header: stream count = 2 (video + audio)
2. Add audio stream header (strh: PCM, 16kHz, mono)
3. Write audio chunks: `[01wb][size][PCM data]`
4. Add audio index entries: `[01wb][0000][offset][audSize]`
5. Delete `/current.wav`

#### Step 2D.4: Configuration

```ini
[video]
audio_enabled=true
mic_gain=2         ; 0=off, 1=no gain, 2-4=amplification
```

#### Step 2D.5: Tests

- start/stop lifecycle, WAV header accuracy
- I2S read with MockMicHal
- WAV→AVI merge produces valid dual-stream AVI
- WAV temp file cleanup

---

### Phase 2E: Motion Detection

**Goal:** Detect motion via camera-based frame differencing and auto-trigger recording. Uses the camera itself — no separate PIR or external motion sensor.

**Depends on:** Phase 2B (runs inline in captureTask)

#### Step 2E.1: Add MotionDetector Module

**New files:** `include/motion_detector.h`, `src/motion_detector.cpp`

**Algorithm (ported from `motionDetect.cpp`):**

1. **JPEG → RGB/grayscale:** Decode current camera JPEG frame using `fmt2rgb888()` or `fmt2grayscale()` from the esp32-camera library. This operates on the raw JPEG data already in the frame buffer — no second camera capture needed.
2. **Downscale to 96×96:** Bilinear interpolation from original resolution to fixed 96×96 bitmap. Ensures consistent processing regardless of camera resolution.
3. **Pixel-by-pixel differencing:** Compare current 96×96 against previous frame's 96×96. For each pixel: `|curr_intensity - prev_intensity| > changeThreshold`.
4. **ROI (Region of Interest) filtering:** Divide image into `numBands` horizontal bands (default 10). Only count changes in bands `startBand` (3) through `endBand` (8). Excludes sky (top) and ground (bottom) which produce spurious motion from clouds, shadows, vegetation.
5. **Threshold comparison:** `changeCount > ROI_pixels × (11 - sensitivity) / 100`. Sensitivity 0-10 (higher = less sensitive).
6. **Confirmation sequence:** Require `confirmFrames` (default 5) consecutive above-threshold frames to confirm motion START. Require frames below threshold plus `stopDelaySecs` delay to confirm STOP.

**API:**
```cpp
class MotionDetector {
public:
  bool init(const MotionConfig& config);
  MotionResult processFrame(const uint8_t* jpegData, size_t jpegLen);
  void reset();
  void setSensitivity(float val);     // 0-10
  void setROI(int startBand, int endBand);
  void setNightSwitch(uint8_t threshold);
};

struct MotionResult {
  bool motionDetected;       // START event
  bool motionStopped;        // STOP event
  float lightLevel;          // 0-100%
  bool isNight;
  uint32_t changePixelCount; // Raw count for diagnostics
};
```

#### Step 2E.2: Night/Day Detection

Hysteretic light level measurement prevents day/night flicker:

```cpp
float lightLevel = (avgIntensity * 100.0f) / 255.0f;

// Switch at nightSwitch% (default 20%), require sustained darkness
if (!isNight && lightLevel < nightSwitch) {
  nightFrames++;
  if (nightFrames >= detectNightFrames) isNight = true;
} else if (isNight && lightLevel > nightSwitch + 5) {
  nightFrames = 0;
  isNight = false;
} else {
  nightFrames = 0;
}
```

Night mode can trigger different recording behavior (e.g., lower FPS, different quality).

#### Step 2E.3: Integration with captureTask

```
if motion detected && !recording → auto-start recording
if motion stopped && recording → wait stopDelaySecs → auto-stop recording
```

Runs inline in `captureTask::onFrameCapture()` after the camera frame is acquired but before release. The JPEG decode + 96×96 comparison is lightweight enough to execute within the frame interval.

#### Step 2E.4: Memory Consideration

JPEG decode to RGB needs significant memory (VGA: 921KB, HD: 2.7MB). **Default to grayscale** (1 byte/pixel) for motion detection — sufficient for intensity-based change detection and uses 3× less memory. VGA grayscale: 307KB. This is acceptable for the 96×96 comparison even though the full-resolution decode buffer is larger.

#### Step 2E.5: Configuration

```ini
[motion]
enabled=false
sensitivity=6          ; 0-10 (higher = less sensitive)
start_band=3           ; ROI start (1=top, 10=bottom)
end_band=8             ; ROI end (inclusive)
change_threshold=15    ; Min pixel intensity delta (0-255)
confirm_frames=5       ; Consecutive frames to confirm motion
stop_delay_secs=2      ; Post-motion recording delay
night_switch=20        ; Light level % threshold for night mode
```

#### Step 2E.6: Tests

- Static scene → no motion; moving object → START; stops → STOP after delay
- ROI filtering: motion in excluded bands ignored
- Night/day hysteresis: transitions only after sustained darkness
- Threshold calibration: known pixel differences produce expected results
- Mock: pre-built 96×96 pixel arrays with controlled differences

---

### Phase 2F: Timelapse Mode

**Goal:** Record at very low frame rates (1 FPS to 1 frame/hour) for long-duration capture.

**Depends on:** Phase 1; independent of Phases 2A–2E

#### Step 2F.1: Recording Mode

```cpp
enum class RecordingMode { VIDEO, TIMELAPSE };
```

**Configuration:**
```ini
[timelapse]
enabled=false
interval_secs=10       ; 1-3600 seconds between frames
max_frames=0           ; 0 = unlimited
```

#### Step 2F.2: Timer Handling for Long Intervals

Hardware timer fires at 1 Hz; software counter divides for intervals > 1 second:
```cpp
if (recordingMode == TIMELAPSE) {
  timelapseCounter++;
  if (timelapseCounter >= timelapseIntervalSecs) {
    timelapseCounter = 0;
    captureAndWriteFrame();
  }
}
```

#### Step 2F.3: Extended File Naming

```
20260705_143022_640x480_TL10s.avi
                      ^^^^^^
                      TL = timelapse, 10s = interval
```

#### Step 2F.4: Tests

- Frame interval accuracy, clip rotation, LED feedback

---

## Configuration Reference

### Complete config.ini for Phase 2

```ini
[wifi]
ssid=MyHomeNetwork
password=secret123
timeout_secs=30

[ap]
ssid=ESP32-Camera
password=admin123

[web]
port=80
password=webpass
max_streams=4

[camera]
capture_interval_ms=20000

[video]
fps=10
resolution=VGA
clip_duration_s=60
max_file_size_mb=0
jpeg_quality=10
audio_enabled=true
mic_gain=2
recording_mode=video

[motion]
enabled=false
sensitivity=6
start_band=3
end_band=8
change_threshold=15
confirm_frames=5
stop_delay_secs=2
night_switch=20

[timelapse]
enabled=false
interval_secs=10
max_frames=0
```

## New Files Summary

| File | Purpose | Sub-phase |
|------|---------|-----------|
| `include/hal/wifi_hal.h` | WiFi HAL interface | 2A |
| `include/hal/wifi_hal_impl.h` | ESP32 WiFi HAL implementation | 2A |
| `include/wifi_service.h` | WiFi service (STA + AP fallback) | 2A |
| `src/wifi_service.cpp` | WiFi service implementation | 2A |
| `include/hal/web_server_hal.h` | Web server HAL interface (lifecycle + request/response plumbing) | 2A |
| `include/hal/web_server_hal_impl.h` | Arduino WebServer adapter | 2A |
| `include/web_server_service.h` | Web server service (routes, handlers, auth) | 2A |
| `src/web_server_service.cpp` | Web server service implementation | 2A |
| `include/hal/timer_hal.h` | Hardware timer HAL interface | 2B |
| `include/hal/timer_hal_impl.h` | ESP32 timer HAL implementation | 2B |
| `include/stream_service.h` | MJPEG streaming service | 2C |
| `src/stream_service.cpp` | Stream task + HTTP multipart handler | 2C |
| `include/hal/mic_hal.h` | Microphone HAL interface | 2D |
| `include/hal/mic_hal_impl.h` | I2S/PDM microphone implementation | 2D |
| `include/mic_service.h` | Microphone service (WAV recording) | 2D |
| `src/mic_service.cpp` | Mic task + WAV file management | 2D |
| `include/motion_detector.h` | Camera-based motion detection module | 2E |
| `src/motion_detector.cpp` | Frame differencing + ROI algorithm | 2E |

## Modified Files Summary

| File | Changes | Sub-phase |
|------|---------|-----------|
| `include/video_controller.h` | FreeRTOS task creation, ISR timer, recording mode enum | 2B |
| `src/video_controller.cpp` | Replace tick() with captureTask, integrate motion/audio/streaming | 2B |
| `include/ini_parser.h` | Add [motion], [timelapse], [video.audio] config sections | 2A–2F |
| `src/ini_parser.cpp` | Parse new config sections | 2A–2F |
| `include/config_service.h` | Add MotionConfig, AudioConfig, TimelapseConfig structs | 2A–2F |
| `src/config_service.cpp` | Log new config sections at startup | 2A–2F |
| `src/main.cpp` | Init WiFi, WebServer; update loop for FreeRTOS tasks | 2A/2B |
| `include/avi_writer.h` | Add audio stream support (stream count=2, 01wb chunks) | 2D |
| `src/avi_writer.cpp` | Audio stream header, audio chunks, dual-stream index | 2D |
| `docs/SYSTEM_DESIGN.md` | Document full architecture, all services, task topology | All |
| `README.md` | Document WiFi, web UI, streaming, audio, motion, timelapse | All |
| `TODO.md` | Mark Phase 2 items complete | All |

## Verification

### Phase 2A — WiFi and Web Server

| # | Test | Expected Result |
|---|------|-----------------|
| 1 | Power on with known WiFi | Connects to STA, logs IP |
| 2 | Power on without WiFi | Starts AP mode, SSID visible |
| 3 | Browse to IP | Web UI loads with status dashboard |
| 4 | Click "Start Recording" | Recording begins, status updates |
| 5 | Browse file list | AVI files listed with download links |
| 6 | Download AVI | File downloads and plays in VLC |
| 7 | Wrong password | Returns 401 Unauthorized |
| 8 | Config change via web | Values persist in config.ini |

### Phase 2B — FreeRTOS Architecture

| # | Test | Expected Result |
|---|------|-----------------|
| 9 | Recording at FPS | Frame count matches expected ±1 |
| 10 | Frame rate stability | ISR-driven capture < 1ms jitter |
| 11 | Memory stability | 10+ sequential clips, no leaks |
| 12 | Concurrent tasks | captureTask + webServer both responsive |

### Phase 2C — Live Streaming

| # | Test | Expected Result |
|---|------|-----------------|
| 13 | Stream in browser | MJPEG visible in browser |
| 14 | Stream + record | Both operate simultaneously |
| 15 | Multiple browsers | Up to max_streams concurrent |
| 16 | Stream disconnect | Resources freed cleanly |
| 17 | Stream timeout | 1200ms no frame → terminates |

### Phase 2D — Audio Recording

| # | Test | Expected Result |
|---|------|-----------------|
| 18 | Record with audio | AVI plays with synchronized audio |
| 19 | Audio gain | Gain=2 produces audible amplification |
| 20 | Audio disabled | audio_enabled=false → video-only AVI |
| 21 | WAV cleanup | No /current.wav after recording stops |
| 22 | AVI ffprobe | 2 streams, correct sample rate |

### Phase 2E — Motion Detection

| # | Test | Expected Result |
|---|------|-----------------|
| 23 | Motion triggers recording | Moving object in front of camera starts recording |
| 24 | Motion stops recording | No motion for stopDelaySecs → stops |
| 25 | ROI filtering | Motion in excluded bands ignored |
| 26 | Night detection | Sustained darkness → isNight true |
| 27 | Sensitivity tuning | Lower value → more sensitive |
| 28 | No false triggers | Static scene 5 minutes → no trigger |

### Phase 2F — Timelapse

| # | Test | Expected Result |
|---|------|-----------------|
| 29 | 10s interval | One frame every 10 seconds |
| 30 | Clip rotation | Clips split at duration |
| 31 | Long duration | 1-hour timelapse valid AVI |
| 32 | LED feedback | Blink per frame |

### Build Verification

| # | Test | Expected Result |
|---|------|-----------------|
| 33 | Full build | Clean compilation |
| 34 | Build with all features enabled | Clean compilation |
| 35 | Native tests | All pass |
| 36 | Flash/PSRAM usage | Fits in 16MB flash + 8MB PSRAM |

## Further Considerations

### 1. PSRAM Budget

| Component | PSRAM |
|-----------|-------|
| Camera buffers (12 × VGA) | ~3.6 MB |
| Stream buffers (4 × 512KB) | ~2.0 MB |
| AVI index (10min × 10FPS) | ~96 KB |
| SdWriteBuffer | 16 KB |
| Motion grayscale buffer (VGA) | 307 KB |
| Mic + task stacks | 24 KB |
| TLS heap | 64 KB |
| **Total** | **~6.1 MB** |

XIAO ESP32S3 has 8MB PSRAM (~1.9MB headroom). At HD resolution, camera buffers alone would exceed PSRAM. **Recommendation:** Limit to VGA/SVGA when all features active simultaneously. Reduce `max_streams` or disable motion detection to reclaim memory.

### 2. FreeRTOS Task Stack Sizes

Reference uses 4KB for all tasks. Watch for:
- **captureTask:** `esp_jpg_decode()` for motion detection uses stack. Increase to 8KB if stack overflow detected.
- **sustainTask:** HTTP chunked encoding is stack-friendly. 4KB sufficient.
- **micTask:** I2S read + gain is stack-friendly. 4KB sufficient.

Enable `CONFIG_FREERTOS_CHECK_STACK_OVERFLOW=2` in debug builds to catch stack issues early.

### 3. WiFi Reconnection

Phase 2A implements single STA attempt at boot. For long deployments:
- Periodic reconnect with exponential backoff (30s, 60s, 120s)
- AP mode persists if STA never reconnects
- Recording continues regardless of WiFi state — WiFi loss does not interrupt recording
- `WiFiService::tick()` in main loop checks `WiFi.status()` periodically

### 4. Web UI Hosting

**Option A (recommended) — Files on SD card:** `/data/index.html` etc. Easy to update without reflashing firmware. Matches reference implementation. Fallback to built-in HTML if SD card data is missing.

**Option B — Embedded in flash:** Compact, no SD dependency. Requires firmware reflash to update. Better for minimal UIs.

**Recommendation:** Option A for flexibility, with Option B as fallback.

### 5. Security

- Basic auth on all endpoints via `[web]` password
- No web server if no password configured (or AP-only captive portal for initial setup)
- AP mode provides network isolation
- No UPnP/mDNS discovery by default — users must know the IP

### 6. Motion Detection at High Resolutions

JPEG decode is the bottleneck. Estimated decode times:
- QVGA (320×240): ~10ms
- VGA (640×480): ~30ms
- HD (1280×720): ~80ms

Reference notes motion detection fails above FHD with OV5640. For OV2640 on XIAO ESP32S3, VGA is the practical maximum for real-time motion detection at video frame rates.

**Recommendation:** Always downsample to 96×96 for comparison, but decode at native resolution. If decode time causes frame drops, use lower capture resolution for motion-enabled recording.

### 7. Testing Concurrent Tasks

- **Native tests** validate individual modules (AviWriter, MotionDetector, ConfigService) with mocks — fast and deterministic
- **Integration tests** on ESP32 hardware validate task interactions: captureTask → streamBuffer → sustainTask flow, micTask WAV output, motion → recording trigger
- **Stress tests:** 1+ hour runs with all features enabled to validate memory stability, file system integrity, and WiFi reliability
- **Mock strategy:** TimerHal and MicHal are mockable for native tests. FreeRTOS task synchronization is tested on hardware only (no native FreeRTOS mock — too complex for limited benefit)

### 8. Recommended Delivery Order

1. **2A (WiFi + Web)** → Immediately usable for remote control and configuration
2. **2F (Timelapse)** → Independent of 2A–2E, can deliver early if timelapse is high priority
3. **2B (FreeRTOS)** → Foundation for 2C and 2D; must be delivered before streaming/audio
4. **2C (Streaming)** → High-visibility feature, depends on 2B
5. **2D (Audio)** → Can develop in parallel with 2C, depends on 2B
6. **2E (Motion)** → Lowest priority unless camera is unattended; depends on 2B

---

## Future Extensions

The following are **not** part of this plan but are candidates for follow-up work.

### Live Audio Streaming

Phase 2 records audio to a temporary WAV file and merges it into the AVI at clip close. Audio is only heard during AVI playback, not live in the browser. A future extension could add a real-time audio stream alongside the MJPEG video stream so users hear microphone input while watching the live preview.

**Approach:** A lightweight WebSocket connection carries PCM audio chunks (or Opus-encoded for lower bandwidth) from the micTask to the browser. The web UI includes a small JavaScript player that decodes and plays the audio stream in sync with the MJPEG `<img>` updates. This would be a new sub-phase (e.g., 2G) depending on Phase 2B (FreeRTOS tasks) and Phase 2A (WiFi + web server).

**Considerations:**
- Opus encoding (via `esp-opus-encoder`) would reduce bandwidth ~10× over raw PCM but adds CPU and flash cost
- WebSocket framing is simpler than a second HTTP multipart stream and supports binary data natively
- Browser autoplay policies require user interaction (button click) before audio can play
- Latency budget: target < 200ms end-to-end (I2S read → encode → network → decode → playback) to feel "live"
- The existing micTask already reads I2S samples; a WebSocket writer could fan out from the same source without a second hardware capture
