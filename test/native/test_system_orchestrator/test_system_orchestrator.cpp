#include <unity.h>
#include "system_orchestrator.h"
#include "stream_service.h"
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
#include "mock_web_server_hal.h"
#include "mock_nvs_hal.h"
#include "mock_restart_hal.h"
#include "mock_hardware_peripheral_manager.h"
#include "mock_wifi_hal.h"
#include "wifi_service.h"
#include "time_formatter.h"
#include "version.h"
#include <cstring>
#include <memory>
#include <string>

// ============================================================
// Test fixture
// ============================================================

struct TestFixture
{
    MockCameraHal camera_hal;
    MockSdCardHal sd_hal;
    MockNvsHal nvs_hal;
    MockLogBackend log_backend;
    RealTimeService time;
    SdController sd_controller;
    CameraController camera_ctrl;
    MockCaptureScheduler capture_scheduler;
    LoggingService logger;
    VideoController video;
    ConfigService config;
    CommandHandler cmd_handler;
    StreamService stream_service;
    MockWebServerHal web_hal;
    MockRestartHal restart_hal;
    MockHardwarePeripheralManager hw_periph;
    MockWifiHal wifi_hal;
    WifiService wifi_service;
    SystemOrchestrator orchestrator;

    /**
     * @brief Constructs the fixture.
     *
     * @param web_password  Web password to register (nullptr for none).
     * @param sd_init_result SD init result; non-SUCCESS latches the
     *                      orchestrator into streaming-only mode,
     *                      mirroring the degraded boot composition.
     */
    explicit TestFixture(const char* web_password = nullptr,
                         hal::SdInitResult sd_init_result =
                             hal::SdInitResult::SUCCESS)
        : sd_controller(time, sd_hal)
        , camera_ctrl(camera_hal)
        , logger(log_backend, time)
        , video(camera_ctrl, sd_controller, time, logger, capture_scheduler)
        , config(logger)
        , cmd_handler(logger)
        , stream_service(logger)
        , wifi_service(wifi_hal, time, logger)
        , orchestrator(video, camera_ctrl, cmd_handler, time, config, logger,
                       stream_service, web_hal, sd_controller, nvs_hal,
                       restart_hal, hw_periph, wifi_service)
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

        stream_service.init(4);

        // Mirror main.cpp composition: the video controller delivers
        // frames to (and tears down) the shared stream service.
        video.set_stream_service(&stream_service);

        // SD card initialization precedes orchestrator init in the
        // composition root; the orchestrator latches availability
        // from the controller at init().
        sd_hal.init_result = sd_init_result;
        sd_controller.init_card();

        // Config load precedes orchestrator init in the composition
        // root; a configured web password must be visible to the
        // /stream route registration snapshot.
        if (web_password != nullptr)
        {
            config.set_config_value("web.password", web_password);
        }

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

    // Verify JSON structure — activity fields only, with no "state" field.
    const char* json = reinterpret_cast<const char*>(response);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"recording\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"streaming\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"frames\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"file_size\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"sd_free_bytes\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"photos\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"videos\""));
    TEST_ASSERT_NULL(strstr(json, "\"state\""));
}

/**
 * @brief TAKE_PHOTO succeeds during streaming-only preview.
 *
 * Photos ride the capture executor, so the loop being up for any
 * consumer no longer refuses a photo; the response still carries the
 * saved path and streaming continues afterwards.
 */
void test_take_photo_succeeds_during_streaming_only()
{
    TestFixture f;
    f.setup_fake_frame();

    dispatch_cmd(f.cmd_handler, CommandCode::START_STREAM);
    TEST_ASSERT_TRUE(f.video.is_streaming());

    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;
    CommandResult result = dispatch_cmd(
        f.cmd_handler, CommandCode::TAKE_PHOTO, nullptr, 0,
        response, &response_len);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SUCCESS),
        static_cast<uint8_t>(result));
    TEST_ASSERT_GREATER_THAN(0u, response_len);
    TEST_ASSERT_TRUE(f.sd_hal.files.size() == 1);
    TEST_ASSERT_TRUE(f.video.is_streaming());
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
 * @brief STOP_RECORDING closes connected stream viewers.
 *
 * Regression pin for the stream-hang bug: when recording stops and
 * streaming mode is not active, the capture loop is torn down and no
 * further frames are delivered. Any viewer still connected would hang
 * forever on a silent multipart response, so the orchestrator must
 * ensure the viewers are closed.
 */
void test_stop_recording_closes_connected_viewers()
{
    TestFixture f;
    f.setup_fake_frame();

    // Recording delivers frames to streams too; connect a viewer.
    dispatch_cmd(f.cmd_handler, CommandCode::START_RECORDING);
    auto channel = std::make_shared<StreamFrameChannel>();
    StreamFrameChannel* raw = channel.get();
    f.stream_service.start_stream(channel);
    TEST_ASSERT_EQUAL_UINT8(1, f.stream_service.active_slot_count());

    dispatch_cmd(f.cmd_handler, CommandCode::STOP_RECORDING);
    f.capture_one_frame();
    TEST_ASSERT_FALSE(f.video.is_recording());

    // The viewer's stream must have been closed and its slot freed.
    TEST_ASSERT_EQUAL_UINT8(0, f.stream_service.active_slot_count());
    TEST_ASSERT_TRUE(raw->is_closed());
}

/**
 * @brief STOP_RECORDING keeps viewers open while streaming mode is on.
 *
 * When the user explicitly started streaming, stopping only the
 * recording must leave the capture loop and connected viewers running.
 */
void test_stop_recording_keeps_streams_when_streaming_active()
{
    TestFixture f;
    f.setup_fake_frame();

    dispatch_cmd(f.cmd_handler, CommandCode::START_STREAM);
    auto channel = std::make_shared<StreamFrameChannel>();
    StreamFrameChannel* raw = channel.get();
    f.stream_service.start_stream(channel);

    dispatch_cmd(f.cmd_handler, CommandCode::START_RECORDING);
    dispatch_cmd(f.cmd_handler, CommandCode::STOP_RECORDING);
    f.capture_one_frame();

    TEST_ASSERT_FALSE(f.video.is_recording());
    TEST_ASSERT_TRUE(f.video.is_streaming());
    TEST_ASSERT_EQUAL_UINT8(1, f.stream_service.active_slot_count());
    TEST_ASSERT_FALSE(raw->is_closed());

    // Cleanup: stop streaming closes the viewer.
    dispatch_cmd(f.cmd_handler, CommandCode::STOP_STREAM);
    TEST_ASSERT_EQUAL_UINT8(0, f.stream_service.active_slot_count());
    TEST_ASSERT_TRUE(raw->is_closed());
}

/**
 * @brief GET_STATUS reports both recording and streaming fields.
 */
