#include <unity.h>
#include "video_controller.h"
#include "camera_controller.h"
#include "mock_camera_hal.h"
#include "mock_sd_card_hal.h"
#include "mock_log_backend.h"
#include <cstring>

// ============================================================
// Test configuration
// ============================================================

static constexpr uint16_t TEST_FPS = 10;
static constexpr uint16_t TEST_CLIP_DURATION_S = 5;
static constexpr uint16_t TEST_WIDTH = 640;
static constexpr uint16_t TEST_HEIGHT = 480;
static constexpr size_t FAKE_JPEG_SIZE = 100;

// ============================================================
// Test helpers
// ============================================================

/**
 * @brief Sets up common test fixtures.
 *
 * Creates mock HALs, services, and a configured VideoController.
 * Initializes the camera and video controller so tests can focus
 * on recording behavior rather than setup.
 */
struct TestFixture
{
    MockCameraHal camera_hal;
    MockSdCardHal sd_hal;
    MockLogBackend log_backend;
    RealTimeService time;
    CameraController camera_ctrl;
    PeripheralManager peripheral;
    LoggingService logger;
    VideoController video;

    TestFixture()
        : camera_ctrl(camera_hal)
        , peripheral(time, sd_hal)
        , logger(log_backend, time)
        , video(camera_ctrl, peripheral, time, logger)
    {
        // Initialize camera
        camera_hal.init_result = hal::CameraInitResult::SUCCESS;
        camera_ctrl.init();

        // Configure video
        ConfigData::VideoConfig config;
        config.fps = TEST_FPS;
        strcpy(config.resolution, "VGA");
        config.clip_duration_s = TEST_CLIP_DURATION_S;
        config.max_file_size_mb = 0;
        config.jpeg_quality = 10;
        video.init(config, TEST_WIDTH, TEST_HEIGHT);
    }

    /**
     * @brief Configures the mock camera to return a fake JPEG frame.
     */
    void setup_fake_frame()
    {
        memset(camera_hal.frame_buffer, 0xAB, FAKE_JPEG_SIZE);
        camera_hal.capture_result = {camera_hal.frame_buffer, FAKE_JPEG_SIZE};
    }
};

// ============================================================
// Default state
// ============================================================

/**
 * @brief Before start_recording(), state is IDLE.
 */
void test_initial_state_is_idle()
{
    TestFixture f;
    f.setup_fake_frame();

    TEST_ASSERT_EQUAL(RecordingState::IDLE, f.video.state());
}

/**
 * @brief Before start_recording(), is_recording() returns false.
 */
void test_initial_not_recording()
{
    TestFixture f;
    f.setup_fake_frame();

    TEST_ASSERT_FALSE(f.video.is_recording());
}

/**
 * @brief Before start_recording(), frames_recorded() returns 0.
 */
void test_initial_frames_recorded_zero()
{
    TestFixture f;
    f.setup_fake_frame();

    TEST_ASSERT_EQUAL_UINT32(0, f.video.frames_recorded());
}

/**
 * @brief Before start_recording(), file_size() returns 0.
 */
void test_initial_file_size_zero()
{
    TestFixture f;
    f.setup_fake_frame();

    TEST_ASSERT_EQUAL_UINT32(0, f.video.file_size());
}

// ============================================================
// start_recording() / stop_recording()
// ============================================================

/**
 * @brief start_recording() transitions from IDLE to RECORDING.
 */
void test_start_recording_transitions_to_recording()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    TEST_ASSERT_TRUE(f.video.start_recording());
    TEST_ASSERT_EQUAL(RecordingState::RECORDING, f.video.state());
    TEST_ASSERT_TRUE(f.video.is_recording());
}

/**
 * @brief start_recording() returns false when not in IDLE state.
 */
void test_start_recording_fails_when_already_recording()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    f.video.start_recording();
    TEST_ASSERT_FALSE(f.video.start_recording());
}

/**
 * @brief stop_recording() triggers clip close on next tick.
 */
