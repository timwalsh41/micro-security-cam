#include <unity.h>
#include "system_orchestrator.h"
#include "command_handler.h"
#include "video_controller.h"
#include "camera_controller.h"
#include "time_service.h"
#include "config_service.h"
#include "logging_service.h"
#include "sd_controller.h"
#include "mock_camera_hal.h"
#include "mock_sd_card_hal.h"
#include "mock_capture_scheduler.h"
#include "mock_log_backend.h"
#include <cstring>

// ============================================================
// Test fixture
// ============================================================

struct TestFixture
{
    MockCameraHal camera_hal;
    MockSdCardHal sd_hal;
    MockLogBackend log_backend;
    RealTimeService time;
    SdController sd_controller;
    CameraController camera_ctrl;
    MockCaptureScheduler capture_scheduler;
    LoggingService logger;
    VideoController video;
    ConfigService config;
    CommandHandler cmd_handler;
    SystemOrchestrator orchestrator;

    TestFixture()
        : sd_controller(time, sd_hal)
        , camera_ctrl(camera_hal)
        , logger(log_backend, time)
        , video(camera_ctrl, sd_controller, time, logger, capture_scheduler)
        , config(logger)
        , cmd_handler(logger)
        , orchestrator(video, camera_ctrl, cmd_handler, time, config, logger)
    {
        camera_hal.init_result = hal::CameraInitResult::SUCCESS;
        camera_ctrl.init();

        ConfigData::VideoConfig vconfig;
        vconfig.fps = 10;
        strcpy(vconfig.resolution, "VGA");
        vconfig.clip_duration_s = 60;
        vconfig.max_file_size_mb = 0;
        vconfig.jpeg_quality = 10;
        video.init(vconfig, 640, 480);

        orchestrator.init();
    }

    /**
     * @brief Configures the mock camera to return a fake JPEG frame.
     */
    void setup_fake_frame()
    {
        memset(camera_hal.frame_buffer, 0xAB, 64);
        camera_hal.capture_result = {camera_hal.frame_buffer, 64};
    }

    /**
     * @brief Drives a single frame capture via the mock scheduler.
     */
    void capture_one_frame()
    {
        capture_scheduler.on_frame_capture();
    }
};

// ============================================================
// Helpers
// ============================================================

static CommandResult dispatch_cmd(
    CommandHandler& handler, CommandCode code,
    const uint8_t* payload = nullptr, size_t len = 0,
    uint8_t* response = nullptr, size_t* responseLen = nullptr)
{
    size_t dummy_len = 0;
    if (responseLen == nullptr)
    {
        responseLen = &dummy_len;
    }
    return handler.dispatch(code, payload, len, response, *responseLen);
}

// ============================================================
// Tests
// ============================================================

/**
 * @brief After init(), dispatching GET_STATUS returns SUCCESS.
 */
void test_init_registers_commands()
{
    TestFixture f;

    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;

    CommandResult result = f.cmd_handler.dispatch(
        CommandCode::GET_STATUS, nullptr, 0, response, response_len);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SUCCESS),
        static_cast<uint8_t>(result));
}

/**
 * @brief GET_STATUS returns a JSON response with recording state.
 */
void test_get_status_returns_json()
{
    TestFixture f;

    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;

    CommandResult result = f.cmd_handler.dispatch(
        CommandCode::GET_STATUS, nullptr, 0, response, response_len);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SUCCESS),
        static_cast<uint8_t>(result));

    // Verify JSON structure
    const char* json = reinterpret_cast<const char*>(response);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"recording\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"state\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"frames\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"file_size\""));
}

/**
 * @brief START_RECORDING delegates to VideoController.
 */
void test_start_recording_delegates()
{
    TestFixture f;

    TEST_ASSERT_FALSE(f.video.is_recording());

    CommandResult result = dispatch_cmd(
        f.cmd_handler, CommandCode::START_RECORDING);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SUCCESS),
        static_cast<uint8_t>(result));
    TEST_ASSERT_TRUE(f.video.is_recording());
}

/**
 * @brief START_RECORDING returns INVALID_STATE if already recording.
 */
void test_start_recording_fails_if_already_recording()
{
    TestFixture f;

    // Start recording first
    dispatch_cmd(f.cmd_handler, CommandCode::START_RECORDING);
    TEST_ASSERT_TRUE(f.video.is_recording());

    // Try again
    CommandResult result = dispatch_cmd(
        f.cmd_handler, CommandCode::START_RECORDING);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::INVALID_STATE),
        static_cast<uint8_t>(result));
}

/**
 * @brief STOP_RECORDING delegates to VideoController.
 *
 * Note: stop_recording() sets a flag; the actual state transition
 * to IDLE happens on the next on_frame_capture(). We call
 * capture_one_frame() here to complete the transition before
 * asserting.
 */
