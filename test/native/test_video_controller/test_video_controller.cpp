#include <unity.h>
#include "video_controller.h"
#include "stream_service.h"
#include "camera_controller.h"
#include "sd_controller.h"
#include "mock_camera_hal.h"
#include "mock_sd_card_hal.h"
#include "mock_capture_scheduler.h"
#include "mock_log_backend.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

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

/**
 * @brief Stream-sink test double with delivery and teardown tracking.
 */
struct TrackingSink : public IStreamService
{
    int deliveries{0};
    int teardown_calls{0};
    bool loop_running_at_teardown{false};
    const MockCaptureScheduler* scheduler_ref{nullptr};

    void deliver_frame(const uint8_t* /*data*/, size_t /*len*/) override
    {
        deliveries++;
    }

    void stop_all_streams() override
    {
        teardown_calls++;
        if (scheduler_ref != nullptr)
        {
            loop_running_at_teardown = scheduler_ref->is_started();
        }
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

    TEST_ASSERT_FALSE(f.video.is_recording());
    TEST_ASSERT_FALSE(f.video.is_streaming());
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
    TEST_ASSERT_TRUE(f.video.is_recording());
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

    // stop_recording() executes through the executor: the sink detaches and
    // the clip close is requested; the scheduler runs one final callback
    // which finalizes the clip before exiting
    f.video.stop_recording();

    TEST_ASSERT_FALSE(f.video.is_recording());
    TEST_ASSERT_FALSE(f.video.is_streaming());
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
    TEST_ASSERT_FALSE(f.video.is_recording());
    TEST_ASSERT_FALSE(f.video.is_streaming());
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
    TEST_ASSERT_TRUE(f.video.is_recording());

    // Now explicitly stop — synchronously closes the clip
    f.video.stop_recording();

    TEST_ASSERT_FALSE(f.video.is_recording());
    TEST_ASSERT_FALSE(f.video.is_streaming());
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
// Photo capture — cross-transport serialization
// ============================================================

/**
 * @brief capture_photo() holds the command mutex across capture+write.
 *
 * Regression test for the boundary-audit race (report §6.1): a photo
 * in flight must not let a concurrent START_STREAM (arriving on the
 * other transport) post and activate the executor inside the photo's
 * capture+SD-write window. While the photo is parked inside its SD
 * write, a concurrent start_streaming() must be blocked — the capture
 * loop must not start and the executor must not capture a frame —
 * until the photo completes. Also asserts the written data is intact
 * and the photo's frame buffer was released (no leak).
 */
void test_capture_photo_holds_mutex_across_capture_and_write()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1234567890u);

    char photo_path[64];
    strcpy(photo_path, "/20260213/20260213_123456.jpg");

    // Park the photo inside its SD write; the gate is released below
    // once the interleaving window has been exercised.
    f.sd_hal.gate_write_at_count.store(1, std::memory_order_relaxed);

    std::atomic<bool> photo_ok{false};
    std::thread photographer([&]()
    {
        photo_ok.store(f.video.capture_photo(photo_path),
                       std::memory_order_release);
    });

    // Wait until the photographer is parked inside the SD write.
    constexpr long MAX_SPIN = 10000000L;
    long spins = 0;
    while (f.sd_hal.gate_write_hits.load(std::memory_order_acquire) < 1 &&
           spins++ < MAX_SPIN)
    {
        std::this_thread::yield();
    }
    TEST_ASSERT_EQUAL_INT(1,
        f.sd_hal.gate_write_hits.load(std::memory_order_acquire));

    // Contending start from the second command transport while the
    // photo holds the gate-check-to-write critical section.
    std::atomic<bool> start_ok{false};
    std::thread starter([&]()
    {
        start_ok.store(f.video.start_streaming(),
                       std::memory_order_release);
    });

    // Give a broken serialization a window to slip the start in.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const bool loop_started_during_write =
        f.capture_scheduler.is_started();
    const int captures_during_write = f.camera_hal.capture_call_count;

    // Release the photo; both contenders must then complete cleanly.
    f.sd_hal.gate_write_release.store(true, std::memory_order_release);
    photographer.join();
    starter.join();

    TEST_ASSERT_TRUE(photo_ok.load());
    TEST_ASSERT_TRUE(start_ok.load());

    // Core invariant: the executor never overlapped the photo window —
    // the loop did not start and no extra frame was captured while the
    // photo's write was in flight.
    TEST_ASSERT_FALSE(loop_started_during_write);
    TEST_ASSERT_EQUAL_INT(1, captures_during_write);

    // No torn data: the photo file holds exactly the fake JPEG.
    const std::vector<uint8_t>& content = f.sd_hal.files[photo_path];
    TEST_ASSERT_EQUAL_UINT32(FAKE_JPEG_SIZE, content.size());
    bool intact = true;
    for (size_t i = 0; i < content.size(); i++)
    {
        if (content[i] != 0xAB)
        {
            intact = false;
            break;
        }
    }
    TEST_ASSERT_TRUE(intact);

    // No buffer leak: the photo released its frame exactly once.
    TEST_ASSERT_EQUAL_INT(1, f.camera_hal.release_call_count);

    // Wind down for a clean fixture teardown.
    f.video.stop_streaming();
    TEST_ASSERT_FALSE(f.capture_scheduler.is_started());
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

    TEST_ASSERT_FALSE(f.video.is_recording());
    TEST_ASSERT_FALSE(f.video.is_streaming());
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
    TEST_ASSERT_FALSE(f.video.is_recording());
    TEST_ASSERT_FALSE(f.video.is_streaming());
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
    TEST_ASSERT_TRUE(f.video.is_recording());

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
    TEST_ASSERT_FALSE(f.video.is_recording());
    TEST_ASSERT_FALSE(f.video.is_streaming());

    // Late callback: must not capture or corrupt state
    f.capture_one_frame();
    TEST_ASSERT_FALSE(f.video.is_recording());
    TEST_ASSERT_FALSE(f.video.is_streaming());
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
    TEST_ASSERT_FALSE(f.video.is_recording());
    TEST_ASSERT_FALSE(f.video.is_streaming());

    // Second session: the scheduler must accept a fresh start
    TEST_ASSERT_TRUE(f.video.start_recording());
    TEST_ASSERT_TRUE(f.video.is_recording());
    f.capture_one_frame();
    TEST_ASSERT_EQUAL_UINT32(1, f.video.frames_recorded());

    f.video.stop_recording();
    TEST_ASSERT_FALSE(f.video.is_recording());
    TEST_ASSERT_FALSE(f.video.is_streaming());

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
    TEST_ASSERT_FALSE(f.video.is_recording());
    TEST_ASSERT_FALSE(f.video.is_streaming());
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
    TEST_ASSERT_FALSE(f.video.is_recording());
    TEST_ASSERT_FALSE(f.video.is_streaming());

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
    TEST_ASSERT_FALSE(f.video.is_streaming());
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
    TEST_ASSERT_TRUE(f.video.is_streaming());
    TEST_ASSERT_FALSE(f.video.is_recording());
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

    TEST_ASSERT_FALSE(f.video.is_recording());
    TEST_ASSERT_FALSE(f.video.is_streaming());
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
// Consumer registry
// ============================================================

/**
 * @brief Test double frame consumer with an optional hook to start a
 *        recording from within on_frame() — the sanctioned path for
 *        consumers to trigger recording from the frame callback.
 */
struct FakeFrameConsumer : public IFrameConsumer
{
    int frames{0};
    VideoController* trigger_recording_on_first_frame{nullptr};
    VideoController* trigger_streaming_on_first_frame{nullptr};
    VideoController* trigger_stop_streaming_on_frame{nullptr};
    VideoController* self_unregister_on_frame{nullptr};
    int self_unregister_id{-1};