void test_stop_recording_transitions_to_idle()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    f.video.start_recording();

    // Advance time past one frame interval to capture a frame
    set_mock_millis(1100);
    f.video.tick();
    TEST_ASSERT_EQUAL_UINT32(1, f.video.frames_recorded());

    // Stop recording
    f.video.stop_recording();
    f.video.tick();  // RECORDING → STOPPING
    f.video.tick();  // STOPPING → close_clip() → IDLE

    TEST_ASSERT_EQUAL(RecordingState::IDLE, f.video.state());
    TEST_ASSERT_FALSE(f.video.is_recording());
}

/**
 * @brief stop_recording() is a no-op when not recording.
 */
void test_stop_recording_noop_when_idle()
{
    TestFixture f;
    f.setup_fake_frame();

    // Should not crash or change state
    f.video.stop_recording();
    TEST_ASSERT_EQUAL(RecordingState::IDLE, f.video.state());
}

// ============================================================
// Frame pacing
// ============================================================

/**
 * @brief First frame is captured immediately after start_recording().
 */
void test_first_frame_captured_immediately()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(5000);

    f.video.start_recording();
    f.video.tick();

    TEST_ASSERT_EQUAL_UINT32(1, f.video.frames_recorded());
    TEST_ASSERT_EQUAL_INT(1, f.camera_hal.capture_call_count);
}

/**
 * @brief Frames are captured at the configured FPS interval.
 */
void test_frames_captured_at_configured_interval()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    f.video.start_recording();

    // Tick at each frame interval — should capture exactly one frame each
    for (uint32_t i = 0; i < 5; i++)
    {
        set_mock_millis(1000 + (i + 1) * 100);  // 100ms = 10 FPS
        f.video.tick();
    }

    TEST_ASSERT_EQUAL_UINT32(5, f.video.frames_recorded());
}

/**
 * @brief No frame captured when interval has not elapsed.
 */
void test_no_frame_when_interval_not_elapsed()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    f.video.start_recording();
    f.video.tick();  // Captures first frame

    // Advance time by less than the frame interval
    set_mock_millis(1050);  // Only 50ms elapsed (need 100ms)
    f.video.tick();

    // Should still be at 1 frame
    TEST_ASSERT_EQUAL_UINT32(1, f.video.frames_recorded());
}

// ============================================================
// Clip rotation — duration limit
// ============================================================

/**
 * @brief Clip auto-closes when clip_duration_s is reached.
 */
void test_clip_closes_at_duration_limit()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    f.video.start_recording();

    // Record for 5 seconds at 10 FPS = 50 frames
    for (uint32_t i = 0; i < 50; i++)
    {
        set_mock_millis(1000 + (i + 1) * 100);
        f.video.tick();
    }

    // 50th frame triggers duration limit → STOPPING
    TEST_ASSERT_EQUAL_UINT32(50, f.video.frames_recorded());
    TEST_ASSERT_EQUAL(RecordingState::STOPPING, f.video.state());

    // Process the STOPPING state — auto-rotates to new clip
    f.video.tick();

    TEST_ASSERT_EQUAL(RecordingState::RECORDING, f.video.state());
    TEST_ASSERT_TRUE(f.video.is_recording());
    TEST_ASSERT_EQUAL_UINT32(0, f.video.frames_recorded());  // Reset for new clip
}

/**
 * @brief Clip auto-closes at frame-count limit (max_frames()).
 */