void test_get_status_reports_streaming_field()
{
    TestFixture f;
    f.setup_fake_frame();

    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;

    // Both activities active → recording:true streaming:true.
    dispatch_cmd(f.cmd_handler, CommandCode::START_STREAM);
    dispatch_cmd(f.cmd_handler, CommandCode::START_RECORDING);

    CommandResult result = f.cmd_handler.dispatch(
        CommandCode::GET_STATUS, nullptr, 0, response, response_len);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SUCCESS),
        static_cast<uint8_t>(result));
    TEST_ASSERT_NOT_NULL(strstr(reinterpret_cast<char*>(response),
                                "\"recording\":true"));
    TEST_ASSERT_NOT_NULL(strstr(reinterpret_cast<char*>(response),
                                "\"streaming\":true"));
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
 * @brief TAKE_PHOTO succeeds while recording; the response carries the
 *        saved path and the recording continues afterwards.
 */
void test_take_photo_succeeds_while_recording()
{
    TestFixture f;
    set_mock_millis(1000);

    f.setup_fake_frame();

    dispatch_cmd(f.cmd_handler, CommandCode::START_RECORDING);
    TEST_ASSERT_TRUE(f.video.is_recording());

    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;

    CommandResult result = f.cmd_handler.dispatch(
        CommandCode::TAKE_PHOTO, nullptr, 0, response, response_len);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SUCCESS),
        static_cast<uint8_t>(result));
    TEST_ASSERT_GREATER_THAN(0u, response_len);

    // The response body is the saved photo path, and the file exists.
    std::string photo_path(reinterpret_cast<char*>(response), response_len);
    TEST_ASSERT_TRUE(f.sd_hal.files.count(photo_path));
    TEST_ASSERT_TRUE(f.video.is_recording());
}

/**
 * @brief TAKE_PHOTO succeeds while recording and streaming are both
 *        active, and both activities continue afterwards.
 */
void test_take_photo_succeeds_while_recording_and_streaming()
{
    TestFixture f;
    set_mock_millis(1000);

    f.setup_fake_frame();

    dispatch_cmd(f.cmd_handler, CommandCode::START_RECORDING);
    dispatch_cmd(f.cmd_handler, CommandCode::START_STREAM);
    TEST_ASSERT_TRUE(f.video.is_recording());
    TEST_ASSERT_TRUE(f.video.is_streaming());

    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;

    CommandResult result = f.cmd_handler.dispatch(
        CommandCode::TAKE_PHOTO, nullptr, 0, response, response_len);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SUCCESS),
        static_cast<uint8_t>(result));
    TEST_ASSERT_GREATER_THAN(0u, response_len);

    std::string photo_path(reinterpret_cast<char*>(response), response_len);
    TEST_ASSERT_TRUE(f.sd_hal.files.count(photo_path));
    TEST_ASSERT_TRUE(f.video.is_recording());
    TEST_ASSERT_TRUE(f.video.is_streaming());
}

/**
 * @brief TAKE_PHOTO when idle is unchanged: same response shape, and
 *        the photo-request drain tears the loop back down afterwards.
 */
void test_take_photo_when_idle_unchanged()
{
    TestFixture f;
    set_mock_millis(1000);

    f.setup_fake_frame();

    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;

    CommandResult result = f.cmd_handler.dispatch(
        CommandCode::TAKE_PHOTO, nullptr, 0, response, response_len);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SUCCESS),
        static_cast<uint8_t>(result));
    TEST_ASSERT_GREATER_THAN(0u, response_len);
    TEST_ASSERT_TRUE(f.sd_hal.files.size() == 1);
    TEST_ASSERT_FALSE(f.video.is_recording());
    TEST_ASSERT_FALSE(f.video.is_streaming());
    TEST_ASSERT_FALSE(f.capture_scheduler.is_started());
}

/**
 * @brief TAKE_PHOTO returns INTERNAL_ERROR and no path when the camera
 *        capture fails — synchronously, also while streaming is active.
 */
void test_take_photo_camera_failure()
{
    TestFixture f;
    set_mock_millis(1234567890u);

    // No frame configured: capture_frame() fails.
    dispatch_cmd(f.cmd_handler, CommandCode::START_STREAM);
    TEST_ASSERT_TRUE(f.video.is_streaming());

    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;

    CommandResult result = f.cmd_handler.dispatch(
        CommandCode::TAKE_PHOTO, nullptr, 0, response, response_len);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::INTERNAL_ERROR),
        static_cast<uint8_t>(result));
    TEST_ASSERT_EQUAL_UINT32(0, response_len);
    TEST_ASSERT_TRUE(f.sd_hal.files.empty());
}

/**
 * @brief TAKE_PHOTO returns INTERNAL_ERROR and no path when SD write fails.
 */
void test_take_photo_sd_write_failure()
{
    TestFixture f;
    set_mock_millis(1234567890u);

    memset(f.camera_hal.frame_buffer, 0x42, 64);
    f.camera_hal.capture_result = {f.camera_hal.frame_buffer, 64};
    f.sd_hal.write_result = false;

    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;

    CommandResult result = f.cmd_handler.dispatch(
        CommandCode::TAKE_PHOTO, nullptr, 0, response, response_len);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::INTERNAL_ERROR),
        static_cast<uint8_t>(result));

    // No path echoed in the response
    TEST_ASSERT_EQUAL_UINT32(0, response_len);
}

/**
 * @brief Two photos within the same second get distinct, non-clobbering paths.
 *
 * The second photo must be written to a uniquified path (_1 suffix)
 * rather than overwriting the first.
 */
void test_take_photo_uniquifies_same_second_collision()
{
    TestFixture f;
    set_mock_millis(1234567890u);

    memset(f.camera_hal.frame_buffer, 0x42, 64);
    f.camera_hal.capture_result = {f.camera_hal.frame_buffer, 64};

    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;

    // First photo
    CommandResult result1 = f.cmd_handler.dispatch(
        CommandCode::TAKE_PHOTO, nullptr, 0, response, response_len);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SUCCESS),
        static_cast<uint8_t>(result1));
    std::string first_path(reinterpret_cast<char*>(response), response_len);
    TEST_ASSERT_GREATER_THAN(0u, first_path.size());

    // Second photo — same timestamp
    CommandResult result2 = f.cmd_handler.dispatch(
        CommandCode::TAKE_PHOTO, nullptr, 0, response, response_len);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SUCCESS),
        static_cast<uint8_t>(result2));
    std::string second_path(reinterpret_cast<char*>(response), response_len);

    // Both files exist and are distinct
    TEST_ASSERT_EQUAL_UINT32(2, f.sd_hal.files.size());
    TEST_ASSERT_NOT_EQUAL(0, strcmp(first_path.c_str(), second_path.c_str()));
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
 * @brief SET_CONFIG without a destination argument is rejected.
 */
void test_set_config_requires_destination()
{
    TestFixture f;

    CommandResult result = dispatch_cmd(
        f.cmd_handler, CommandCode::SET_CONFIG);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::INVALID_ARGS),
        static_cast<uint8_t>(result));
}

/**
 * @brief SET_CONFIG with an unknown destination is rejected.
 */
void test_set_config_unknown_destination()
{
    TestFixture f;

    CommandResult result = dispatch_cmd(
        f.cmd_handler, CommandCode::SET_CONFIG,
        reinterpret_cast<const uint8_t*>("floppy"), 6);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::INVALID_ARGS),
        static_cast<uint8_t>(result));
}

/**
 * @brief SET_CONFIG with destination nvs persists the live config.
 */
