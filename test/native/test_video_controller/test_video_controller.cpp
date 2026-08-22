#include <unity.h>
#include "video_controller.h"
#include "stream_service.h"
#include "camera_controller.h"
#include "sd_controller.h"
#include "mock_camera_hal.h"
#include "mock_sd_card_hal.h"
#include "mock_capture_scheduler.h"
#include "mock_log_backend.h"
#include <cstring>

// ============================================================
// Test configuration
// ============================================================

static constexpr uint16_t TEST_FPS = 10;
static constexpr uint16_t TEST_CLIP_DURATION_S = 5;
static constexpr uint16_t TEST_FRAME_COUNT = TEST_FPS * TEST_CLIP_DURATION_S;
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
 * Uses MockCaptureScheduler for deterministic
 * synchronous frame capture. on_frame_capture() triggers the
 * scheduler callback synchronously.
 */
struct TestFixture
{
    MockCameraHal camera_hal;
    MockSdCardHal sd_hal;
    MockLogBackend log_backend;
    RealTimeService time;
    LoggingService logger;
    SdController sd_controller;
    CameraController camera_ctrl;
    MockCaptureScheduler capture_scheduler;
    VideoController video;

    TestFixture()
        : logger(log_backend, time)
        , sd_controller(time, sd_hal)
        , camera_ctrl(camera_hal)
        , video(camera_ctrl, sd_controller, time, logger, capture_scheduler)
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

    /**
     * @brief Drives a single frame capture via the mock scheduler.
     *
     * Calls capture_scheduler.on_frame_capture() which synchronously
     * invokes the VideoController frame callback.
     */
    void capture_one_frame()
    {
        capture_scheduler.on_frame_capture();
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
 * @brief stop_recording() synchronously closes the clip and returns to IDLE.
 */
void test_stop_recording_transitions_to_idle()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    f.video.start_recording();

    // Capture a frame
    f.capture_one_frame();
    TEST_ASSERT_EQUAL_UINT32(1, f.video.frames_recorded());

    // stop_recording() synchronously closes the clip: the scheduler runs
    // one final callback which observes m_stop_clip and transitions to IDLE
    f.video.stop_recording();

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
    f.capture_one_frame();

    TEST_ASSERT_EQUAL_UINT32(1, f.video.frames_recorded());
    TEST_ASSERT_EQUAL_INT(1, f.camera_hal.capture_call_count);
}

/**
 * @brief Frames are captured via successive on_frame_capture() calls.
 */
void test_frames_captured_at_configured_interval()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    f.video.start_recording();

    // Capture 5 frames via on_frame_capture()
    for (uint32_t i = 0; i < 5; i++)
    {
        f.capture_one_frame();
    }

    TEST_ASSERT_EQUAL_UINT32(5, f.video.frames_recorded());
}

/**
 * @brief No frame is captured without a scheduler trigger.
 *
 * Frame pacing is the hardware timer's responsibility (see
 * CaptureSchedulerEsp32); the mock scheduler only fires on explicit
 * on_frame_capture() calls, so frames must not accumulate on their own.
 */
void test_no_frame_without_scheduler_trigger()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    f.video.start_recording();
    f.capture_one_frame();  // Captures first frame

    // Without calling on_frame_capture(), no new frame
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

    // Record for desired frame count
    for (uint32_t i = 0; i < TEST_FRAME_COUNT; i++)
    {
        f.capture_one_frame();
    }

    // 50th frame triggers duration limit → auto-rotates to new clip
    TEST_ASSERT_EQUAL_UINT32(0, f.video.frames_recorded());  // Reset after auto-rotation
    TEST_ASSERT_TRUE(f.video.is_recording());
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
        f.capture_one_frame();

        // Check if auto-rotation happened (frames_recorded reset to 0)
        if (f.video.frames_recorded() == 0 && i > 0)
        {
            break;
        }
    }

    TEST_ASSERT_TRUE(f.video.is_recording());

    // Auto-rotated to new clip
    TEST_ASSERT_EQUAL_UINT32(0, f.video.frames_recorded());
}

// ============================================================
// Clip rotation — new clip after auto-close
// ============================================================

/**
 * @brief After a clip auto-closes, the next capture continues in a new clip.
 */