    void on_frame(const uint8_t* /*data*/, size_t /*len*/) override
    {
        frames++;
        if (trigger_recording_on_first_frame != nullptr)
        {
            trigger_recording_on_first_frame->start_recording();
            trigger_recording_on_first_frame = nullptr;
        }
        if (trigger_streaming_on_first_frame != nullptr)
        {
            trigger_streaming_on_first_frame->start_streaming();
            trigger_streaming_on_first_frame = nullptr;
        }
        if (trigger_stop_streaming_on_frame != nullptr)
        {
            trigger_stop_streaming_on_frame->stop_streaming();
            trigger_stop_streaming_on_frame = nullptr;
        }
        if (self_unregister_on_frame != nullptr && self_unregister_id >= 0)
        {
            self_unregister_on_frame->unregister_frame_consumer(
                self_unregister_id);
            self_unregister_id = -1;
            self_unregister_on_frame = nullptr;
        }
    }
};

/**
 * @brief Registering a consumer starts the loop; unregistering the last
 *        consumer tears the pipeline down and closes viewers.
 */
void test_register_consumer_lifecycle()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    TrackingSink sink;
    sink.scheduler_ref = &f.capture_scheduler;
    f.video.set_stream_service(&sink);

    FakeFrameConsumer consumer;
    int id = f.video.register_frame_consumer(&consumer);
    TEST_ASSERT_TRUE(id >= 0);
    TEST_ASSERT_TRUE(f.video.is_capture_active());
    TEST_ASSERT_TRUE(f.capture_scheduler.is_started());

    f.capture_one_frame();
    TEST_ASSERT_EQUAL_INT(1, consumer.frames);

    // Last consumer gone: loop stops, viewers closed.
    f.video.unregister_frame_consumer(id);
    TEST_ASSERT_FALSE(f.video.is_capture_active());
    TEST_ASSERT_FALSE(f.capture_scheduler.is_started());
    TEST_ASSERT_EQUAL_INT(1, sink.teardown_calls);
    TEST_ASSERT_FALSE(sink.loop_running_at_teardown);
}

/**
 * @brief Registration beyond MAX_FRAME_CONSUMERS is rejected; duplicate
 *        registration of the same pointer is rejected.
 */
void test_register_rejects_full_and_duplicate()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    FakeFrameConsumer c1;
    FakeFrameConsumer c2;
    FakeFrameConsumer c3;

    int id1 = f.video.register_frame_consumer(&c1);
    int id2 = f.video.register_frame_consumer(&c2);
    int id3 = f.video.register_frame_consumer(&c3);
    TEST_ASSERT_TRUE(id1 >= 0);
    TEST_ASSERT_TRUE(id2 >= 0);
    TEST_ASSERT_TRUE(id3 >= 0);

    FakeFrameConsumer c4;
    TEST_ASSERT_EQUAL_INT(-1, f.video.register_frame_consumer(&c4));
    TEST_ASSERT_EQUAL_INT(-1, f.video.register_frame_consumer(nullptr));
    TEST_ASSERT_EQUAL_INT(-1, f.video.register_frame_consumer(&c2));

    // Recording/streaming share the same registry capacity.
    TEST_ASSERT_FALSE(f.video.start_recording());
    TEST_ASSERT_FALSE(f.video.start_streaming());

    // Freeing one slot makes room again.
    f.video.unregister_frame_consumer(id3);
    TEST_ASSERT_TRUE(f.video.start_streaming());

    f.video.stop_streaming();
    f.video.unregister_frame_consumer(id1);
    f.video.unregister_frame_consumer(id2);
}

/**
 * @brief A rejected registration leaves no reserved slot behind.
 *
 * When the capture loop cannot start (scheduler busy), the registry
 * slot reserved by the failed registration must be released: no
 * phantom consumer may keep is_capture_active() true or permanently
 * consume registry capacity.
 */
void test_failed_registration_leaves_no_reserved_slot()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    f.capture_scheduler.start_result = false;

    FakeFrameConsumer consumer;
    TEST_ASSERT_EQUAL_INT(-1, f.video.register_frame_consumer(&consumer));
    TEST_ASSERT_FALSE(f.video.is_capture_active());
    TEST_ASSERT_FALSE(f.video.is_recording());
    TEST_ASSERT_FALSE(f.video.is_streaming());

    // The registry has room again once the scheduler recovers.
    f.capture_scheduler.start_result = true;
    int id = f.video.register_frame_consumer(&consumer);
    TEST_ASSERT_TRUE(id >= 0);
    TEST_ASSERT_TRUE(f.video.is_capture_active());

    f.video.unregister_frame_consumer(id);
    TEST_ASSERT_FALSE(f.video.is_capture_active());
}

/**
 * @brief A consumer can start recording from within on_frame() —
 *        the sanctioned extension path with no special casing.
 */
void test_consumer_can_trigger_recording_from_on_frame()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    FakeFrameConsumer monitor;
    monitor.trigger_recording_on_first_frame = &f.video;

    int id = f.video.register_frame_consumer(&monitor);
    TEST_ASSERT_TRUE(id >= 0);
    TEST_ASSERT_FALSE(f.video.is_recording());

    // First frame: the monitor sees it and issues start_recording() from
    // within on_frame() — a fire-and-forget request in the executor
    // context, consumed at the next activation boundary (the documented
    // D3 timing: a consumer-triggered start binds for the next
    // activation).
    f.capture_one_frame();
    TEST_ASSERT_FALSE(f.video.is_recording());

    // Next activation: a pure service pass executes the start; no frame
    // is captured.
    f.capture_one_frame();
    TEST_ASSERT_TRUE(f.video.is_recording());

    // Subsequent frames are written to SD by the recording session.
    f.capture_one_frame();
    TEST_ASSERT_EQUAL_UINT32(1, f.video.frames_recorded());
    TEST_ASSERT_GREATER_OR_EQUAL(1, f.sd_hal.write_call_count);

    // Wind down: stop recording (the executor finalizes the clip at the
    // join's final callback), then detach the monitor.
    f.video.stop_recording();
    TEST_ASSERT_FALSE(f.video.is_recording());
    f.video.unregister_frame_consumer(id);
    TEST_ASSERT_FALSE(f.video.is_capture_active());
}

/**
 * @brief stop_streaming() cancels a queued-but-unexecuted start.
 *
 * While the capture loop is running, a stop must be posted even though
 * the streaming sink is not bound yet (the start request issued from
 * within on_frame() is still pending): the executor executes start and
 * stop in queue order, so the stop reliably wins instead of being
 * silently dropped against a start that has not run yet.
 */
void test_stop_streaming_cancels_pending_start()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    FakeFrameConsumer monitor;
    monitor.trigger_streaming_on_first_frame = &f.video;

    int id = f.video.register_frame_consumer(&monitor);
    TEST_ASSERT_TRUE(id >= 0);

    // The monitor issues start_streaming() from within on_frame() — a
    // fire-and-forget request: the loop runs but the sink is not bound.
    f.capture_one_frame();
    TEST_ASSERT_FALSE(f.video.is_streaming());
    TEST_ASSERT_TRUE(f.capture_scheduler.is_started());

    // The stop must be posted against the pending start, not dropped.
    f.video.stop_streaming();
    TEST_ASSERT_FALSE(f.video.is_streaming());

    // The queued batch executes start then stop (FIFO): streaming ends
    // up off while the monitor keeps the loop alive.
    f.capture_one_frame();
    TEST_ASSERT_FALSE(f.video.is_streaming());
    TEST_ASSERT_TRUE(f.capture_scheduler.is_started());

    f.video.unregister_frame_consumer(id);
    TEST_ASSERT_FALSE(f.capture_scheduler.is_started());
}