void test_set_config_nvs_persists_live_config()
{
    TestFixture f;
    f.config.set_config_value("wifi.ssid", "Home");

    CommandResult result = dispatch_cmd(
        f.cmd_handler, CommandCode::SET_CONFIG,
        reinterpret_cast<const uint8_t*>("nvs"), 3);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SUCCESS),
        static_cast<uint8_t>(result));
    // Two blob writes: the config itself and the schema version.
    TEST_ASSERT_EQUAL_INT(2, f.nvs_hal.write_call_count);
    auto it = f.nvs_hal.blobs.find(ConfigService::NVS_KEY);
    TEST_ASSERT_TRUE(it != f.nvs_hal.blobs.end());
    std::string ini(reinterpret_cast<const char*>(it->second.data()),
                    it->second.size());
    TEST_ASSERT_NOT_NULL(strstr(ini.c_str(), "[wifi]"));
    TEST_ASSERT_NOT_NULL(strstr(ini.c_str(), "ssid=Home"));
}

/**
 * @brief SET_CONFIG destination argument is case-insensitive.
 */
void test_set_config_destination_case_insensitive()
{
    TestFixture f;

    CommandResult nvs_result = dispatch_cmd(
        f.cmd_handler, CommandCode::SET_CONFIG,
        reinterpret_cast<const uint8_t*>("NVS"), 3);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SUCCESS),
        static_cast<uint8_t>(nvs_result));
    TEST_ASSERT_EQUAL_INT(2, f.nvs_hal.write_call_count);

    CommandResult sd_result = dispatch_cmd(
        f.cmd_handler, CommandCode::SET_CONFIG,
        reinterpret_cast<const uint8_t*>("Sd"), 2);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SUCCESS),
        static_cast<uint8_t>(sd_result));
    TEST_ASSERT_TRUE(f.sd_hal.files.find(ConfigService::DEFAULT_CONFIG_PATH) !=
                     f.sd_hal.files.end());
}

/**
 * @brief SET_CONFIG_VALUE changes the live config without persisting it.
 */
void test_set_config_value_does_not_persist()
{
    TestFixture f;

    CommandResult result = dispatch_cmd(
        f.cmd_handler, CommandCode::SET_CONFIG_VALUE,
        reinterpret_cast<const uint8_t*>("wifi.ssid=Home"),
        strlen("wifi.ssid=Home"));

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SUCCESS),
        static_cast<uint8_t>(result));
    TEST_ASSERT_EQUAL_INT(0, f.nvs_hal.write_call_count);
    TEST_ASSERT_TRUE(f.sd_hal.files.empty());

    char value[CONFIG_FIELD_MAX_LENGTH];
    TEST_ASSERT_TRUE(f.config.get_config_value("wifi.ssid", value,
                                               sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("Home", value);
}

/**
 * @brief SET_CONFIG with destination sd writes the config file.
 */
void test_set_config_sd_persists_live_config()
{
    TestFixture f;
    f.config.set_config_value("wifi.ssid", "Home");

    CommandResult result = dispatch_cmd(
        f.cmd_handler, CommandCode::SET_CONFIG,
        reinterpret_cast<const uint8_t*>("sd"), 2);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SUCCESS),
        static_cast<uint8_t>(result));
    auto it = f.sd_hal.files.find(ConfigService::DEFAULT_CONFIG_PATH);
    TEST_ASSERT_TRUE(it != f.sd_hal.files.end());
    std::string ini(reinterpret_cast<const char*>(it->second.data()),
                    it->second.size());
    TEST_ASSERT_NOT_NULL(strstr(ini.c_str(), "ssid=Home"));
}

/**
 * @brief SET_CONFIG to SD while degraded returns SD_UNAVAILABLE.
 */
void test_set_config_sd_unavailable_when_degraded()
{
    TestFixture f(nullptr, hal::SdInitResult::NO_SD_CARD);

    CommandResult result = dispatch_cmd(
        f.cmd_handler, CommandCode::SET_CONFIG,
        reinterpret_cast<const uint8_t*>("sd"), 2);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SD_UNAVAILABLE),
        static_cast<uint8_t>(result));
}

/**
 * @brief A failed SD config write leaves the RAM config intact.
 */
void test_set_config_sd_write_failure_leaves_ram_config_intact()
{
    TestFixture f;
    f.config.set_config_value("wifi.ssid", "Home");
    f.sd_hal.write_result = false;

    CommandResult result = dispatch_cmd(
        f.cmd_handler, CommandCode::SET_CONFIG,
        reinterpret_cast<const uint8_t*>("sd"), 2);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::INTERNAL_ERROR),
        static_cast<uint8_t>(result));

    // The live RAM config must be untouched by the failed persistence.
    char value[CONFIG_FIELD_MAX_LENGTH];
    TEST_ASSERT_TRUE(f.config.get_config_value("wifi.ssid", value,
                                               sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("Home", value);
}

/**
 * @brief GET_CONFIG returns the full config as serialized INI text.
 */
void test_get_config_returns_serialized_ini()
{
    TestFixture f;
    f.config.set_config_value("wifi.ssid", "Home");

    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;
    CommandResult result = dispatch_cmd(
        f.cmd_handler, CommandCode::GET_CONFIG, nullptr, 0,
        response, &response_len);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SUCCESS),
        static_cast<uint8_t>(result));
    const char* ini = reinterpret_cast<const char*>(response);
    TEST_ASSERT_NOT_NULL(strstr(ini, "[wifi]"));
    TEST_ASSERT_NOT_NULL(strstr(ini, "ssid=Home"));
    TEST_ASSERT_NOT_NULL(strstr(ini, "[ap]"));
    TEST_ASSERT_NOT_NULL(strstr(ini, "[video]"));

    // Round-trip: re-parsing the response yields the same values.
    ConfigData parsed = IniParser::parse(ini, response_len);
    TEST_ASSERT_EQUAL_STRING("Home", parsed.wifi.ssid);
    TEST_ASSERT_EQUAL_STRING("MicroCam", parsed.ap.ssid);
}

/**
 * @brief GET_CONFIG returns full configs up to the storage buffer size.
 *
 * Regression: configs that serialize between the old 512-byte response
 * limit and the 1024-byte storage limit failed with INTERNAL_ERROR even
 * though CONFIG_SAVE accepted them.
 */
void test_get_config_returns_full_band_config()
{
    TestFixture f;

    // Fill every string field to its maximum valid length so the
    // serialized config lands in the 512–1023 byte band.
    char long_value[CONFIG_FIELD_MAX_LENGTH];
    memset(long_value, 'a', sizeof(long_value) - 1);
    long_value[sizeof(long_value) - 1] = '\0';
    f.config.set_config_value("wifi.ssid", long_value);
    f.config.set_config_value("wifi.password", long_value);
    f.config.set_config_value("wifi.static_ip", long_value);
    f.config.set_config_value("wifi.gateway", long_value);
    f.config.set_config_value("wifi.subnet_mask", long_value);
    f.config.set_config_value("ap.password", long_value);
    f.config.set_config_value("ap.ap_ip", long_value);
    f.config.set_config_value("ap.ap_gateway", long_value);
    f.config.set_config_value("ap.ap_subnet_mask", long_value);
    f.config.set_config_value("web.password", long_value);

    // Sanity: the config exceeds the old limit but fits the storage
    // buffer (i.e. CONFIG_SAVE would accept it).
    char expected[ConfigService::CONFIG_BUFFER_SIZE];
    size_t serialized_len = f.config.serialize(expected, sizeof(expected));
    TEST_ASSERT_GREATER_THAN(512, serialized_len);
    TEST_ASSERT_LESS_THAN(ConfigService::CONFIG_BUFFER_SIZE, serialized_len);

    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;
    CommandResult result = dispatch_cmd(
        f.cmd_handler, CommandCode::GET_CONFIG, nullptr, 0,
        response, &response_len);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SUCCESS),
        static_cast<uint8_t>(result));
    TEST_ASSERT_EQUAL_UINT32(serialized_len, response_len);
    TEST_ASSERT_EQUAL_MEMORY(expected, response, serialized_len);
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
// Stream commands
// ============================================================