void test_stop_recording_delegates()
{
    TestFixture f;
    f.setup_fake_frame();

    dispatch_cmd(f.cmd_handler, CommandCode::START_RECORDING);
    TEST_ASSERT_TRUE(f.video.is_recording());

    CommandResult result = dispatch_cmd(
        f.cmd_handler, CommandCode::STOP_RECORDING);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SUCCESS),
        static_cast<uint8_t>(result));

    // Capture one frame to complete the stop transition
    f.capture_one_frame();
    TEST_ASSERT_FALSE(f.video.is_recording());
}

/**
 * @brief TAKE_PHOTO captures a frame and saves to SD.
 */
void test_take_photo_captures_frame()
{
    TestFixture f;
    set_mock_millis(1234567890u);

    // Configure mock camera to return a frame
    memset(f.camera_hal.frame_buffer, 0x42, 64);
    f.camera_hal.capture_result = {f.camera_hal.frame_buffer, 64};

    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;

    CommandResult result = f.cmd_handler.dispatch(
        CommandCode::TAKE_PHOTO, nullptr, 0, response, response_len);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SUCCESS),
        static_cast<uint8_t>(result));

    // Verify the camera captured a frame
    TEST_ASSERT_GREATER_THAN(0, f.camera_hal.capture_call_count);

    // Verify response contains a path
    TEST_ASSERT_GREATER_THAN(0u, response_len);
}

/**
 * @brief TAKE_PHOTO returns INVALID_STATE when recording.
 */
void test_take_photo_fails_when_recording()
{
    TestFixture f;
    set_mock_millis(1000);

    dispatch_cmd(f.cmd_handler, CommandCode::START_RECORDING);
    TEST_ASSERT_TRUE(f.video.is_recording());

    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;

    CommandResult result = f.cmd_handler.dispatch(
        CommandCode::TAKE_PHOTO, nullptr, 0, response, response_len);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::INVALID_STATE),
        static_cast<uint8_t>(result));
}

/**
 * @brief SET_TIME sets the unix time offset.
 */
void test_set_time_sets_offset()
{
    TestFixture f;

    uint32_t unix_time = 1234567890;
    uint8_t payload[4];
    memcpy(payload, &unix_time, sizeof(payload));

    CommandResult result = dispatch_cmd(
        f.cmd_handler, CommandCode::SET_TIME,
        payload, sizeof(payload));

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SUCCESS),
        static_cast<uint8_t>(result));
}

/**
 * @brief SET_TIME with short payload returns INVALID_ARGS.
 */
void test_set_time_invalid_payload()
{
    TestFixture f;

    uint8_t payload[2] = {0x00, 0x01};

    CommandResult result = dispatch_cmd(
        f.cmd_handler, CommandCode::SET_TIME,
        payload, sizeof(payload));

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::INVALID_ARGS),
        static_cast<uint8_t>(result));
}

/**
 * @brief SET_CONFIG returns INVALID_STATE (deferred to Slice 5).
 */
void test_set_config_returns_invalid_state()
{
    TestFixture f;

    CommandResult result = dispatch_cmd(
        f.cmd_handler, CommandCode::SET_CONFIG);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::INVALID_STATE),
        static_cast<uint8_t>(result));
}

/**
 * @brief tick() does not crash.
 */
void test_tick_does_not_crash()
{
    TestFixture f;
    f.setup_fake_frame();

    // Should be safe to call tick() in any state
    f.orchestrator.tick();

    // Start recording, tick again
    dispatch_cmd(f.cmd_handler, CommandCode::START_RECORDING);
    f.orchestrator.tick();

    // Capture a frame during recording
    f.capture_one_frame();

    // Stop, tick again
    dispatch_cmd(f.cmd_handler, CommandCode::STOP_RECORDING);
    f.capture_one_frame();  // Completes the stop transition
    f.orchestrator.tick();

    TEST_ASSERT_TRUE(true);  // If we got here, no crash
}

// ============================================================
// Test runner
// ============================================================

int main()
{
    UNITY_BEGIN();

    RUN_TEST(test_init_registers_commands);
    RUN_TEST(test_get_status_returns_json);
    RUN_TEST(test_start_recording_delegates);
    RUN_TEST(test_start_recording_fails_if_already_recording);
    RUN_TEST(test_stop_recording_delegates);
    RUN_TEST(test_take_photo_captures_frame);
    RUN_TEST(test_take_photo_fails_when_recording);
    RUN_TEST(test_set_time_sets_offset);
    RUN_TEST(test_set_time_invalid_payload);
    RUN_TEST(test_set_config_returns_invalid_state);
    RUN_TEST(test_tick_does_not_crash);

    return UNITY_END();
}