void test_new_clip_opens_after_auto_close()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    f.video.start_recording();

    // Record for exactly the clip duration
    for (uint32_t i = 0; i < TEST_FRAME_COUNT; i++)
    {
        f.capture_one_frame();
    }

    // Auto-rotated to new clip
    TEST_ASSERT_TRUE(f.video.is_recording());
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
    for (uint32_t i = 0; i < TEST_FRAME_COUNT; i++)
    {
        f.capture_one_frame();
    }

    // Auto-rotated to new clip
    TEST_ASSERT_EQUAL(RecordingState::RECORDING, f.video.state());

    // Now explicitly stop — synchronously closes the clip
    f.video.stop_recording();

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

    // Stop recording — synchronously closes the clip
    f.video.stop_recording();
}

// ============================================================
// Photo capture — parent directory creation
// ============================================================

/**
 * @brief capture_photo() creates the parent directory before writing.
 *
 * The first photo of a new day must not fail because its /YYYYMMDD
 * directory does not yet exist on the SD card.
 */
void test_capture_photo_creates_parent_dir()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1234567890u);

    char photo_path[64];
    strcpy(photo_path, "/20260213/20260213_123456.jpg");

    TEST_ASSERT_TRUE(f.video.capture_photo(photo_path));

    // create_dir must have been called for the parent directory
    TEST_ASSERT_GREATER_THAN(0, f.sd_hal.create_dir_call_count);
    TEST_ASSERT_EQUAL_STRING("/20260213", f.sd_hal.last_create_dir_path);

    // The photo file must exist in the SD store
    TEST_ASSERT_TRUE(f.sd_hal.files.find(photo_path) != f.sd_hal.files.end());
}

/**
 * @brief capture_photo() writes the JPEG data to the photo path.
 */
void test_capture_photo_writes_jpeg_data()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1234567890u);

    char photo_path[64];
    strcpy(photo_path, "/20260213/20260213_123456.jpg");

    TEST_ASSERT_TRUE(f.video.capture_photo(photo_path));

    const std::vector<uint8_t>& content = f.sd_hal.files[photo_path];
    TEST_ASSERT_EQUAL_UINT32(FAKE_JPEG_SIZE, content.size());
    TEST_ASSERT_EQUAL_UINT8(0xAB, content[0]);
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

    // Capture one frame
    f.capture_one_frame();
    TEST_ASSERT_EQUAL_UINT32(1, f.video.frames_recorded());

    // Stop immediately — synchronously closes the clip
    f.video.stop_recording();

    TEST_ASSERT_EQUAL(RecordingState::IDLE, f.video.state());
    TEST_ASSERT_EQUAL_UINT32(0, f.video.frames_recorded());  // Reset after close
}

// ============================================================
// Frame accumulation
// ============================================================

/**
 * @brief Frame chunks are written directly to SD during recording.
 */
void test_frames_written_to_sd()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    f.video.start_recording();

    // Capture 3 frames
    for (uint32_t i = 0; i < 3; i++)
    {
        f.capture_one_frame();
    }

    TEST_ASSERT_EQUAL_UINT32(3, f.video.frames_recorded());
    // Each frame should have triggered an SD write
    TEST_ASSERT_GREATER_OR_EQUAL(3, f.sd_hal.write_call_count);

    f.video.stop_recording();
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
    f.capture_one_frame();  // Captures first frame

    // Stop and close — synchronously finalizes the clip
    f.video.stop_recording();

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
        f.capture_one_frame();
    }

    TEST_ASSERT_EQUAL_UINT32(10, f.video.frames_recorded());

    // Close the clip — synchronously finalizes the AVI
    f.video.stop_recording();

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
    f.capture_one_frame();
    f.capture_one_frame();
    TEST_ASSERT_EQUAL_UINT32(2, f.video.frames_recorded());

    // Now make the SD write fail
    f.sd_hal.write_result = false;

    // Stop recording — close_clip() will attempt SD writes that fail
    f.video.stop_recording();

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
 * not increment and recording should continue on the next frame.
 */
void test_frame_capture_failure_during_recording()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    f.video.start_recording();

    // First frame succeeds
    f.capture_one_frame();
    TEST_ASSERT_EQUAL_UINT32(1, f.video.frames_recorded());

    // Make capture fail
    f.camera_hal.capture_result = {nullptr, 0};

    // Next capture will fail
    f.capture_one_frame();

    // Frame count should NOT have incremented
    TEST_ASSERT_EQUAL_UINT32(1, f.video.frames_recorded());
    TEST_ASSERT_EQUAL(RecordingState::RECORDING, f.video.state());

    // Restore capture and try again — should succeed
    f.setup_fake_frame();
    f.capture_one_frame();

    TEST_ASSERT_EQUAL_UINT32(2, f.video.frames_recorded());
    TEST_ASSERT_TRUE(f.video.is_recording());

    // Clean up — synchronously closes the clip
    f.video.stop_recording();
}