/**
 * @brief START_STREAM transitions VideoController to STREAMING.
 */
void test_start_stream_delegates()
{
    TestFixture f;

    TEST_ASSERT_FALSE(f.video.is_recording());
    TEST_ASSERT_FALSE(f.video.is_streaming());

    CommandResult result = dispatch_cmd(
        f.cmd_handler, CommandCode::START_STREAM);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SUCCESS),
        static_cast<uint8_t>(result));
    TEST_ASSERT_TRUE(f.video.is_streaming());
    TEST_ASSERT_FALSE(f.video.is_recording());
}

/**
 * @brief STOP_STREAM returns VideoController to IDLE.
 */
void test_stop_stream_delegates()
{
    TestFixture f;
    f.setup_fake_frame();

    dispatch_cmd(f.cmd_handler, CommandCode::START_STREAM);
    TEST_ASSERT_TRUE(f.video.is_streaming());
    TEST_ASSERT_FALSE(f.video.is_recording());

    CommandResult result = dispatch_cmd(
        f.cmd_handler, CommandCode::STOP_STREAM);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SUCCESS),
        static_cast<uint8_t>(result));
    TEST_ASSERT_FALSE(f.video.is_recording());
    TEST_ASSERT_FALSE(f.video.is_streaming());
}

/**
 * @brief STOP_STREAM closes and frees all active stream slots.
 *
 * A viewer connected during STREAMING must have its writer closed and
 * its slot reclaimed when streaming stops, so the slot is not leaked
 * until the client disconnects.
 */
void test_stop_stream_closes_active_streams()
{
    TestFixture f;
    f.setup_fake_frame();

    // Connect two viewers while streaming.
    dispatch_cmd(f.cmd_handler, CommandCode::START_STREAM);
    auto c1 = std::make_shared<StreamFrameChannel>();
    auto c2 = std::make_shared<StreamFrameChannel>();
    StreamFrameChannel* raw1 = c1.get();
    StreamFrameChannel* raw2 = c2.get();
    f.stream_service.start_stream(c1);
    f.stream_service.start_stream(c2);
    TEST_ASSERT_EQUAL_UINT8(2, f.stream_service.active_slot_count());

    CommandResult result = dispatch_cmd(
        f.cmd_handler, CommandCode::STOP_STREAM);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SUCCESS),
        static_cast<uint8_t>(result));
    TEST_ASSERT_FALSE(f.video.is_recording());
    TEST_ASSERT_FALSE(f.video.is_streaming());
    // Both streams ended and both slots freed.
    TEST_ASSERT_EQUAL_UINT8(0, f.stream_service.active_slot_count());
    TEST_ASSERT_TRUE(raw1->is_closed());
    TEST_ASSERT_TRUE(raw2->is_closed());
}

/**
 * @brief STOP_STREAM closes viewers even while recording continues.
 *
 * Viewers exist only while streaming mode is active: turning streaming
 * off closes all connected streams within one frame period via the
 * capture-task dispatch boundary (no scheduler stop/restart), while the
 * recording subscription persists and keeps writing clips to SD.
 */
void test_stop_stream_closes_viewers_while_recording()
{
    TestFixture f;
    f.setup_fake_frame();

    // Streaming starts, a viewer connects, then recording begins on the
    // shared capture loop.
    dispatch_cmd(f.cmd_handler, CommandCode::START_STREAM);
    auto channel = std::make_shared<StreamFrameChannel>();
    StreamFrameChannel* raw = channel.get();
    f.stream_service.start_stream(channel);
    TEST_ASSERT_EQUAL_UINT8(1, f.stream_service.active_slot_count());
    dispatch_cmd(f.cmd_handler, CommandCode::START_RECORDING);
    TEST_ASSERT_TRUE(f.video.is_recording());

    CommandResult result = dispatch_cmd(
        f.cmd_handler, CommandCode::STOP_STREAM);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SUCCESS),
        static_cast<uint8_t>(result));
    TEST_ASSERT_FALSE(f.video.is_streaming());
    // The close is deferred to the next dispatch boundary; the capture
    // loop keeps running for the surviving recording.
    TEST_ASSERT_TRUE(f.video.is_capture_active());
    TEST_ASSERT_TRUE(f.capture_scheduler.is_started());
    TEST_ASSERT_EQUAL_INT(0, f.capture_scheduler.stop_call_count);

    // Next frame dispatch executes the close...
    f.capture_one_frame();
    TEST_ASSERT_EQUAL_UINT8(0, f.stream_service.active_slot_count());
    TEST_ASSERT_TRUE(raw->is_closed());
    // ...while recording persists unchanged.
    TEST_ASSERT_TRUE(f.video.is_recording());
}

/**
 * @brief LIST_STREAMS returns a JSON active_streams count.
 */
void test_list_streams_returns_count()
{
    TestFixture f;

    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;

    CommandResult result = f.cmd_handler.dispatch(
        CommandCode::LIST_STREAMS, nullptr, 0, response, response_len);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SUCCESS),
        static_cast<uint8_t>(result));
    TEST_ASSERT_NOT_NULL(strstr(reinterpret_cast<char*>(response),
                                "\"active_streams\""));
}

/**
 * @brief The /stream route is registered at init().
 */
void test_stream_route_registered()
{
    TestFixture f;

    bool found = false;
    for (auto& route : f.web_hal.registered_routes)
    {
        if (route.uri == "/stream" && route.method == hal::HttpMethod::GET)
        {
            found = true;
        }
    }
    TEST_ASSERT_TRUE(found);
}

/**
 * @brief /stream rejects viewers unless streaming mode is explicit.
 *
 * Viewers connect only while streaming mode is active, so teardown
 * always closes them.
 */