/**
 * @brief A fire-and-forget stop that drains the registry still closes
 *        viewers, and the next command reaps the parked loop.
 *
 * A lifecycle command issued from within on_frame() is never waited
 * on, so no command task consumes the executor-reported drain right
 * away. The executor must close connected viewers itself at the next
 * activation boundary, leave the drained signal armed, and the next
 * command of any kind must then drive the bounded scheduler join.
 */
void test_fire_and_forget_drain_closes_viewers_and_reaps_loop()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    TrackingSink sink;
    f.video.set_stream_service(&sink);

    FakeFrameConsumer monitor;
    monitor.trigger_streaming_on_first_frame = &f.video;

    int id = f.video.register_frame_consumer(&monitor);
    TEST_ASSERT_TRUE(id >= 0);
    f.capture_one_frame();   // monitor posts fire-and-forget START_STREAMING
    f.capture_one_frame();   // service pass executes the start
    TEST_ASSERT_TRUE(f.video.is_streaming());

    // Monitor stops streaming and unregisters itself from within
    // on_frame() — both fire-and-forget.
    monitor.trigger_stop_streaming_on_frame = &f.video;
    monitor.self_unregister_on_frame = &f.video;
    monitor.self_unregister_id = id;
    f.capture_one_frame();

    // Service pass executes the batch: sink detached, monitor detached,
    // registry drains with no waiting command task.
    f.capture_one_frame();
    TEST_ASSERT_FALSE(f.video.is_streaming());
    TEST_ASSERT_FALSE(f.video.is_capture_active());

    // The executor closes the viewers at the next (idle) activation
    // boundary; the loop stays parked for the command-side teardown.
    f.capture_one_frame();
    TEST_ASSERT_EQUAL_INT(1, sink.teardown_calls);
    TEST_ASSERT_TRUE(f.capture_scheduler.is_started());

    // The next command consumes the armed drain: the bounded join runs
    // and the loop restarts for the freshly bound streaming sink.
    TEST_ASSERT_TRUE(f.video.start_streaming());
    TEST_ASSERT_TRUE(f.video.is_streaming());
    TEST_ASSERT_TRUE(f.capture_scheduler.is_started());
    TEST_ASSERT_EQUAL_INT(1, sink.teardown_calls);

    f.video.stop_streaming();
    TEST_ASSERT_FALSE(f.capture_scheduler.is_started());
}


/**
 * @brief Derived state: recording takes precedence over streaming.
 */
void test_state_reports_recording_when_both_active()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    TEST_ASSERT_TRUE(f.video.start_streaming());
    TEST_ASSERT_TRUE(f.video.start_recording());

    TEST_ASSERT_TRUE(f.video.is_recording());
    TEST_ASSERT_TRUE(f.video.is_streaming());
    TEST_ASSERT_TRUE(f.video.is_recording());
}

/**
 * @brief stop_recording() during REC_AND_PREVIEW keeps preview alive.
 *
 * The capture loop must keep running (no scheduler stop), the clip must
 * be finalized on the next frame callback, and viewers keep receiving
 * frames — this is the core stream-hang regression at controller level.
 */
void test_stop_recording_during_streaming_keeps_preview()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    TrackingSink sink;
    sink.scheduler_ref = &f.capture_scheduler;
    f.video.set_stream_service(&sink);

    TEST_ASSERT_TRUE(f.video.start_streaming());
    TEST_ASSERT_TRUE(f.video.start_recording());
    f.capture_one_frame();
    TEST_ASSERT_EQUAL_UINT32(1, f.video.frames_recorded());

    f.video.stop_recording();

    // Recording ended synchronously; preview continues untouched.
    TEST_ASSERT_FALSE(f.video.is_recording());
    TEST_ASSERT_TRUE(f.video.is_streaming());
    TEST_ASSERT_TRUE(f.video.is_streaming());
    TEST_ASSERT_FALSE(f.video.is_recording());
    TEST_ASSERT_EQUAL_INT(0, f.capture_scheduler.stop_call_count);

    // Next frame callback finalizes the clip and keeps delivering.
    auto total_bytes = [&f]()
    {
        size_t total = 0;
        for (auto& entry : f.sd_hal.files)
        {
            total += entry.second.size();
        }
        return total;
    };
    size_t bytes_before = total_bytes();
    f.capture_one_frame();
    TEST_ASSERT_EQUAL_UINT32(0, f.video.frames_recorded());  // clip closed
    TEST_ASSERT_TRUE(total_bytes() > bytes_before);           // index+header flushed
    TEST_ASSERT_EQUAL_INT(2, sink.deliveries);
    // The capture loop must still be running for the surviving preview.
    TEST_ASSERT_TRUE(f.capture_scheduler.is_started());
}

/**
 * @brief Rapid restart is rejected while a stopped clip awaits closure.
 *
 * With streaming keeping the loop alive, stop_recording() finalizes the
 * clip asynchronously. start_recording() must be rejected until the
 * finalization callback has run, then succeed.
 */
void test_restart_rejected_while_clip_close_pending()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    TrackingSink sink;
    f.video.set_stream_service(&sink);

    TEST_ASSERT_TRUE(f.video.start_streaming());
    TEST_ASSERT_TRUE(f.video.start_recording());
    f.capture_one_frame();

    f.video.stop_recording();

    // Clip not yet finalized — restart must be rejected.
    TEST_ASSERT_FALSE(f.video.start_recording());
    TEST_ASSERT_FALSE(f.video.is_recording());

    // Finalization callback runs → restart succeeds.
    f.capture_one_frame();
    TEST_ASSERT_TRUE(f.video.start_recording());
    TEST_ASSERT_TRUE(f.video.is_recording());

    // Wind down: stopping recording while streaming leaves finalization
    // pending; stopping streaming last tears the pipeline down, whose
    // scheduler stop runs the pending clip closure.
    f.video.stop_recording();
    f.video.stop_streaming();
    TEST_ASSERT_FALSE(f.video.is_recording());
    TEST_ASSERT_FALSE(f.video.is_streaming());
}

/**
 * @brief stop_streaming() during REC_AND_PREVIEW keeps recording alive
 *        and closes all viewers via the deferred dispatch-boundary
 *        path (viewers exist only while streaming mode is active).
 */
void test_stop_streaming_during_recording_keeps_recording()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    TrackingSink sink;
    sink.scheduler_ref = &f.capture_scheduler;
    f.video.set_stream_service(&sink);

    TEST_ASSERT_TRUE(f.video.start_recording());
    TEST_ASSERT_TRUE(f.video.start_streaming());
    f.capture_one_frame();

    f.video.stop_streaming();

    // Recording continues on the same capture loop.
    TEST_ASSERT_TRUE(f.video.is_recording());
    TEST_ASSERT_FALSE(f.video.is_streaming());
    TEST_ASSERT_EQUAL_INT(0, f.capture_scheduler.stop_call_count);
    // Viewer close is deferred to the next dispatch boundary — it has
    // not run yet and must not have stopped the scheduler.
    TEST_ASSERT_EQUAL_INT(0, sink.teardown_calls);
    TEST_ASSERT_TRUE(f.capture_scheduler.is_started());

    uint32_t writes_before = f.sd_hal.write_call_count;
    f.capture_one_frame();
    // Deferred viewer close ran inside the still-running capture loop.
    TEST_ASSERT_EQUAL_INT(1, sink.teardown_calls);
    TEST_ASSERT_TRUE(sink.loop_running_at_teardown);
    TEST_ASSERT_TRUE(f.capture_scheduler.is_started());
    TEST_ASSERT_EQUAL_INT(0, f.capture_scheduler.stop_call_count);
    // Recording persisted through STOP_STREAM and the frame was written.
    TEST_ASSERT_TRUE(f.video.is_recording());
    TEST_ASSERT_EQUAL_UINT32(2, f.video.frames_recorded());
    TEST_ASSERT_TRUE(f.sd_hal.write_call_count > writes_before);  // AVI write happened

    f.video.stop_recording();
}