// ============================================================
// Synchronous stop semantics
// ============================================================

/**
 * @brief A stale callback arriving after stop is ignored.
 *
 * Once stop_recording() has transitioned to IDLE, any late scheduler
 * callback (e.g. a task notification queued before the timer stopped)
 * must be dropped by the state guard.
 */
void test_stale_callback_after_stop_ignored()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    f.video.start_recording();
    f.capture_one_frame();
    TEST_ASSERT_EQUAL_UINT32(1, f.video.frames_recorded());

    f.video.stop_recording();
    TEST_ASSERT_EQUAL(RecordingState::IDLE, f.video.state());

    // Late callback: must not capture or corrupt state
    f.capture_one_frame();
    TEST_ASSERT_EQUAL(RecordingState::IDLE, f.video.state());
    TEST_ASSERT_EQUAL_UINT32(0, f.video.frames_recorded());
}

/**
 * @brief Recording can be restarted after a synchronous stop.
 *
 * Also exercises clip filename collision handling: both sessions share
 * the same timestamp, so the second clip must be written to a uniquified
 * path rather than clobbering the first.
 */
void test_restart_after_stop()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    TEST_ASSERT_TRUE(f.video.start_recording());
    f.capture_one_frame();
    TEST_ASSERT_EQUAL_UINT32(1, f.video.frames_recorded());

    f.video.stop_recording();
    TEST_ASSERT_EQUAL(RecordingState::IDLE, f.video.state());

    // Second session: the scheduler must accept a fresh start
    TEST_ASSERT_TRUE(f.video.start_recording());
    TEST_ASSERT_EQUAL(RecordingState::RECORDING, f.video.state());
    f.capture_one_frame();
    TEST_ASSERT_EQUAL_UINT32(1, f.video.frames_recorded());

    f.video.stop_recording();
    TEST_ASSERT_EQUAL(RecordingState::IDLE, f.video.state());

    // Collision avoidance: same timestamp → two distinct files on the SD
    TEST_ASSERT_EQUAL_UINT32(2, f.sd_hal.files.size());
}

/**
 * @brief start_recording() fails cleanly when the scheduler is busy.
 *
 * Simulates a scheduler that refuses to start (e.g. a leftover task
 * after a timed-out stop). The controller must return to IDLE with no
 * active clip and must not leave the scheduler running.
 */
void test_start_recording_fails_when_scheduler_busy()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    f.capture_scheduler.start_result = false;

    TEST_ASSERT_FALSE(f.video.start_recording());
    TEST_ASSERT_EQUAL(RecordingState::IDLE, f.video.state());
    TEST_ASSERT_FALSE(f.video.is_recording());
    TEST_ASSERT_EQUAL_INT(1, f.capture_scheduler.start_call_count);
}

/**
 * @brief A closed clip leaves a complete AVI file in the SD store.
 *
 * After stop, the stored file must begin with the patched RIFF header
 * and end with the idx1 index chunk.
 */