void test_stream_route_rejects_without_streaming_mode()
{
    TestFixture f;
    f.setup_fake_frame();

    // Recording-only: rejected with 409.
    dispatch_cmd(f.cmd_handler, CommandCode::START_RECORDING);
    TEST_ASSERT_TRUE(f.video.is_recording());
    TEST_ASSERT_TRUE(f.web_hal.simulate_request("/stream", hal::HttpMethod::GET));
    TEST_ASSERT_EQUAL_INT(1, f.web_hal.mock_request.responses.size());
    TEST_ASSERT_EQUAL_INT(409, f.web_hal.mock_request.responses[0].code);

    dispatch_cmd(f.cmd_handler, CommandCode::STOP_RECORDING);

    // Explicit streaming mode: a live streaming response is attached to
    // the request (no error response — the stream itself is the reply).
    dispatch_cmd(f.cmd_handler, CommandCode::START_STREAM);
    f.web_hal.mock_request.responses.clear();
    TEST_ASSERT_TRUE(f.web_hal.simulate_request("/stream", hal::HttpMethod::GET));
    TEST_ASSERT_EQUAL_INT(0, f.web_hal.mock_request.responses.size());
    TEST_ASSERT_EQUAL_INT(1, f.web_hal.mock_request.stream_responses.size());
    TEST_ASSERT_EQUAL_UINT8(1, f.stream_service.active_slot_count());

    // The viewer connects through the transport response: the channel
    // is primed with the placeholder JPEG part, so the first bytes on
    // the wire are a well-formed multipart part (never a bare opening
    // boundary, which strict multipart parsers reject).
    std::shared_ptr<StreamFrameChannel> viewer =
        f.web_hal.mock_request.stream_responses[0].channel;
    TEST_ASSERT_TRUE(viewer->has_pending_part());
    uint8_t buf[64];
    size_t n = viewer->pull(buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_TRUE(strstr(
        reinterpret_cast<const char*>(buf), "--CAM_BOUNDARY") != nullptr);

    // When the transport ends the response (client disconnect), the end
    // callback frees the viewer slot.
    f.web_hal.mock_request.end_stream_response(0);
    TEST_ASSERT_EQUAL_UINT8(0, f.stream_service.active_slot_count());
}

// ============================================================
// /stream authentication
// ============================================================

/**
 * @brief /stream rejects requests without or with wrong credentials.
 */
void test_stream_route_rejects_bad_credentials()
{
    TestFixture f("secret");

    // No auth header at all.
    TEST_ASSERT_TRUE(f.web_hal.simulate_request("/stream", hal::HttpMethod::GET));
    TEST_ASSERT_EQUAL_INT(1, f.web_hal.mock_request.responses.size());
    TEST_ASSERT_EQUAL_INT(401, f.web_hal.mock_request.responses[0].code);

    // Wrong password.
    f.web_hal.mock_request.set_auth("user", "wrong");
    f.web_hal.simulate_request("/stream", hal::HttpMethod::GET);
    TEST_ASSERT_EQUAL_INT(1, f.web_hal.mock_request.responses.size());
    TEST_ASSERT_EQUAL_INT(401, f.web_hal.mock_request.responses[0].code);
}

/**
 * @brief /stream auth checks the register-time password snapshot.
 *
 * Regression pin for the live-config read: a runtime SET_CONFIG_VALUE
 * on web.password must not change what /stream authenticates against —
 * it keeps using the snapshot taken at registration, so /stream and /
 * stay consistent after a runtime password change.
 */
void test_stream_route_uses_register_time_password_snapshot()
{
    TestFixture f("secret");

    // Correct register-time credentials: auth passes (409 — streaming
    // not started — not a 401).
    f.web_hal.mock_request.set_auth("user", "secret");
    TEST_ASSERT_TRUE(f.web_hal.simulate_request("/stream", hal::HttpMethod::GET));
    TEST_ASSERT_EQUAL_INT(1, f.web_hal.mock_request.responses.size());
    TEST_ASSERT_EQUAL_INT(409, f.web_hal.mock_request.responses[0].code);

    // Runtime config mutation (mirrors serial SET web.password=...).
    f.config.set_config_value("web.password", "rotated");

    // The register-time password still authenticates...
    f.web_hal.mock_request.set_auth("user", "secret");
    f.web_hal.simulate_request("/stream", hal::HttpMethod::GET);
    TEST_ASSERT_EQUAL_INT(1, f.web_hal.mock_request.responses.size());
    TEST_ASSERT_EQUAL_INT(409, f.web_hal.mock_request.responses[0].code);

    // ...while the rotated one does not.
    f.web_hal.mock_request.set_auth("user", "rotated");
    f.web_hal.simulate_request("/stream", hal::HttpMethod::GET);
    TEST_ASSERT_EQUAL_INT(1, f.web_hal.mock_request.responses.size());
    TEST_ASSERT_EQUAL_INT(401, f.web_hal.mock_request.responses[0].code);
}

// ============================================================
// Streaming-only degraded mode (SD unavailable)
// ============================================================

/**
 * @brief START_RECORDING while degraded returns SD_UNAVAILABLE.
 *
 * No recording state may change and no capture request may be posted.
 */
void test_degraded_start_recording_returns_sd_unavailable()
{
    TestFixture f(nullptr, hal::SdInitResult::NO_SD_CARD);
    f.setup_fake_frame();

    CommandResult result = dispatch_cmd(
        f.cmd_handler, CommandCode::START_RECORDING);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SD_UNAVAILABLE),
        static_cast<uint8_t>(result));
    TEST_ASSERT_FALSE(f.video.is_recording());
    TEST_ASSERT_FALSE(f.capture_scheduler.is_started());
}

/**
 * @brief TAKE_PHOTO while degraded returns SD_UNAVAILABLE.
 *
 * The gate precedes camera capture: no camera frame may be wasted.
 */
void test_degraded_take_photo_returns_sd_unavailable()
{
    TestFixture f(nullptr, hal::SdInitResult::NO_SD_CARD);
    f.setup_fake_frame();

    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;
    CommandResult result = dispatch_cmd(
        f.cmd_handler, CommandCode::TAKE_PHOTO, nullptr, 0,
        response, &response_len);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SD_UNAVAILABLE),
        static_cast<uint8_t>(result));
    TEST_ASSERT_EQUAL_INT(0, f.camera_hal.capture_call_count);
    TEST_ASSERT_EQUAL_UINT32(0, response_len);
}

/**
 * @brief SD_UNAVAILABLE takes precedence over INVALID_STATE.
 *
 * A photo request during streaming with no SD reports the SD reason,
 * not the capture-loop state rejection.
 */
void test_degraded_take_photo_precedence_over_invalid_state()
{
    TestFixture f(nullptr, hal::SdInitResult::NO_SD_CARD);
    f.setup_fake_frame();

    dispatch_cmd(f.cmd_handler, CommandCode::START_STREAM);
    TEST_ASSERT_TRUE(f.video.is_streaming());

    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;
    CommandResult result = dispatch_cmd(
        f.cmd_handler, CommandCode::TAKE_PHOTO, nullptr, 0,
        response, &response_len);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SD_UNAVAILABLE),
        static_cast<uint8_t>(result));
}

/**
 * @brief STOP_RECORDING stays SUCCESS while degraded (idempotent).
 */
void test_degraded_stop_recording_returns_success()
{
    TestFixture f(nullptr, hal::SdInitResult::NO_SD_CARD);

    CommandResult result = dispatch_cmd(
        f.cmd_handler, CommandCode::STOP_RECORDING);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SUCCESS),
        static_cast<uint8_t>(result));
}

/**
 * @brief GET_STATUS reports the latched SD availability.
 */