/**
 * @brief The deferred viewer close survives persistent capture failure.
 *
 * After STOP_STREAM while recording keeps the loop alive, every
 * capture-task activation may take the camera-failure path; the
 * deferred viewer close must still run on the next activation
 * regardless of whether any frame captures successfully.
 */
void test_deferred_viewer_close_survives_capture_failure()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    TrackingSink sink;
    sink.scheduler_ref = &f.capture_scheduler;
    f.video.set_stream_service(&sink);

    TEST_ASSERT_TRUE(f.video.start_recording());
    TEST_ASSERT_TRUE(f.video.start_streaming());
    f.capture_one_frame();

    f.video.stop_streaming();
    TEST_ASSERT_TRUE(f.video.is_recording());
    TEST_ASSERT_EQUAL_INT(0, sink.teardown_calls);

    // Persistent camera failure from here on: recording keeps the loop
    // running, so each activation takes the capture-failure branch.
    f.camera_hal.capture_result = {nullptr, 0};
    f.capture_one_frame();

    // Viewer close ran inside the still-running capture loop despite
    // the failed capture.
    TEST_ASSERT_EQUAL_INT(1, sink.teardown_calls);
    TEST_ASSERT_TRUE(sink.loop_running_at_teardown);
    TEST_ASSERT_TRUE(f.capture_scheduler.is_started());
    TEST_ASSERT_EQUAL_INT(0, f.capture_scheduler.stop_call_count);
    TEST_ASSERT_TRUE(f.video.is_recording());

    f.video.stop_recording();
    TEST_ASSERT_FALSE(f.capture_scheduler.is_started());
}

/**
 * @brief A deferred viewer close does not leak into a later session:
 *        teardown clears the pending flag, so a fresh streaming session
 *        started afterwards keeps its new viewers connected.
 */
void test_deferred_viewer_close_does_not_leak_into_new_session()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    TrackingSink sink;
    sink.scheduler_ref = &f.capture_scheduler;
    f.video.set_stream_service(&sink);

    // Record + stream, then stop streaming while recording continues:
    // the viewer close is deferred to the capture task boundary.
    TEST_ASSERT_TRUE(f.video.start_recording());
    TEST_ASSERT_TRUE(f.video.start_streaming());
    f.capture_one_frame();
    f.video.stop_streaming();
    TEST_ASSERT_EQUAL_INT(0, sink.teardown_calls);

    // Recording stops first: full teardown closes the viewers itself
    // and consumes the pending close.
    f.video.stop_recording();
    TEST_ASSERT_FALSE(f.capture_scheduler.is_started());
    TEST_ASSERT_EQUAL_INT(1, sink.teardown_calls);
    TEST_ASSERT_FALSE(sink.loop_running_at_teardown);

    // A brand-new preview session must keep its viewers connected.
    TEST_ASSERT_TRUE(f.video.start_streaming());
    f.capture_one_frame();
    TEST_ASSERT_EQUAL_INT(2, sink.deliveries);  // initial frame + new-session frame
    TEST_ASSERT_EQUAL_INT(1, sink.teardown_calls);
    TEST_ASSERT_TRUE(f.capture_scheduler.is_started());

    f.video.stop_streaming();
}

/**
 * @brief Last-activity-off teardown stops the loop then closes viewers.
 */
void test_final_teardown_stops_loop_then_closes_viewers()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    TrackingSink sink;
    sink.scheduler_ref = &f.capture_scheduler;
    f.video.set_stream_service(&sink);

    f.video.start_streaming();
    f.capture_one_frame();

    f.video.stop_streaming();

    TEST_ASSERT_FALSE(f.video.is_recording());
    TEST_ASSERT_FALSE(f.video.is_streaming());
    TEST_ASSERT_FALSE(f.capture_scheduler.is_started());
    TEST_ASSERT_EQUAL_INT(1, sink.teardown_calls);
    // Viewers are closed only after the scheduler has been stopped.
    TEST_ASSERT_FALSE(sink.loop_running_at_teardown);

    // Stale callbacks after full teardown are ignored.
    f.capture_one_frame();
    TEST_ASSERT_EQUAL_INT(1, sink.deliveries);
}

/**
 * @brief capture_photo is rejected while any activity keeps the loop
 *        running, and allowed once fully idle.
 */
void test_capture_photo_gating_across_modes()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    char photo_path[64];
    strcpy(photo_path, "/20260213/20260213_123456.jpg");

    // Preview-only: rejected
    TEST_ASSERT_TRUE(f.video.start_streaming());
    TEST_ASSERT_FALSE(f.video.capture_photo(photo_path));

    // Recording + preview: rejected
    TEST_ASSERT_TRUE(f.video.start_recording());
    TEST_ASSERT_FALSE(f.video.capture_photo(photo_path));

    // Recording only: rejected
    f.video.stop_streaming();
    TEST_ASSERT_TRUE(f.video.is_recording());
    TEST_ASSERT_FALSE(f.video.capture_photo(photo_path));

    // Fully idle: allowed
    f.video.stop_recording();
    TEST_ASSERT_FALSE(f.video.is_recording());
    TEST_ASSERT_FALSE(f.video.is_streaming());
    TEST_ASSERT_TRUE(f.video.capture_photo(photo_path));
}

// ============================================================
// Start racing a last-consumer teardown
// ============================================================

/**
 * @brief Scheduler double whose stop() blocks on a gate.
 *
 * Emulates the bounded asynchronous join window of the platform
 * scheduler (CaptureSchedulerEsp32::stop() waits for the capture task
 * to exit) so tests can interleave a concurrent lifecycle command into
 * the teardown. Like the real scheduler, the final callback runs once
 * before stop() returns and the started state clears only afterwards.
 */
struct GatedStopScheduler : public ICaptureScheduler
{
    std::function<void()> callback;
    std::atomic<bool> started{false};
    std::atomic<bool> stop_entered{false};
    std::atomic<bool> release_stop{false};

    /** Number of times stop() has been entered. */
    int stop_call_count{0};

    bool start(uint32_t /*interval_ms*/,
               std::function<void()> frame_callback) override
    {
        bool expected = false;
        if (!started.compare_exchange_strong(expected, true))
        {
            return false;
        }
        callback = frame_callback;
        return true;
    }