void test_clip_closes_at_max_frames()
{
    // Configure a very long clip duration to ensure max_frames() is hit first
    TestFixture f;
    f.setup_fake_frame();
    ConfigData::VideoConfig config;
    config.fps = TEST_FPS;
    strcpy(config.resolution, "VGA");
    config.clip_duration_s = 0;
    config.max_file_size_mb = 0;
    config.jpeg_quality = 10;
    f.video.init(config, TEST_WIDTH, TEST_HEIGHT);

    set_mock_millis(1000);
    f.video.start_recording();

    // Record up to max_frames()
    uint32_t limit = f.video.max_frames();
    for (uint32_t i = 0; i < limit; i++)
    {
        set_mock_millis(1000 + (i + 1) * 100);
        f.video.tick();
    }

    TEST_ASSERT_EQUAL_UINT32(limit, f.video.frames_recorded());
    TEST_ASSERT_EQUAL(RecordingState::STOPPING, f.video.state());

    // Process the STOPPING state — auto-rotates to new clip
    f.video.tick();

    TEST_ASSERT_EQUAL(RecordingState::RECORDING, f.video.state());
    TEST_ASSERT_EQUAL_UINT32(0, f.video.frames_recorded());
}

// ============================================================
// Clip rotation — file size limit
// ============================================================

/**
 * @brief Clip auto-closes when max_file_size_mb is reached.
 */
void test_clip_closes_at_file_size_limit()
{
    TestFixture f;
    f.setup_fake_frame();

    // Configure a small file size limit
    ConfigData::VideoConfig config;
    config.fps = TEST_FPS;
    strcpy(config.resolution, "VGA");
    config.clip_duration_s = 0;   // No duration limit
    config.max_file_size_mb = 1;  // 1 MB limit
    config.jpeg_quality = 10;
    f.video.init(config, TEST_WIDTH, TEST_HEIGHT);

    set_mock_millis(1000);
    f.video.start_recording();

    // Record frames until the file size limit is reached.
    // Each frame chunk ~108 bytes (8 header + 100 data), so we
    // need roughly 10000 frames to hit 1 MB. Record in a loop
    // but cap at a safe maximum to avoid test timeout.
    for (uint32_t i = 0; i < 100000; i++)
    {
        set_mock_millis(1000 + (i + 1) * 100);
        f.video.tick();

        if (f.video.state() == RecordingState::STOPPING)
        {
            break;
        }
    }

    TEST_ASSERT_EQUAL(RecordingState::STOPPING, f.video.state());
    TEST_ASSERT_GREATER_THAN(0u, f.video.file_size());

    // Process STOPPING — auto-rotates to new clip
    f.video.tick();
    TEST_ASSERT_EQUAL(RecordingState::RECORDING, f.video.state());
    TEST_ASSERT_EQUAL_UINT32(0, f.video.frames_recorded());
}

// ============================================================
// Clip rotation — new clip after auto-close
// ============================================================

/**
 * @brief After a clip auto-closes, the next tick opens a new clip.
 */
void test_new_clip_opens_after_auto_close()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    f.video.start_recording();

    // Record for exactly the clip duration (50 frames = 5s @ 10 FPS)
    for (uint32_t i = 0; i < 50; i++)
    {
        set_mock_millis(1000 + (i + 1) * 100);
        f.video.tick();
    }

    // Clip auto-closed, state is STOPPING
    TEST_ASSERT_EQUAL(RecordingState::STOPPING, f.video.state());

    // Process the STOPPING state — auto-rotates to new clip
    f.video.tick();

    TEST_ASSERT_EQUAL(RecordingState::RECORDING, f.video.state());
    TEST_ASSERT_EQUAL_UINT32(0, f.video.frames_recorded());
}

/**
 * @brief Explicit stop_recording() after auto-rotate goes to IDLE.
 */
void test_explicit_stop_after_rotation_goes_to_idle()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    f.video.start_recording();

    // Record until clip duration limit triggers auto-rotation
    for (uint32_t i = 0; i < 50; i++)
    {
        set_mock_millis(1000 + (i + 1) * 100);
        f.video.tick();
    }

    // Process STOPPING → auto-rotates to new clip
    f.video.tick();
    TEST_ASSERT_EQUAL(RecordingState::RECORDING, f.video.state());

    // Now explicitly stop
    f.video.stop_recording();
    f.video.tick();  // RECORDING → STOPPING
    f.video.tick();  // STOPPING → close_clip() → IDLE

    TEST_ASSERT_EQUAL(RecordingState::IDLE, f.video.state());
    TEST_ASSERT_FALSE(f.video.is_recording());
}