void test_degraded_get_status_reports_sd_false()
{
    TestFixture f(nullptr, hal::SdInitResult::NO_SD_CARD);

    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;
    dispatch_cmd(f.cmd_handler, CommandCode::GET_STATUS, nullptr, 0,
                 response, &response_len);

    TEST_ASSERT_NOT_NULL(strstr(
        reinterpret_cast<const char*>(response), "\"sd\":false"));
}

/**
 * @brief A failed SD card initialization also latches degraded mode.
 */
void test_degraded_hw_init_failure_enters_streaming_only()
{
    TestFixture f(nullptr, hal::SdInitResult::HW_INIT_FAILURE);

    CommandResult result = dispatch_cmd(
        f.cmd_handler, CommandCode::START_RECORDING);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SD_UNAVAILABLE),
        static_cast<uint8_t>(result));
    TEST_ASSERT_FALSE(f.video.is_recording());

    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;
    dispatch_cmd(f.cmd_handler, CommandCode::GET_STATUS, nullptr, 0,
                 response, &response_len);
    TEST_ASSERT_NOT_NULL(strstr(
        reinterpret_cast<const char*>(response), "\"sd\":false"));
}

/**
 * @brief Healthy fixture reports SD available in GET_STATUS.
 */
void test_get_status_reports_sd_true_when_available()
{
    TestFixture f;

    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;
    dispatch_cmd(f.cmd_handler, CommandCode::GET_STATUS, nullptr, 0,
                 response, &response_len);

    TEST_ASSERT_NOT_NULL(strstr(
        reinterpret_cast<const char*>(response), "\"sd\":true"));
}

/**
 * @brief GET_STATUS counts today's photos and videos in the date dir.
 */
void test_get_status_reports_media_counts()
{
    TestFixture f;

    // Seed a media set in the fixture's simulated today date directory.
    // The time service starts at a fixed mock epoch; use the same
    // formatter as the status handler to compute today's directory.
    char date[9];
    TEST_ASSERT_TRUE(TimeFormatter::format_date(
        f.time.unix_now(), date, sizeof(date)));

    std::string dir = std::string("/") + date;
    f.sd_hal.directories.insert(dir);
    f.sd_hal.seed_file((dir + "/x.avi").c_str(), {0x01});
    f.sd_hal.seed_file((dir + "/y.avi").c_str(), {0x01});
    f.sd_hal.seed_file((dir + "/z.jpg").c_str(), {0x01});
    f.sd_hal.seed_file((dir + "/n.txt").c_str(), {0x01});

    f.sd_hal.mock_free_bytes = 123456;

    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;
    dispatch_cmd(f.cmd_handler, CommandCode::GET_STATUS, nullptr, 0,
                 response, &response_len);

    const char* json = reinterpret_cast<const char*>(response);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"sd_free_bytes\":123456"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"photos\":1"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"videos\":2"));
}

/**
 * @brief GET_STATUS reports free space above the 32-bit range
 *        untruncated (a mostly-empty large card is not ~0).
 */
void test_get_status_free_bytes_beyond_32_bits()
{
    TestFixture f;
    f.sd_hal.mock_free_bytes = 5368709120ULL; // 5 GB free.

    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;
    dispatch_cmd(f.cmd_handler, CommandCode::GET_STATUS, nullptr, 0,
                 response, &response_len);

    TEST_ASSERT_NOT_NULL(strstr(
        reinterpret_cast<const char*>(response),
        "\"sd_free_bytes\":5368709120"));
}

/**
 * @brief GET_STATUS caches SD storage stats with a bounded TTL: status
 *        polls within the TTL reuse the cached values without SD
 *        transactions; the cache refreshes after the TTL elapses.
 */
void test_get_status_caches_sd_stats_within_ttl()
{
    TestFixture f;
    set_mock_millis(1000);
    f.sd_hal.mock_free_bytes = 111;

    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;
    dispatch_cmd(f.cmd_handler, CommandCode::GET_STATUS, nullptr, 0,
                 response, &response_len);
    TEST_ASSERT_NOT_NULL(strstr(
        reinterpret_cast<const char*>(response), "\"sd_free_bytes\":111"));
    int scans_after_first = f.sd_hal.list_dir_call_count;
    TEST_ASSERT_TRUE(scans_after_first >= 1);

    // A poll within the TTL serves cached values: no new scan, and a
    // changed free-space value is not yet visible.
    set_mock_millis(30000);
    f.sd_hal.mock_free_bytes = 999;
    memset(response, 0, sizeof(response));
    dispatch_cmd(f.cmd_handler, CommandCode::GET_STATUS, nullptr, 0,
                 response, &response_len);
    TEST_ASSERT_NOT_NULL(strstr(
        reinterpret_cast<const char*>(response), "\"sd_free_bytes\":111"));
    TEST_ASSERT_EQUAL_INT(scans_after_first, f.sd_hal.list_dir_call_count);

    // After the TTL elapses the next poll refreshes from the card.
    set_mock_millis(61001);
    memset(response, 0, sizeof(response));
    dispatch_cmd(f.cmd_handler, CommandCode::GET_STATUS, nullptr, 0,
                 response, &response_len);
    TEST_ASSERT_NOT_NULL(strstr(
        reinterpret_cast<const char*>(response), "\"sd_free_bytes\":999"));
    TEST_ASSERT_EQUAL_INT(scans_after_first + 1,
                          f.sd_hal.list_dir_call_count);
}

// ============================================================
// Version, reboot, and uptime
// ============================================================

/**
 * @brief GET_VERSION returns the firmware version as a JSON object
 *        built from the compile-time version strings.
 */
void test_get_version_returns_version_json()
{
    TestFixture f;

    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;
    CommandResult result = dispatch_cmd(
        f.cmd_handler, CommandCode::GET_VERSION, nullptr, 0,
        response, &response_len);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SUCCESS),
        static_cast<uint8_t>(result));

    char expected[64];
    snprintf(expected, sizeof(expected), "{\"version\":\"%s-%s\"}",
             version::base_version_string, version::git_hash);
    TEST_ASSERT_EQUAL_STRING(expected,
                             reinterpret_cast<const char*>(response));
}

/**
 * @brief REBOOT schedules exactly one deferred restart on the restart
 *        HAL and returns success with no response payload; the mock
 *        hal means no real restart occurs.
 */
void test_reboot_schedules_deferred_restart()
{
    TestFixture f;

    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;
    CommandResult result = dispatch_cmd(
        f.cmd_handler, CommandCode::REBOOT, nullptr, 0,
        response, &response_len);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SUCCESS),
        static_cast<uint8_t>(result));
    TEST_ASSERT_EQUAL_UINT32(0, response_len);
    TEST_ASSERT_EQUAL_INT(1, f.restart_hal.schedule_count);
    TEST_ASSERT_EQUAL_UINT32(SystemOrchestrator::REBOOT_DELAY_MS,
                             f.restart_hal.last_delay_ms);
}

/**
 * @brief A second REBOOT before the restart fires schedules a second
 *        restart (idempotency is owned by the restart HAL); the
 *        orchestrator always reports success.
 */
void test_reboot_repeat_dispatch_schedules_again()
{
    TestFixture f;

    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;
    dispatch_cmd(f.cmd_handler, CommandCode::REBOOT, nullptr, 0,
                 response, &response_len);
    dispatch_cmd(f.cmd_handler, CommandCode::REBOOT, nullptr, 0,
                 response, &response_len);

    TEST_ASSERT_EQUAL_INT(2, f.restart_hal.schedule_count);
}