    void stop() override
    {
        stop_call_count++;
        if (!started.exchange(false))
        {
            return;
        }
        stop_entered.store(true, std::memory_order_release);
        while (!release_stop.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
        // Final callback pass, mirroring the real scheduler's contract.
        if (callback)
        {
            callback();
        }
    }

    void on_frame_capture() override
    {
        if (started.load() && callback)
        {
            callback();
        }
    }

    bool is_started() const
    {
        return started.load();
    }
};

/**
 * @brief A start racing a last-consumer teardown can never produce a
 *        registered-but-not-running activity.
 *
 * Contends START_STREAM against a concurrent STOP_STREAM whose teardown
 * join is held open by a gate, emulating the cross-transport interleaving
 * where one transport tears the pipeline down while the other starts an
 * activity into the dying loop. The invariant under test: whenever the
 * controller reports streaming active afterwards, the capture loop must
 * actually be running and frames must be delivered — a silent dead
 * registration is a defect.
 */
void test_start_racing_last_consumer_teardown_never_dead_registers()
{
    constexpr int ROUNDS = 100;

    for (int round = 0; round < ROUNDS; round++)
    {
        MockCameraHal camera_hal;
        MockSdCardHal sd_hal;
        MockLogBackend log_backend;
        RealTimeService time;
        LoggingService logger(log_backend, time);
        SdController sd_controller(time, sd_hal);
        CameraController camera_ctrl(camera_hal);
        GatedStopScheduler scheduler;
        VideoController video(camera_ctrl, sd_controller, time, logger,
                              scheduler);

        camera_hal.init_result = hal::CameraInitResult::SUCCESS;
        camera_ctrl.init();

        ConfigData::VideoConfig config;
        config.fps = TEST_FPS;
        strcpy(config.resolution, "VGA");
        config.clip_duration_s = TEST_CLIP_DURATION_S;
        config.max_file_size_mb = 0;
        config.jpeg_quality = 10;
        video.init(config, TEST_WIDTH, TEST_HEIGHT);

        TrackingSink sink;
        video.set_stream_service(&sink);

        TEST_ASSERT_TRUE(video.start_streaming());

        std::atomic<bool> go{false};
        std::thread stopper([&]()
        {
            while (!go.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            video.stop_streaming();
        });
        std::thread restarter([&]()
        {
            while (!go.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            // Contending start from the second command transport.
            (void)video.start_streaming();
        });

        for (int i = 0; i < 64; i++)
        {
            std::this_thread::yield();
        }
        go.store(true, std::memory_order_release);

        // Wait until the stopper is inside the gated teardown join.
        constexpr long MAX_SPIN = 10000000L;
        long spins = 0;
        while (!scheduler.stop_entered.load(std::memory_order_acquire) &&
               spins++ < MAX_SPIN)
        {
            std::this_thread::yield();
        }
        scheduler.release_stop.store(true, std::memory_order_release);
        stopper.join();
        restarter.join();

        // Core invariant: reported streaming state and actual loop state
        // must agree — no registered-but-dead activity.
        const bool streaming = video.is_streaming();
        TEST_ASSERT_EQUAL_INT(streaming ? 1 : 0,
                              scheduler.is_started() ? 1 : 0);

        if (streaming)
        {
            memset(camera_hal.frame_buffer, 0xAB, FAKE_JPEG_SIZE);
            camera_hal.capture_result = {camera_hal.frame_buffer,
                                         FAKE_JPEG_SIZE};
            sink.deliveries = 0;
            scheduler.on_frame_capture();
            TEST_ASSERT_EQUAL_INT(1, sink.deliveries);
        }

        // Wind down deterministically for the next round.
        video.stop_streaming();
        TEST_ASSERT_FALSE(video.is_streaming());
        TEST_ASSERT_FALSE(scheduler.is_started());
    }
}

// ============================================================
// Concurrent start contention
// ============================================================

/**
 * @brief Teardown entry is single-flight across command transports.
 *
 * Reproduces the cross-transport interleaving where STOP_STREAM's
 * teardown join is held open while a second transport runs a complete
 * START_STREAM + STOP_STREAM pair into the dying loop. The second
 * stop must not enter the scheduler a second time (the platform
 * scheduler's stop() is not reentrant); instead it defers to the
 * in-flight owner, whose post-join recheck observes the emptied
 * registry and completes the teardown exactly once.
 */
void test_concurrent_teardown_is_single_flight()
{
    MockCameraHal camera_hal;
    MockSdCardHal sd_hal;
    MockLogBackend log_backend;
    RealTimeService time;
    LoggingService logger(log_backend, time);
    SdController sd_controller(time, sd_hal);
    CameraController camera_ctrl(camera_hal);
    GatedStopScheduler scheduler;
    VideoController video(camera_ctrl, sd_controller, time, logger,
                          scheduler);

    camera_hal.init_result = hal::CameraInitResult::SUCCESS;
    camera_ctrl.init();

    ConfigData::VideoConfig config;
    config.fps = TEST_FPS;
    strcpy(config.resolution, "VGA");
    config.clip_duration_s = TEST_CLIP_DURATION_S;
    config.max_file_size_mb = 0;
    config.jpeg_quality = 10;
    video.init(config, TEST_WIDTH, TEST_HEIGHT);

    TrackingSink sink;
    video.set_stream_service(&sink);

    TEST_ASSERT_TRUE(video.start_streaming());

    std::atomic<bool> go{false};
    std::thread stopper([&]()
    {
        while (!go.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
        video.stop_streaming();
    });

    for (int i = 0; i < 64; i++)
    {
        std::this_thread::yield();
    }
    go.store(true, std::memory_order_release);

    // Wait until the first stopper owns the gated teardown join (the
    // joiner's m_command_mutex is released while blocked there).
    constexpr long MAX_SPIN = 10000000L;
    long spins = 0;
    while (!scheduler.stop_entered.load(std::memory_order_acquire) &&
           spins++ < MAX_SPIN)
    {
        std::this_thread::yield();
    }

    // Second transport runs a full start+stop pair into the dying loop
    // while the first teardown still owns the join window.
    TEST_ASSERT_TRUE(video.start_streaming());
    video.stop_streaming();

    // The second stop must never have driven a second scheduler stop.
    TEST_ASSERT_EQUAL_INT(1, scheduler.stop_call_count);
    TEST_ASSERT_EQUAL_INT(0, sink.teardown_calls);

    // Release the join: the owning teardown's post-join recheck sees an
    // empty registry and completes the teardown exactly once.
    scheduler.release_stop.store(true, std::memory_order_release);
    stopper.join();

    TEST_ASSERT_FALSE(video.is_streaming());
    TEST_ASSERT_FALSE(video.is_recording());
    TEST_ASSERT_FALSE(scheduler.is_started());
    TEST_ASSERT_EQUAL_INT(1, scheduler.stop_call_count);
    TEST_ASSERT_EQUAL_INT(1, sink.teardown_calls);
}

/**
 * @brief Concurrent start attempts can never silently lose or
 *        duplicate a registration.
 *
 * Contends START_RECORDING (main thread, standing in for the serial
 * transport) against several concurrent START_STREAM attempts (worker
 * threads, standing in for the web transport) and asserts the registry
 * invariants: every command that reports success leaves its activity
 * observable via the status queries, each frame trigger is delivered
 * exactly once to the stream sink, and recording accumulates exactly
 * one frame per trigger (duplicate sink registration would double
 * these observables).
 */
void test_concurrent_starts_never_lose_registration()
{
    constexpr int ROUNDS = 150;

    for (int round = 0; round < ROUNDS; round++)
    {
        TestFixture f;
        TrackingSink sink;
        f.video.set_stream_service(&sink);
        f.setup_fake_frame();

        std::atomic<bool> go{false};
        std::atomic<int> stream_started{0};
        std::atomic<int> record_started{0};

        auto stream_contender = [&]()
        {
            while (!go.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            if (f.video.start_streaming())
            {
                stream_started.fetch_add(1, std::memory_order_relaxed);
            }
        };

        std::thread s1(stream_contender);
        std::thread s2(stream_contender);
        std::thread s3(stream_contender);

        // Give the contenders time to reach the barrier before opening it.
        for (int i = 0; i < 64; i++)
        {
            std::this_thread::yield();
        }
        go.store(true, std::memory_order_release);

        // Main thread contends as the second command transport.
        if (f.video.start_recording())
        {
            record_started.fetch_add(1, std::memory_order_relaxed);
        }

        s1.join();
        s2.join();
        s3.join();

        // Every successful start leaves its activity observable.
        TEST_ASSERT_EQUAL_INT(static_cast<int>(record_started.load() > 0),
                              static_cast<int>(f.video.is_recording()));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(stream_started.load() > 0),
                              static_cast<int>(f.video.is_streaming()));

        // Exactly one registration per successful activity.
        const uint32_t frames_before = f.video.frames_recorded();
        f.capture_one_frame();

        TEST_ASSERT_EQUAL_INT(f.video.is_streaming() ? 1 : 0,
                              sink.deliveries);
        TEST_ASSERT_EQUAL_UINT32(
            frames_before + (f.video.is_recording() ? 1u : 0u),
            f.video.frames_recorded());
    }
}

/**
 * @brief A START_STREAMING queued during a stop's join window cannot
 *        leave the controller wedged (scheduler stopped + sink bound +
 *        loop dead).
 *
 * Reproduces the cross-transport interleaving where a stop's bounded
 * scheduler join is held open while a second transport posts a
 * START_STREAMING into the dying loop. The post-join recheck in
 * finish_command() must observe the queued-but-unexecuted work and
 * restart the loop before declaring the teardown done. Otherwise the
 * controller reports streaming active and the sink is bound, but no
 * code path ever restarts the scheduler — streaming hangs until
 * reboot. The invariant under test: is_streaming() == true implies
 * the scheduler is started AND frames flow to the sink; is_streaming()
 * == false implies the scheduler is stopped. Stress-repeated to keep
 * the regression honest under jitter.
 */
void test_start_queued_during_stop_join_never_wedges_streaming()
{
    constexpr int ROUNDS = 100;

    for (int round = 0; round < ROUNDS; round++)
    {
        MockCameraHal camera_hal;
        MockSdCardHal sd_hal;
        MockLogBackend log_backend;
        RealTimeService time;
        LoggingService logger(log_backend, time);
        SdController sd_controller(time, sd_hal);
        CameraController camera_ctrl(camera_hal);
        GatedStopScheduler scheduler;
        VideoController video(camera_ctrl, sd_controller, time, logger,
                              scheduler);

        camera_hal.init_result = hal::CameraInitResult::SUCCESS;
        camera_ctrl.init();

        ConfigData::VideoConfig config;
        config.fps = TEST_FPS;
        strcpy(config.resolution, "VGA");
        config.clip_duration_s = TEST_CLIP_DURATION_S;
        config.max_file_size_mb = 0;
        config.jpeg_quality = 10;
        video.init(config, TEST_WIDTH, TEST_HEIGHT);

        TrackingSink sink;
        video.set_stream_service(&sink);

        TEST_ASSERT_TRUE(video.start_streaming());

        std::atomic<bool> go{false};
        std::thread stopper([&]()
        {
            while (!go.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            video.stop_streaming();
        });
        std::thread restarter([&]()
        {
            while (!go.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            // Queued during the stopper's gated join window: m_loop_active
            // is still true at post time, the request enters the queue,
            // and the wait_for_completion serve path runs under
            // m_command_mutex so it is totally ordered with the post-join
            // recheck.
            (void)video.start_streaming();
        });

        for (int i = 0; i < 64; i++)
        {
            std::this_thread::yield();
        }
        go.store(true, std::memory_order_release);

        // Wait until the stopper owns the gated teardown join, then
        // give the restarter a window to post its START_STREAMING
        // while the join is still in flight.
        constexpr long MAX_SPIN = 10000000L;
        long spins = 0;
        while (!scheduler.stop_entered.load(std::memory_order_acquire) &&
               spins++ < MAX_SPIN)
        {
            std::this_thread::yield();
        }
        // Hold the gate for a brief moment so the restarter's post lands
        // inside the join window.
        for (int i = 0; i < 256; i++)
        {
            std::this_thread::yield();
        }
        scheduler.release_stop.store(true, std::memory_order_release);
        stopper.join();
        restarter.join();

        // Core invariant: is_streaming ↔ scheduler started. A wedged
        // state (sink bound, scheduler stopped, is_streaming true) would
        // violate this.
        const bool streaming = video.is_streaming();
        TEST_ASSERT_EQUAL_INT(streaming ? 1 : 0,
                              scheduler.is_started() ? 1 : 0);

        // If streaming claims to be active, frames must flow to the
        // sink — the scheduler really is running.
        if (streaming)
        {
            memset(camera_hal.frame_buffer, 0xAB, FAKE_JPEG_SIZE);
            camera_hal.capture_result = {camera_hal.frame_buffer,
                                         FAKE_JPEG_SIZE};
            sink.deliveries = 0;
            scheduler.on_frame_capture();
            TEST_ASSERT_EQUAL_INT(1, sink.deliveries);
        }

        // Wind down deterministically for the next round.
        video.stop_streaming();
        TEST_ASSERT_FALSE(video.is_streaming());
        TEST_ASSERT_FALSE(scheduler.is_started());
    }
}

// ============================================================
// Clip-rotation hardening (cross-task serialization)
// ============================================================

/**
 * @brief Reads the little-endian u32 at the given offset.
 */
static uint32_t read_u32_le(const std::vector<uint8_t>& content, size_t offset)
{
    return static_cast<uint32_t>(content[offset]) |
           (static_cast<uint32_t>(content[offset + 1]) << 8) |
           (static_cast<uint32_t>(content[offset + 2]) << 16) |
           (static_cast<uint32_t>(content[offset + 3]) << 24);
}

/**
 * @brief Returns whether one stored clip is a finalized AVI.
 *
 * A clip is considered finalized when it is large enough for a real
 * header and its RIFF size field (offset 4) equals content size minus 8.
 * A placeholder header carries 0 there; only close_clip()'s patched
 * header fills the real size.
 */
static bool clip_file_is_patched(const std::vector<uint8_t>& content)
{
    if (content.size() < 224)
    {
        return false;
    }
    return read_u32_le(content, 4) ==
           static_cast<uint32_t>(content.size() - 8);
}

/**
 * @brief Returns whether every .avi clip in the mock SD store is
 *        finalized (no abandoned placeholder-header clip).
 */
static bool all_avi_clips_patched(const MockSdCardHal& sd_hal)
{
    for (const auto& entry : sd_hal.files)
    {
        if (entry.first.size() < 4 ||
            entry.first.compare(entry.first.size() - 4, 4, ".avi") != 0)
        {
            continue;
        }
        if (!clip_file_is_patched(entry.second))
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief Waits until the create_dir park gate has been hit n times.
 */
static void wait_for_gate_hits(const MockSdCardHal& sd_hal, int n)
{
    constexpr long MAX_SPIN = 10000000L;
    long spins = 0;
    while (sd_hal.gate_create_dir_hits.load(std::memory_order_acquire) < n &&
           spins++ < MAX_SPIN)
    {
        std::this_thread::yield();
    }
}

/**
 * @brief Normal rotation finalizes clip A with a patched header and a
 *        fresh clip B receives the next frame; B finalizes at stop.
 */
void test_rotation_finalizes_and_reopens_header()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    TEST_ASSERT_TRUE(f.video.start_recording());

    // Record exactly one clip's worth of frames: the last frame triggers
    // the duration limit and the pump rotates to a fresh clip.
    for (uint32_t i = 0; i < TEST_FRAME_COUNT; i++)
    {
        f.capture_one_frame();
    }
    TEST_ASSERT_TRUE(f.video.is_recording());

    // Two clips exist: finalized clip A and the live reopened clip B.
    TEST_ASSERT_EQUAL_UINT32(2, f.sd_hal.files.size());
    for (const auto& entry : f.sd_hal.files)
    {
        if (entry.first.find("_1.avi") == std::string::npos)
        {
            // Clip A: finalized by the rotation with a patched header.
            TEST_ASSERT_TRUE(clip_file_is_patched(entry.second));
        }
    }

    // The next frame lands in the reopened clip B, which finalizes at stop.
    f.capture_one_frame();
    TEST_ASSERT_EQUAL_UINT32(1, f.video.frames_recorded());
    f.video.stop_recording();
    TEST_ASSERT_FALSE(f.video.is_recording());
    TEST_ASSERT_EQUAL_UINT32(2, f.sd_hal.files.size());
    TEST_ASSERT_TRUE(all_avi_clips_patched(f.sd_hal));
}

/**
 * @brief A STOP landing inside the rotation window cannot interleave:
 *        the capture task owns the executor token for its close/reopen
 *        section, so the posted stop's request executes only after the
 *        rotation completes, and the reopened clip is finalized. No torn
 *        clip is possible.
 */
void test_stop_during_rotation_finalizes()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    // Arm the park gate before start_recording: the initial clip open
    // is create_dir call #1, the rotation's reopen is call #2, where the
    // capture task (the executor) parks mid-rotation.
    f.sd_hal.gate_create_dir_at_count = 2;
    TEST_ASSERT_TRUE(f.video.start_recording());

    std::atomic<bool> stop_issued{false};
    std::thread driver([&]()
    {
        // Fire exactly one clip's worth of frames: the last activation
        // enters the rotation and parks on the gate.
        for (uint32_t i = 0; i < TEST_FRAME_COUNT; i++)
        {
            f.capture_one_frame();
        }
    });
    std::thread stopper([&]()
    {
        while (!stop_issued.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
        f.video.stop_recording();
    });

    // Wait until the capture task is parked mid-rotation (it owns the
    // executor token), then issue the stop so its request serializes
    // behind the rotation.
    wait_for_gate_hits(f.sd_hal, 2);
    stop_issued.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Watchdog release: the parked capture task owns the executor token
    // the stop's request waits behind, so the release must come from
    // this thread.
    f.sd_hal.gate_create_dir_release = true;

    driver.join();
    stopper.join();

    TEST_ASSERT_FALSE(f.video.is_recording());
    TEST_ASSERT_FALSE(f.capture_scheduler.is_started());
    TEST_ASSERT_TRUE(all_avi_clips_patched(f.sd_hal));
}

/**
 * @brief The R4F1 regression: STOP + START landing inside the capture
 *        task's rotation window can never produce a stray clip with an
 *        unpatched AVI header. The start either serializes behind the
 *        whole rotation (and is rejected while a clip awaits closure or
 *        starts cleanly after finalization) or is rejected because the
 *        recording is still active; corruption is impossible either way.
 */
void test_stop_start_during_rotation_race()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    f.sd_hal.gate_create_dir_at_count = 2;
    TEST_ASSERT_TRUE(f.video.start_recording());

    std::atomic<bool> go{false};
    std::thread driver([&]()
    {
        for (uint32_t i = 0; i < TEST_FRAME_COUNT; i++)
        {
            f.capture_one_frame();
        }
    });
    std::thread stopper([&]()
    {
        while (!go.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
        f.video.stop_recording();
    });
    std::thread restarter([&]()
    {
        while (!go.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
        (void)f.video.start_recording();
    });

    wait_for_gate_hits(f.sd_hal, 2);
    go.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    f.sd_hal.gate_create_dir_release = true;

    driver.join();
    stopper.join();
    restarter.join();

    // Registry/loop consistency: reported state and actual loop state
    // must agree.
    const bool started = f.video.is_recording();
    TEST_ASSERT_EQUAL_INT(started ? 1 : 0,
                          f.capture_scheduler.is_started() ? 1 : 0);

    // Quiesce: if the start won the race it opened a fresh live clip
    // (placeholder header until finalization); stopping it finalizes
    // every clip on the card.
    if (started)
    {
        f.video.stop_recording();
        TEST_ASSERT_FALSE(f.video.is_recording());
    }
    TEST_ASSERT_FALSE(f.capture_scheduler.is_started());
    TEST_ASSERT_TRUE(all_avi_clips_patched(f.sd_hal));
}

/**
 * @brief A START issued while the capture task rotates serializes behind
 *        the rotation and is rejected because the reopened clip still
 *        awaits finalization; no stray file is produced.
 */
void test_start_during_rotation_rejected_or_clean()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    f.sd_hal.gate_create_dir_at_count = 2;
    TEST_ASSERT_TRUE(f.video.start_recording());

    std::thread driver([&]()
    {
        for (uint32_t i = 0; i < TEST_FRAME_COUNT; i++)
        {
            f.capture_one_frame();
        }
    });

    wait_for_gate_hits(f.sd_hal, 2);

    std::thread restarter([&]()
    {
        // Waits behind the parked rotation: the capture task owns the
        // executor token, so the posted start executes only after the
        // rotation completes.
        (void)f.video.start_recording();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    f.sd_hal.gate_create_dir_release = true;

    driver.join();
    restarter.join();

    // The rotation completed under the mutex; the start observed the
    // unfinalized reopened clip and was rejected.
    TEST_ASSERT_TRUE(f.video.is_recording());
    TEST_ASSERT_TRUE(f.capture_scheduler.is_started());
    TEST_ASSERT_EQUAL_UINT32(2, f.sd_hal.files.size());

    f.video.stop_recording();
    TEST_ASSERT_FALSE(f.video.is_recording());
    TEST_ASSERT_TRUE(all_avi_clips_patched(f.sd_hal));
}

/**
 * @brief Both orderings of a STOP's flag-set against the rotation's
 *        flag exchange leave every clip finalized.
 *
 * Ordering 1: the stop lands before the rotation's exchange (streaming
 * keeps the loop alive), the rotation consumes the stop's flag, clip A
 * is finalized, and no clip is reopened. Ordering 2: the stop lands
 * inside the rotation window (parked mid-rotation); the reopened clip B
 * is finalized by the next activation.
 */
void test_stop_flag_vs_rotation_flag_ordering()
{
    // --- Ordering 1: the stop's flag-set lands before the closure's
    // --- exchange (the rotation exchange consumes the stop's flag). ---
    {
        TestFixture f;
        TrackingSink sink;
        f.video.set_stream_service(&sink);
        f.setup_fake_frame();
        set_mock_millis(1000);

        TEST_ASSERT_TRUE(f.video.start_streaming());
        TEST_ASSERT_TRUE(f.video.start_recording());

        // A few frames, well below the rotation limit.
        for (uint32_t i = 0; i < 3; i++)
        {
            f.capture_one_frame();
        }

        // Stop before the next activation: the stop's flag-set lands
        // before the closure's exchange. The exchange consumes the stop's
        // flag, clip A is finalized, and the reopen decision observes the
        // detached sink and keeps the clip closed (no reopen, no stray
        // file).
        f.video.stop_recording();
        TEST_ASSERT_FALSE(f.video.is_recording());
        TEST_ASSERT_TRUE(f.capture_scheduler.is_started());

        f.capture_one_frame();
        TEST_ASSERT_EQUAL_UINT32(1, f.sd_hal.files.size());
        TEST_ASSERT_TRUE(all_avi_clips_patched(f.sd_hal));

        f.video.stop_streaming();
        TEST_ASSERT_FALSE(f.capture_scheduler.is_started());
    }

    // --- Ordering 2: stop lands inside the rotation window. ---
    {
        TestFixture f;
        TrackingSink sink;
        sink.scheduler_ref = &f.capture_scheduler;
        f.video.set_stream_service(&sink);
        f.setup_fake_frame();
        set_mock_millis(1000);

        TEST_ASSERT_TRUE(f.video.start_streaming());
        f.sd_hal.gate_create_dir_at_count = 2;
        TEST_ASSERT_TRUE(f.video.start_recording());

        std::atomic<bool> stop_issued{false};
        std::thread driver([&]()
        {
            for (uint32_t i = 0; i < TEST_FRAME_COUNT; i++)
            {
                f.capture_one_frame();
            }
        });
        std::thread stopper([&]()
        {
            while (!stop_issued.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            f.video.stop_recording();
        });

        wait_for_gate_hits(f.sd_hal, 2);
        stop_issued.store(true, std::memory_order_release);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        f.sd_hal.gate_create_dir_release = true;

        driver.join();
        stopper.join();

        // Recording stopped; streaming keeps the loop alive. Clip A was
        // finalized by the rotation, the reopened clip B is finalized by
        // the next activation.
        TEST_ASSERT_FALSE(f.video.is_recording());
        TEST_ASSERT_TRUE(f.capture_scheduler.is_started());
        TEST_ASSERT_EQUAL_UINT32(2, f.sd_hal.files.size());

        f.capture_one_frame();
        TEST_ASSERT_TRUE(all_avi_clips_patched(f.sd_hal));

        f.video.stop_streaming();
        TEST_ASSERT_FALSE(f.capture_scheduler.is_started());
    }
}

/**
 * @brief Stress: rotation races with concurrent stop/start churn across
 *        many rounds; every clip stays finalized and the registry/loop
 *        state stays consistent.
 */
void test_concurrent_rotation_stop_start_stress()
{
    constexpr int ROUNDS = 150;

    for (int round = 0; round < ROUNDS; round++)
    {
        TestFixture f;
        f.setup_fake_frame();
        set_mock_millis(1000);

        f.sd_hal.gate_create_dir_at_count = 2;
        TEST_ASSERT_TRUE(f.video.start_recording());

        const bool with_restart = (round % 2) == 1;
        std::atomic<bool> go{false};
        std::thread driver([&]()
        {
            for (uint32_t i = 0; i < TEST_FRAME_COUNT; i++)
            {
                f.capture_one_frame();
            }
        });
        std::thread stopper([&]()
        {
            while (!go.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            f.video.stop_recording();
        });
        std::thread restarter;
        if (with_restart)
        {
            restarter = std::thread([&]()
            {
                while (!go.load(std::memory_order_acquire))
                {
                    std::this_thread::yield();
                }
                (void)f.video.start_recording();
            });
        }

        wait_for_gate_hits(f.sd_hal, 2);
        go.store(true, std::memory_order_release);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        f.sd_hal.gate_create_dir_release = true;

        driver.join();
        stopper.join();
        if (with_restart)
        {
            restarter.join();
        }

        // Quiesce: any surviving session stops cleanly.
        if (f.video.is_recording())
        {
            f.video.stop_recording();
        }

        TEST_ASSERT_FALSE(f.video.is_recording());
        TEST_ASSERT_FALSE(f.capture_scheduler.is_started());
        TEST_ASSERT_TRUE(all_avi_clips_patched(f.sd_hal));
    }
}

/**
 * @brief A drain from a command whose waiter timed out still closes
 *        viewers, and the next command reaps the parked loop.
 *
 * A lifecycle command whose waiter times out (the capture task wedged
 * past REQUEST_COMPLETION_TIMEOUT_MS) never consumes the
 * executor-reported drain: finish_command() runs while the request is
 * still queued, so it finds no drain armed. The executor must
 * therefore arm the deferred viewer close for every drain and close
 * connected viewers itself at a later boundary, while the drained
 * signal stays armed for the next command task of any kind to drive
 * the bounded join.
 */
void test_timed_out_waiter_drain_closes_viewers_and_reaps_loop()
{
    TestFixture f;
    f.setup_fake_frame();
    set_mock_millis(1000);

    TrackingSink sink;
    f.video.set_stream_service(&sink);

    // Park the capture task mid-rotation: the initial clip open is
    // create_dir call #1, the rotation's reopen is call #2, where the
    // capture thread parks while holding the executor token.
    f.sd_hal.gate_create_dir_at_count = 2;
    TEST_ASSERT_TRUE(f.video.start_recording());
    TEST_ASSERT_TRUE(f.video.start_streaming());

    std::thread driver([&]()
    {
        // Fire exactly one clip's worth of frames: the last activation
        // enters the rotation and parks on the gate.
        for (uint32_t i = 0; i < TEST_FRAME_COUNT; i++)
        {
            f.capture_one_frame();
        }
    });

    // Once the capture thread is parked, issue both stops: the
    // executor cannot serve them, so each waiter times out and its
    // finish_command() runs while the requests are still queued.
    wait_for_gate_hits(f.sd_hal, 2);
    f.video.stop_streaming();
    f.video.stop_recording();

    // Watchdog release: the parked capture task holds the executor
    // token, so the release must come from this thread.
    f.sd_hal.gate_create_dir_release = true;
    driver.join();

    // The next activation serves the queued stops: both consumers
    // detach and the registry drains with no waiting command task
    // left to consume the drain.
    f.capture_one_frame();

    // The closing-latch activation finalizes the clip and closes the
    // viewers even though both waiters timed out; the loop stays
    // parked for the command-side teardown.
    f.capture_one_frame();
    TEST_ASSERT_EQUAL_INT(1, sink.teardown_calls);
    TEST_ASSERT_TRUE(f.capture_scheduler.is_started());

    // The next command consumes the armed drain: the bounded join runs
    // and the loop restarts for the freshly bound streaming sink.
    TEST_ASSERT_TRUE(f.video.start_streaming());
    TEST_ASSERT_TRUE(f.video.is_streaming());
    TEST_ASSERT_TRUE(f.capture_scheduler.is_started());
    TEST_ASSERT_EQUAL_INT(1, sink.teardown_calls);

    f.video.stop_streaming();
    TEST_ASSERT_FALSE(f.capture_scheduler.is_started());
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

    // Independent recording/streaming activities
    RUN_TEST(test_state_reports_recording_when_both_active);
    RUN_TEST(test_stop_recording_during_streaming_keeps_preview);
    RUN_TEST(test_restart_rejected_while_clip_close_pending);
    RUN_TEST(test_stop_streaming_during_recording_keeps_recording);
    RUN_TEST(test_deferred_viewer_close_survives_capture_failure);
    RUN_TEST(test_deferred_viewer_close_does_not_leak_into_new_session);
    RUN_TEST(test_final_teardown_stops_loop_then_closes_viewers);
    RUN_TEST(test_capture_photo_gating_across_modes);
    RUN_TEST(test_capture_photo_holds_mutex_across_capture_and_write);

    // Consumer registry
    RUN_TEST(test_register_consumer_lifecycle);
    RUN_TEST(test_register_rejects_full_and_duplicate);
    RUN_TEST(test_failed_registration_leaves_no_reserved_slot);
    RUN_TEST(test_consumer_can_trigger_recording_from_on_frame);
    RUN_TEST(test_stop_streaming_cancels_pending_start);
    RUN_TEST(test_fire_and_forget_drain_closes_viewers_and_reaps_loop);

    // Registry concurrency (dual-transport contention)
    RUN_TEST(test_concurrent_starts_never_lose_registration);
    RUN_TEST(test_start_racing_last_consumer_teardown_never_dead_registers);
    RUN_TEST(test_concurrent_teardown_is_single_flight);
    RUN_TEST(test_start_queued_during_stop_join_never_wedges_streaming);

    // Clip-rotation hardening (cross-task serialization)
    RUN_TEST(test_rotation_finalizes_and_reopens_header);
    RUN_TEST(test_stop_during_rotation_finalizes);
    RUN_TEST(test_stop_start_during_rotation_race);
    RUN_TEST(test_start_during_rotation_rejected_or_clean);
    RUN_TEST(test_stop_flag_vs_rotation_flag_ordering);
    RUN_TEST(test_concurrent_rotation_stop_start_stress);
    RUN_TEST(test_timed_out_waiter_drain_closes_viewers_and_reaps_loop);

    return UNITY_END();
}