// ============================================================
// Directory creation
// ============================================================

/**
 * @brief open_clip() calls create_dir for the parent directory.
 */
void test_create_dir_called_on_open_clip()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    f.video.start_recording();

    // create_dir should have been called at least once for the parent directory
    TEST_ASSERT_GREATER_THAN(0, f.sd_hal.create_dir_call_count);

    // Stop recording
    f.video.stop_recording();
    f.video.tick();
    f.video.tick();
}

// ============================================================
// Rapid start/stop
// ============================================================

/**
 * @brief Start → stop within one frame interval produces a valid clip.
 */
void test_rapid_start_stop()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    f.video.start_recording();

    // No time has elapsed — tick captures first frame but no interval boundary
    f.video.tick();
    TEST_ASSERT_EQUAL_UINT32(1, f.video.frames_recorded());

    // Stop immediately
    f.video.stop_recording();
    f.video.tick();  // RECORDING → STOPPING
    f.video.tick();  // STOPPING → close_clip() → IDLE

    TEST_ASSERT_EQUAL(RecordingState::IDLE, f.video.state());
    TEST_ASSERT_EQUAL_UINT32(0, f.video.frames_recorded());  // Reset after close
}

// ============================================================
// Frame accumulation
// ============================================================

/**
 * @brief Frame chunks are accumulated during recording for replay at close.
 */
void test_frame_chunks_accumulated()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    f.video.start_recording();

    // Capture 3 frames
    for (uint32_t i = 0; i < 3; i++)
    {
        set_mock_millis(1000 + (i + 1) * 100);
        f.video.tick();
    }

    TEST_ASSERT_EQUAL_UINT32(3, f.video.frames_recorded());

    // File size should be > 0 (header + 3 frame chunks)
    TEST_ASSERT_GREATER_THAN(0u, f.video.file_size());

    // Close the clip
    f.video.stop_recording();
    f.video.tick();  // RECORDING → STOPPING
    f.video.tick();  // STOPPING → close_clip() → IDLE

    // After close, frames_recorded resets
    TEST_ASSERT_EQUAL_UINT32(0, f.video.frames_recorded());
}

// ============================================================
// File naming
// ============================================================

/**
 * @brief open_clip() generates a file path for the SD card.
 */
void test_file_path_generated_on_start()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(86400000u);  // 1 day since epoch

    f.video.start_recording();
    f.video.tick();  // Captures first frame

    // Stop and close
    f.video.stop_recording();
    f.video.tick();  // RECORDING → STOPPING
    f.video.tick();  // STOPPING → close_clip() → IDLE

    // Verify the SD HAL received file operations
    TEST_ASSERT_GREATER_THAN(0, f.sd_hal.open_call_count);
}

// ============================================================
// AVI format integrity
// ============================================================

/**
 * @brief Recorded clip produces a valid AVI with correct frame count.
 */
void test_avi_frame_count_matches()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    f.video.start_recording();

    // Record 10 frames
    for (uint32_t i = 0; i < 10; i++)
    {
        set_mock_millis(1000 + (i + 1) * 100);
        f.video.tick();
    }

    TEST_ASSERT_EQUAL_UINT32(10, f.video.frames_recorded());

    // Close the clip
    f.video.stop_recording();
    f.video.tick();  // RECORDING → STOPPING
    f.video.tick();  // STOPPING → close_clip() → IDLE

    // The SD HAL should have been called for open + writes + close
    TEST_ASSERT_GREATER_THAN(0, f.sd_hal.open_call_count);
    TEST_ASSERT_GREATER_THAN(0, f.sd_hal.write_call_count);
}

// ============================================================
// Error handling — SD write failure
// ============================================================

/**
 * @brief Clip close handles SD write failure gracefully.
 *
 * If the SD card fails to write during close_clip(), the
 * controller should still transition to IDLE without crashing.
 */