void test_clip_file_intact_after_close()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    f.video.start_recording();

    // Record 3 frames
    for (uint32_t i = 0; i < 3; i++)
    {
        f.capture_one_frame();
    }

    f.video.stop_recording();
    TEST_ASSERT_EQUAL(RecordingState::IDLE, f.video.state());

    // Exactly one file should exist in the store
    TEST_ASSERT_EQUAL_UINT32(1, f.sd_hal.files.size());

    const std::vector<uint8_t>& content = f.sd_hal.files.begin()->second;
    TEST_ASSERT_GREATER_THAN(224, content.size());

    // Patched header begins with RIFF magic
    TEST_ASSERT_EQUAL_UINT8('R', content[0]);
    TEST_ASSERT_EQUAL_UINT8('I', content[1]);
    TEST_ASSERT_EQUAL_UINT8('F', content[2]);
    TEST_ASSERT_EQUAL_UINT8('F', content[3]);

    // Index chunk present after header + movi data
    bool found_idx1 = false;
    for (size_t i = 0; i + 4 <= content.size(); i++)
    {
        if (content[i] == 'i' && content[i + 1] == 'd'
            && content[i + 2] == 'x' && content[i + 3] == '1')
        {
            found_idx1 = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(found_idx1);
}

// ============================================================
// LED feedback (integration test)
// ============================================================

/**
 * @brief Rapid frame captures don't crash or corrupt state.
 */
void test_rapid_captures_stability()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(0);

    f.video.start_recording();

    // Rapidly capture many frames
    for (uint32_t i = 0; i < 200; i++)
    {
        f.capture_one_frame();
    }

    // Should complete without crash
    // Final state depends on clip rotation, just verify no assertion failure
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
// Streaming (preview)
// ============================================================

/**
 * @brief start_streaming() transitions IDLE → STREAMING.
 */
void test_start_streaming_transitions_to_streaming()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    TEST_ASSERT_TRUE(f.video.start_streaming());
    TEST_ASSERT_EQUAL(RecordingState::STREAMING, f.video.state());
    TEST_ASSERT_FALSE(f.video.is_recording());
}

/**
 * @brief stop_streaming() transitions STREAMING → IDLE.
 */
void test_stop_streaming_transitions_to_idle()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    f.video.start_streaming();
    f.video.stop_streaming();

    TEST_ASSERT_EQUAL(RecordingState::IDLE, f.video.state());
}

/**
 * @brief A preview frame is delivered to the stream sink but not written to SD.
 */
void test_streaming_delivers_frame_without_sd_write()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    // Wire a minimal sink that records deliveries and teardown calls.
    struct Sink : public IStreamService
    {
        int deliveries{0};
        int teardown_calls{0};
        void deliver_frame(const uint8_t* /*data*/, size_t /*len*/) override
        {
            deliveries++;
        }
        void stop_all_streams() override
        {
            teardown_calls++;
        }
    } sink;

    f.video.set_stream_service(&sink);
    f.video.start_streaming();

    size_t files_before = f.sd_hal.files.size();
    f.capture_one_frame();

    TEST_ASSERT_EQUAL_INT(1, sink.deliveries);
    // No SD writes occurred during preview.
    TEST_ASSERT_EQUAL_UINT32(static_cast<unsigned int>(files_before),
                             static_cast<unsigned int>(f.sd_hal.files.size()));

    f.video.stop_streaming();
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
    RUN_TEST(test_no_frame_without_scheduler_trigger);

    // Clip rotation
    RUN_TEST(test_clip_closes_at_duration_limit);
    RUN_TEST(test_clip_closes_at_file_size_limit);
    RUN_TEST(test_new_clip_opens_after_auto_close);
    RUN_TEST(test_explicit_stop_after_rotation_goes_to_idle);

    // Directory creation
    RUN_TEST(test_create_dir_called_on_open_clip);

    // Photo capture — parent directory creation
    RUN_TEST(test_capture_photo_creates_parent_dir);
    RUN_TEST(test_capture_photo_writes_jpeg_data);

    // Rapid start/stop
    RUN_TEST(test_rapid_start_stop);

    // Frame accumulation
    RUN_TEST(test_frames_written_to_sd);

    // File naming
    RUN_TEST(test_file_path_generated_on_start);

    // AVI format
    RUN_TEST(test_avi_frame_count_matches);

    // Stability
    RUN_TEST(test_rapid_captures_stability);

    // Error handling
    RUN_TEST(test_clip_close_with_sd_write_failure);
    RUN_TEST(test_frame_capture_failure_during_recording);

    // Synchronous stop semantics
    RUN_TEST(test_stale_callback_after_stop_ignored);
    RUN_TEST(test_restart_after_stop);
    RUN_TEST(test_start_recording_fails_when_scheduler_busy);
    RUN_TEST(test_clip_file_intact_after_close);

    // build_camera_config resolution mapping
    RUN_TEST(test_build_camera_config_known_names);
    RUN_TEST(test_build_camera_config_unknown_defaults_to_vga);
    RUN_TEST(test_build_camera_config_returns_requested_dims);

    // Streaming (preview)
    RUN_TEST(test_start_streaming_transitions_to_streaming);
    RUN_TEST(test_stop_streaming_transitions_to_idle);
    RUN_TEST(test_streaming_delivers_frame_without_sd_write);

    return UNITY_END();
}