/**
 * @brief REBOOT reports INTERNAL_ERROR when the restart HAL fails to
 *        schedule the restart, so a caller never sees a success
 *        response for a reboot that will not happen.
 */
void test_reboot_schedule_failure_returns_internal_error()
{
    TestFixture f;
    f.restart_hal.last_result = false;

    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;
    CommandResult result = dispatch_cmd(
        f.cmd_handler, CommandCode::REBOOT, nullptr, 0,
        response, &response_len);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::INTERNAL_ERROR),
        static_cast<uint8_t>(result));
    TEST_ASSERT_EQUAL_INT(1, f.restart_hal.schedule_count);
}

/**
 * @brief GET_STATUS reports the device uptime in milliseconds from
 *        the millis clock, so a test can prove a reboot happened.
 */
void test_get_status_includes_uptime_ms()
{
    TestFixture f;
    set_mock_millis(123456);

    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;
    dispatch_cmd(f.cmd_handler, CommandCode::GET_STATUS, nullptr, 0,
                 response, &response_len);

    TEST_ASSERT_NOT_NULL(strstr(
        reinterpret_cast<const char*>(response), "\"uptime_ms\":123456"));
}

/**
 * @brief GET_STATUS reports the build-identity version string from
 *        version.h (rendered by the web UI status panel).
 */
void test_get_status_includes_version()
{
    TestFixture f;

    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;
    dispatch_cmd(f.cmd_handler, CommandCode::GET_STATUS, nullptr, 0,
                 response, &response_len);

    char expected[64];
    snprintf(expected, sizeof(expected), "\"version\":\"%s-%s\"",
             version::base_version_string, version::git_hash);
    TEST_ASSERT_NOT_NULL(strstr(reinterpret_cast<const char*>(response),
                                expected));
}

/**
 * @brief GET_STATUS reports the core temperature read through the
 *        hardware peripheral manager (rendered by the web UI).
 */
void test_get_status_reports_core_temperature()
{
    TestFixture f;
    f.hw_periph.temperature_c = 54.5f;

    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;
    dispatch_cmd(f.cmd_handler, CommandCode::GET_STATUS, nullptr, 0,
                 response, &response_len);

    TEST_ASSERT_NOT_NULL(strstr(
        reinterpret_cast<const char*>(response), "\"temperature_c\":54.50"));
}

/**
 * @brief GET_STATUS reports the applied camera frame dimensions
 *        (rendered in the web UI stream preview).
 */
void test_get_status_reports_frame_dimensions()
{
    TestFixture f;

    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;
    dispatch_cmd(f.cmd_handler, CommandCode::GET_STATUS, nullptr, 0,
                 response, &response_len);

    char expected[64];
    snprintf(expected, sizeof(expected), "\"width\":%u,\"height\":%u",
             static_cast<unsigned int>(f.camera_ctrl.get_params().width),
             static_cast<unsigned int>(f.camera_ctrl.get_params().height));
    TEST_ASSERT_NOT_NULL(strstr(reinterpret_cast<const char*>(response),
                                expected));
}

/**
 * @brief GET_STATUS reports the cumulative delivered-frame counter the
 *        web UI samples to render the preview framerate.
 */
void test_get_status_includes_stream_frames()
{
    TestFixture f;

    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;
    dispatch_cmd(f.cmd_handler, CommandCode::GET_STATUS, nullptr, 0,
                 response, &response_len);
    TEST_ASSERT_NOT_NULL(strstr(
        reinterpret_cast<const char*>(response), "\"stream_frames\":0"));

    const uint8_t frame[4] = {0xFF, 0xD8, 0xFF, 0xD9};
    f.stream_service.deliver_frame(frame, sizeof(frame));

    response_len = 0;
    dispatch_cmd(f.cmd_handler, CommandCode::GET_STATUS, nullptr, 0,
                 response, &response_len);
    TEST_ASSERT_NOT_NULL(strstr(
        reinterpret_cast<const char*>(response), "\"stream_frames\":1"));
}

/**
 * @brief GET_STATUS reports the local IP from the WiFi service
 *        (rendered by the web UI status panel).
 */