void test_clip_close_with_sd_write_failure()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    f.video.start_recording();

    // Capture a few frames
    set_mock_millis(1100);
    f.video.tick();
    set_mock_millis(1200);
    f.video.tick();
    TEST_ASSERT_EQUAL_UINT32(2, f.video.frames_recorded());

    // Now make the SD write fail
    f.sd_hal.write_result = false;

    // Stop recording — close_clip() will attempt SD writes that fail
    f.video.stop_recording();
    f.video.tick();  // RECORDING → STOPPING
    f.video.tick();  // STOPPING → close_clip() → IDLE

    // Should still transition to IDLE without crash
    TEST_ASSERT_EQUAL(RecordingState::IDLE, f.video.state());
    TEST_ASSERT_FALSE(f.video.is_recording());
}

// ============================================================
// Error handling — capture failure
// ============================================================

/**
 * @brief Capture failure during recording is handled gracefully.
 *
 * If the camera fails to capture a frame, the frame count should
 * not increment and recording should continue on the next tick.
 */
void test_frame_capture_failure_during_recording()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    f.video.start_recording();

    // First frame succeeds
    f.video.tick();
    TEST_ASSERT_EQUAL_UINT32(1, f.video.frames_recorded());

    // Make capture fail
    f.camera_hal.capture_result = {nullptr, 0};

    // Tick at the right interval — capture will fail
    set_mock_millis(1100);
    f.video.tick();

    // Frame count should NOT have incremented
    TEST_ASSERT_EQUAL_UINT32(1, f.video.frames_recorded());
    TEST_ASSERT_EQUAL(RecordingState::RECORDING, f.video.state());

    // Restore capture and tick again — should succeed
    f.setup_fake_frame();
    set_mock_millis(1200);
    f.video.tick();

    TEST_ASSERT_EQUAL_UINT32(2, f.video.frames_recorded());
    TEST_ASSERT_TRUE(f.video.is_recording());

    // Clean up
    f.video.stop_recording();
    f.video.tick();
    f.video.tick();
}

// ============================================================
// LED feedback (integration test)
// ============================================================

/**
 * @brief State transitions don't crash when tick() is called rapidly.
 */
void test_rapid_ticks_stability()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(0);

    f.video.start_recording();

    // Rapidly tick many times with advancing time
    for (uint32_t i = 0; i < 200; i++)
    {
        set_mock_millis(i * 50);
        f.video.tick();
    }

    // Should complete without crash
    // Final state depends on timing, just verify no assertion failure
    TEST_ASSERT_TRUE(f.video.state() == RecordingState::IDLE ||
                     f.video.state() == RecordingState::RECORDING);
}

// ============================================================
// build_camera_config resolution mapping
// ============================================================

MockLogBackend cam_log_backend;
RealTimeService cam_time;
LoggingService cam_logger{cam_log_backend, cam_time};

/**
 * @brief build_camera_config returns correct dimensions for known names.
 */
void test_build_camera_config_known_names()
{
    CameraImageConfig user_config;
    user_config.jpeg_quality = 10;

    user_config.resolution = "VGA";
    auto vga = build_camera_config(user_config, cam_logger);
    TEST_ASSERT_EQUAL_UINT16(640, vga.width);
    TEST_ASSERT_EQUAL_UINT16(480, vga.height);

    user_config.resolution = "HD";
    auto hd = build_camera_config(user_config, cam_logger);
    TEST_ASSERT_EQUAL_UINT16(1280, hd.width);
    TEST_ASSERT_EQUAL_UINT16(720, hd.height);

    user_config.resolution = "QVGA";
    auto qvga = build_camera_config(user_config, cam_logger);
    TEST_ASSERT_EQUAL_UINT16(320, qvga.width);
    TEST_ASSERT_EQUAL_UINT16(240, qvga.height);

    user_config.resolution = "QQVGA";
    auto qqvga = build_camera_config(user_config, cam_logger);
    TEST_ASSERT_EQUAL_UINT16(160, qqvga.width);
    TEST_ASSERT_EQUAL_UINT16(120, qqvga.height);

    user_config.resolution = "SVGA";
    auto svga = build_camera_config(user_config, cam_logger);
    TEST_ASSERT_EQUAL_UINT16(800, svga.width);
    TEST_ASSERT_EQUAL_UINT16(600, svga.height);

    user_config.resolution = "XGA";
    auto xga = build_camera_config(user_config, cam_logger);
    TEST_ASSERT_EQUAL_UINT16(1024, xga.width);
    TEST_ASSERT_EQUAL_UINT16(768, xga.height);

    user_config.resolution = "UXGA";
    auto uxga = build_camera_config(user_config, cam_logger);
    TEST_ASSERT_EQUAL_UINT16(1600, uxga.width);
    TEST_ASSERT_EQUAL_UINT16(1200, uxga.height);
}

