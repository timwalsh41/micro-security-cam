#include <Arduino.h>
#include <unity.h>
#include "hal/camera_hal_impl.h"
#include "hal/sd_card_hal_impl.h"
#include "hal/timer_hal_impl.h"
#include "hal/serial_hal_impl.h"
#include "hal/serial_log_backend.h"
#include "hal/gpio_hal.h"
#include "camera_controller.h"
#include "video_controller.h"
#include "capture_scheduler_esp32.h"
#include "time_service.h"
#include "logging_service.h"
#include "sd_controller.h"
#include "ini_parser.h"
#include "time_formatter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include <cstring>

/**
 * @file test_video_controller_esp32.cpp
 * @brief End-to-end clip integrity test on real hardware.
 *
 * Records a short AVI clip through the full stack (camera HAL →
 * CameraController → VideoController → SdController → real SD card),
 * then reads the clip back and validates the AVI structure: RIFF
 * magic, header-patched size consistency, and the presence of the
 * index chunk. Ignored when no SD card, camera, or PSRAM is
 * available.
 */

/** Recording interval at 5 fps. */
static constexpr uint16_t RECORD_FPS = 5;

/** How long to record, in milliseconds. */
static constexpr uint32_t RECORD_DURATION_MS = 2000;

/** Upper bound on clip size read back (PSRAM-backed). */
static constexpr size_t MAX_CLIP_BYTES = 1024u * 1024u;

/**
 * @brief Fixture wiring the full recording stack with real HALs.
 */
struct RecordFixture
{
    hal::SerialHalImpl serial_hal;
    hal::SerialLogBackend log_backend;
    RealTimeService time;
    LoggingService logger;
    hal::SdCardHalImpl sd_hal;
    SdController sd_ctrl;
    hal::CameraHalImpl camera_hal;
    CameraController camera_ctrl;
    hal::TimerHalImpl timer_hal;
    CaptureSchedulerEsp32 scheduler;
    VideoController video;

    RecordFixture()
        : serial_hal(115200)
        , log_backend(serial_hal)
        , logger(log_backend, time)
        , sd_hal(hal::SD_CS_PIN)
        , sd_ctrl(time, sd_hal)
        , camera_ctrl(camera_hal)
        , scheduler(timer_hal)
        , video(camera_ctrl, sd_ctrl, time, logger, scheduler)
    {
    }
};

/**
 * @brief Builds the clip path exactly as VideoController::open_clip() does.
 *
 * @param unix_ts   Timestamp used for the file name.
 * @param suffix    Serial collision suffix (0 = none).
 * @param width     Video width.
 * @param height    Video height.
 * @param fps       Recording frame rate.
 * @param out       Output buffer.
 * @param out_size  Size of the output buffer.
 */
static void build_clip_path(uint32_t unix_ts, uint32_t suffix,
                            uint16_t width, uint16_t height, uint16_t fps,
                            char* out, size_t out_size)
{
    char date[9];
    char time_str[7];
    TimeFormatter::format_date(unix_ts, date, sizeof(date));
    TimeFormatter::format_time(unix_ts, time_str, sizeof(time_str));

    char base[64];
    snprintf(base, sizeof(base), "/%s/%s_%s_%ux%u_%ufps.avi",
             date, date, time_str, width, height, fps);

    if (suffix == 0)
    {
        snprintf(out, out_size, "%s", base);
        return;
    }

    const char* dot = strrchr(base, '.');
    if (dot != nullptr)
    {
        snprintf(out, out_size, "%.*s_%u%s",
                 static_cast<int>(dot - base), base, suffix, dot);
    }
    else
    {
        snprintf(out, out_size, "%s_%u", base, suffix);
    }
}

/**
 * @brief Locates the clip file written by the recording.
 *
 * The timestamp is captured before and after recording; the clip was
 * named with one of these two seconds, possibly with a serial suffix
 * from a same-second collision with an earlier file.
 *
 * @param fixture Fixture whose SD HAL to query.
 * @param t0      Timestamp captured before start_recording().
 * @param width   Video width.
 * @param height  Video height.
 * @param fps     Recording frame rate.
 * @param out     Buffer for the found path.
 * @param out_size Size of the output buffer.
 * @return true if a clip file was found.
 */
static bool find_clip(RecordFixture& fixture, uint32_t t0,
                      uint16_t width, uint16_t height, uint16_t fps,
                      char* out, size_t out_size)
{
    for (uint32_t ts = t0; ts <= t0 + 1; ts++)
    {
        for (uint32_t suffix = 0; suffix <= 5; suffix++)
        {
            build_clip_path(ts, suffix, width, height, fps, out, out_size);
            if (fixture.sd_hal.exists(out))
            {
                return true;
            }
        }
    }
    return false;
}