void test_get_status_includes_local_ip()
{
    TestFixture f;

    uint8_t response[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;
    dispatch_cmd(f.cmd_handler, CommandCode::GET_STATUS, nullptr, 0,
                 response, &response_len);

    char expected[64];
    snprintf(expected, sizeof(expected), "\"local_ip\":\"%s\"",
             f.wifi_hal.ip_buffer);
    TEST_ASSERT_NOT_NULL(strstr(reinterpret_cast<const char*>(response),
                                expected));
}

// ============================================================
// POST /update (bulk config)
// ============================================================

/**
 * @brief /update without a configured web password is refused with 401.
 */
void test_update_no_password_configured_refused()
{
    TestFixture f;

    TEST_ASSERT_TRUE(f.web_hal.simulate_request("/update", hal::HttpMethod::POST));
    TEST_ASSERT_EQUAL_INT(1, f.web_hal.mock_request.responses.size());
    TEST_ASSERT_EQUAL_INT(401, f.web_hal.mock_request.responses[0].code);
}

/**
 * @brief /update requires valid HTTP Basic credentials.
 */
void test_update_requires_credentials()
{
    TestFixture f("secret");

    TEST_ASSERT_TRUE(f.web_hal.simulate_request("/update", hal::HttpMethod::POST));
    TEST_ASSERT_EQUAL_INT(401, f.web_hal.mock_request.responses[0].code);

    f.web_hal.mock_request.set_auth("user", "wrong");
    TEST_ASSERT_TRUE(f.web_hal.simulate_request("/update", hal::HttpMethod::POST));
    TEST_ASSERT_EQUAL_INT(401, f.web_hal.mock_request.responses[0].code);
}

/**
 * @brief /update overlays the posted INI text and persists to NVS.
 */
void test_update_applies_config_overlay_and_saves_nvs()
{
    TestFixture f("secret");

    // A recognizable setting: web.max_streams clamps 1..4.
    f.web_hal.mock_request.set_auth("user", "secret");
    f.web_hal.mock_request.set_form_value(
        "config", "[video]\nfps=15\n");
    TEST_ASSERT_TRUE(f.web_hal.simulate_request("/update", hal::HttpMethod::POST));

    TEST_ASSERT_EQUAL_INT(1, f.web_hal.mock_request.responses.size());
    TEST_ASSERT_EQUAL_INT(200, f.web_hal.mock_request.responses[0].code);
    TEST_ASSERT_TRUE(
        f.web_hal.mock_request.responses[0].body.find("\"result\":\"ok\"") !=
        std::string::npos);
    TEST_ASSERT_EQUAL_UINT32(15, f.config.config().video.fps);
}

/**
 * @brief /update with an empty config body is a 400.
 */
void test_update_missing_body_400()
{
    TestFixture f("secret");
    f.web_hal.mock_request.set_auth("user", "secret");

    TEST_ASSERT_TRUE(f.web_hal.simulate_request("/update", hal::HttpMethod::POST));
    TEST_ASSERT_EQUAL_INT(400, f.web_hal.mock_request.responses[0].code);
}

/**
 * @brief /update rejects a form value that was truncated in transit
 *        (413) instead of applying the cut-down config text.
 */
void test_update_truncated_config_rejected_413()
{
    TestFixture f("secret");
    f.web_hal.mock_request.set_auth("user", "secret");

    uint8_t fps_before = f.config.config().video.fps;
    f.web_hal.mock_request.set_form_value("config", "[video]\nfps=15\n");
    f.web_hal.mock_request.form_truncated = true;

    TEST_ASSERT_TRUE(f.web_hal.simulate_request("/update", hal::HttpMethod::POST));
    TEST_ASSERT_EQUAL_INT(413, f.web_hal.mock_request.responses[0].code);
    TEST_ASSERT_TRUE(
        f.web_hal.mock_request.responses[0].body.find("too large") !=
        std::string::npos);
    TEST_ASSERT_EQUAL_UINT32(fps_before, f.config.config().video.fps);
}

/**
 * @brief /update dest=sd requires the SD card.
 */
void test_update_dest_sd_unavailable_503()
{
    TestFixture f("secret", hal::SdInitResult::NO_SD_CARD);
    f.web_hal.mock_request.set_auth("user", "secret");

    f.web_hal.mock_request.set_arg("dest", "sd");
    f.web_hal.mock_request.set_form_value("config", "[video]\nfps=12\n");
    TEST_ASSERT_TRUE(f.web_hal.simulate_request("/update", hal::HttpMethod::POST));
    TEST_ASSERT_EQUAL_INT(503, f.web_hal.mock_request.responses[0].code);
}

/**
 * @brief Streaming works end to end while degraded.
 *
 * In streaming-only mode the full viewer path stays operational:
 * start streaming, capture a frame that reaches a connected viewer,
 * then stop streaming and verify the viewer is closed.
 */
void test_degraded_streaming_end_to_end()
{
    TestFixture f(nullptr, hal::SdInitResult::NO_SD_CARD);
    f.setup_fake_frame();

    CommandResult start = dispatch_cmd(
        f.cmd_handler, CommandCode::START_STREAM);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SUCCESS),
        static_cast<uint8_t>(start));
    TEST_ASSERT_TRUE(f.video.is_streaming());

    // Connect a viewer and deliver a frame to it.
    auto channel = std::make_shared<StreamFrameChannel>();
    StreamFrameChannel* raw = channel.get();
    f.stream_service.start_stream(channel);
    TEST_ASSERT_EQUAL_UINT8(1, f.stream_service.active_slot_count());

    f.capture_one_frame();
    TEST_ASSERT_FALSE(raw->is_closed());

    // Stopping streaming closes the viewer and frees the slot.
    CommandResult stop = dispatch_cmd(
        f.cmd_handler, CommandCode::STOP_STREAM);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CommandResult::SUCCESS),
        static_cast<uint8_t>(stop));
    TEST_ASSERT_EQUAL_UINT8(0, f.stream_service.active_slot_count());
    TEST_ASSERT_TRUE(raw->is_closed());
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
    RUN_TEST(test_stop_recording_closes_connected_viewers);
    RUN_TEST(test_stop_recording_keeps_streams_when_streaming_active);
    RUN_TEST(test_take_photo_captures_frame);
    RUN_TEST(test_take_photo_succeeds_while_recording);
    RUN_TEST(test_take_photo_succeeds_while_recording_and_streaming);
    RUN_TEST(test_take_photo_when_idle_unchanged);
    RUN_TEST(test_take_photo_camera_failure);
    RUN_TEST(test_take_photo_sd_write_failure);
    RUN_TEST(test_take_photo_uniquifies_same_second_collision);
    RUN_TEST(test_set_time_sets_offset);
    RUN_TEST(test_set_time_invalid_payload);
    RUN_TEST(test_set_config_requires_destination);
    RUN_TEST(test_set_config_unknown_destination);
    RUN_TEST(test_set_config_nvs_persists_live_config);
    RUN_TEST(test_set_config_destination_case_insensitive);
    RUN_TEST(test_set_config_value_does_not_persist);
    RUN_TEST(test_set_config_sd_persists_live_config);
    RUN_TEST(test_set_config_sd_unavailable_when_degraded);
    RUN_TEST(test_set_config_sd_write_failure_leaves_ram_config_intact);
    RUN_TEST(test_get_config_returns_serialized_ini);
    RUN_TEST(test_get_config_returns_full_band_config);
    RUN_TEST(test_tick_does_not_crash);
    RUN_TEST(test_start_stream_delegates);
    RUN_TEST(test_stop_stream_delegates);
    RUN_TEST(test_stop_stream_closes_active_streams);
    RUN_TEST(test_stop_stream_closes_viewers_while_recording);
    RUN_TEST(test_list_streams_returns_count);
    RUN_TEST(test_take_photo_succeeds_during_streaming_only);
    RUN_TEST(test_stream_route_registered);
    RUN_TEST(test_stream_route_rejects_without_streaming_mode);
    RUN_TEST(test_stream_route_rejects_bad_credentials);
    RUN_TEST(test_stream_route_uses_register_time_password_snapshot);
    RUN_TEST(test_get_status_reports_streaming_field);
    RUN_TEST(test_degraded_start_recording_returns_sd_unavailable);
    RUN_TEST(test_degraded_take_photo_returns_sd_unavailable);
    RUN_TEST(test_degraded_take_photo_precedence_over_invalid_state);
    RUN_TEST(test_degraded_stop_recording_returns_success);
    RUN_TEST(test_degraded_get_status_reports_sd_false);
    RUN_TEST(test_degraded_hw_init_failure_enters_streaming_only);
    RUN_TEST(test_get_status_reports_sd_true_when_available);
    RUN_TEST(test_get_status_reports_media_counts);
    RUN_TEST(test_get_status_free_bytes_beyond_32_bits);
    RUN_TEST(test_get_status_caches_sd_stats_within_ttl);

    // Status panel display fields (version, temperature, preview)
    RUN_TEST(test_get_status_includes_version);
    RUN_TEST(test_get_status_reports_core_temperature);
    RUN_TEST(test_get_status_reports_frame_dimensions);
    RUN_TEST(test_get_status_includes_stream_frames);
    RUN_TEST(test_get_status_includes_local_ip);

    // Version, reboot, and uptime
    RUN_TEST(test_get_version_returns_version_json);
    RUN_TEST(test_reboot_schedules_deferred_restart);
    RUN_TEST(test_reboot_repeat_dispatch_schedules_again);
    RUN_TEST(test_reboot_schedule_failure_returns_internal_error);
    RUN_TEST(test_get_status_includes_uptime_ms);

    // POST /update
    RUN_TEST(test_update_no_password_configured_refused);
    RUN_TEST(test_update_requires_credentials);
    RUN_TEST(test_update_applies_config_overlay_and_saves_nvs);
    RUN_TEST(test_update_missing_body_400);
    RUN_TEST(test_update_truncated_config_rejected_413);
    RUN_TEST(test_update_dest_sd_unavailable_503);
    RUN_TEST(test_degraded_streaming_end_to_end);

    return UNITY_END();
}