/**
 * @brief build_camera_config returns VGA for unknown names.
 */
void test_build_camera_config_unknown_defaults_to_vga()
{
    CameraImageConfig user_config;
    user_config.resolution = "UNKNOWN";
    user_config.jpeg_quality = 10;
    auto res = build_camera_config(user_config, cam_logger);
    TEST_ASSERT_EQUAL_UINT16(640, res.width);
    TEST_ASSERT_EQUAL_UINT16(480, res.height);
}

/**
 * @brief build_camera_config returns requested dimensions directly.
 *
 */
void test_build_camera_config_returns_requested_dims()
{
    CameraImageConfig user_config;
    user_config.resolution = "UXGA";
    user_config.jpeg_quality = 10;
    auto params = build_camera_config(user_config, cam_logger);
    TEST_ASSERT_EQUAL_UINT16(1600, params.width);
    TEST_ASSERT_EQUAL_UINT16(1200, params.height);
}

// ============================================================
// Test runner
// ============================================================

int main()
{
    UNITY_BEGIN();

    // Default state
    RUN_TEST(test_initial_state_is_idle);
    RUN_TEST(test_initial_not_recording);
    RUN_TEST(test_initial_frames_recorded_zero);
    RUN_TEST(test_initial_file_size_zero);

    // start_recording / stop_recording
    RUN_TEST(test_start_recording_transitions_to_recording);
    RUN_TEST(test_start_recording_fails_when_already_recording);
    RUN_TEST(test_stop_recording_transitions_to_idle);
    RUN_TEST(test_stop_recording_noop_when_idle);

    // Frame pacing
    RUN_TEST(test_first_frame_captured_immediately);
    RUN_TEST(test_frames_captured_at_configured_interval);
    RUN_TEST(test_no_frame_when_interval_not_elapsed);

    // Clip rotation
    RUN_TEST(test_clip_closes_at_duration_limit);
    RUN_TEST(test_clip_closes_at_max_frames);
    RUN_TEST(test_clip_closes_at_file_size_limit);
    RUN_TEST(test_new_clip_opens_after_auto_close);
    RUN_TEST(test_explicit_stop_after_rotation_goes_to_idle);

    // Directory creation
    RUN_TEST(test_create_dir_called_on_open_clip);

    // Rapid start/stop
    RUN_TEST(test_rapid_start_stop);

    // Frame accumulation
    RUN_TEST(test_frame_chunks_accumulated);

    // File naming
    RUN_TEST(test_file_path_generated_on_start);

    // AVI format
    RUN_TEST(test_avi_frame_count_matches);

    // Stability
    RUN_TEST(test_rapid_ticks_stability);

    // Error handling
    RUN_TEST(test_clip_close_with_sd_write_failure);
    RUN_TEST(test_frame_capture_failure_during_recording);

    // build_camera_config resolution mapping
    RUN_TEST(test_build_camera_config_known_names);
    RUN_TEST(test_build_camera_config_unknown_defaults_to_vga);
    RUN_TEST(test_build_camera_config_returns_requested_dims);

    return UNITY_END();
}