/**
 * @brief Reads a clip fully into a PSRAM buffer.
 *
 * @param fixture Fixture whose SD HAL to read from.
 * @param path    Clip path on the card.
 * @param buf     Destination buffer (MAX_CLIP_BYTES).
 * @return Number of bytes read.
 */
static size_t read_clip(RecordFixture& fixture, const char* path, uint8_t* buf)
{
    size_t total = 0;
    if (fixture.sd_hal.open(path, hal::FileOpenMode::READ))
    {
        while (total < MAX_CLIP_BYTES)
        {
            size_t n = fixture.sd_hal.read(buf + total, 4096);
            if (n == 0)
            {
                break;
            }
            total += n;
        }
        fixture.sd_hal.close();
    }
    return total;
}

/**
 * @brief Records a short clip and validates the resulting AVI file.
 */
void test_recorded_clip_is_valid_avi()
{
    RecordFixture f;

    // SD card required
    if (f.sd_ctrl.init_card() != hal::SdInitResult::SUCCESS)
    {
        TEST_IGNORE_MESSAGE("No SD card present");
    }

    // Camera required
    CameraImageConfig img_cfg;
    img_cfg.resolution = "VGA";
    img_cfg.jpeg_quality = 10;
    ResolvedCameraParams params = build_camera_config(img_cfg, f.logger);
    if (f.camera_ctrl.init(params) != hal::CameraInitResult::SUCCESS)
    {
        TEST_IGNORE_MESSAGE("Camera not available");
    }

    // Record for ~2 seconds at 5 fps
    const ResolvedCameraParams& applied = f.camera_ctrl.get_params();
    ConfigData::VideoConfig video_cfg;
    video_cfg.fps = RECORD_FPS;
    video_cfg.clip_duration_s = 60;
    video_cfg.max_file_size_mb = 0;
    video_cfg.jpeg_quality = 10;

    TEST_ASSERT_TRUE(f.video.init(video_cfg, applied.width, applied.height));

    uint32_t t0 = f.time.unix_now();
    TEST_ASSERT_TRUE(f.video.start_recording());
    vTaskDelay(pdMS_TO_TICKS(RECORD_DURATION_MS));
    const uint32_t recorded = f.video.frames_recorded();
    f.video.stop_recording();

    TEST_ASSERT_FALSE(f.video.is_recording());
    TEST_ASSERT_FALSE(f.video.is_streaming());
    TEST_ASSERT_GREATER_OR_EQUAL(3u, recorded);

    // Read the clip back
    char clip_path[64];
    TEST_ASSERT_TRUE(find_clip(f, t0, applied.width, applied.height,
                               RECORD_FPS, clip_path, sizeof(clip_path)));

    uint8_t* buf = static_cast<uint8_t*>(
        heap_caps_malloc(MAX_CLIP_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buf == nullptr)
    {
        TEST_IGNORE_MESSAGE("PSRAM allocation failed");
    }

    size_t size = read_clip(f, clip_path, buf);
    heap_caps_free(buf);
    TEST_ASSERT_GREATER_OR_EQUAL(224u, size);

    // RIFF magic
    TEST_ASSERT_EQUAL_UINT8('R', buf[0]);
    TEST_ASSERT_EQUAL_UINT8('I', buf[1]);
    TEST_ASSERT_EQUAL_UINT8('F', buf[2]);
    TEST_ASSERT_EQUAL_UINT8('F', buf[3]);

    // Header-patched RIFF size must cover the whole file
    uint32_t riff_size = static_cast<uint32_t>(buf[4])
                         | (static_cast<uint32_t>(buf[5]) << 8)
                         | (static_cast<uint32_t>(buf[6]) << 16)
                         | (static_cast<uint32_t>(buf[7]) << 24);
    TEST_ASSERT_EQUAL_UINT32(size, static_cast<size_t>(riff_size) + 8);

    // Index chunk present after header + movi data
    bool found_idx1 = false;
    for (size_t i = 0; i + 4 <= size; i++)
    {
        if (buf[i] == 'i' && buf[i + 1] == 'd'
            && buf[i + 2] == 'x' && buf[i + 3] == '1')
        {
            found_idx1 = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(found_idx1);
}

// ============================================================
// Test runner (Arduino entry point)
// ============================================================

/**
 * @brief Runs all video controller tests once.
 *
 * @return Unity result code.
 */
int runUnityTests()
{
    UNITY_BEGIN();
    RUN_TEST(test_recorded_clip_is_valid_avi);
    return UNITY_END();
}

/**
 * @brief Arduino entry point; runs the Unity suite.
 */
void setup()
{
    delay(2000);
    runUnityTests();
}

/**
 * @brief Arduino idle loop; unused.
 */
void loop() {}
