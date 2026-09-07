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
    SystemOrchestrator orchestrator;

    explicit TestFixture(const char* web_password = nullptr)
        : sd_controller(time, sd_hal)
        , camera_ctrl(camera_hal)
        , logger(log_backend, time)
        , video(camera_ctrl, sd_controller, time, logger, capture_scheduler)
        , config(logger)
        , cmd_handler(logger)
        , stream_service(logger)
        , orchestrator(video, camera_ctrl, cmd_handler, time, config, logger,
                       stream_service, web_hal)
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
    TEST_ASSERT_NULL(strstr(json, "\"state\""));
}

/**
 * @brief TAKE_PHOTO is rejected during streaming-only preview.
 *
 * The gate must reject whenever the capture loop is running for any
 * consumer, not just recording, and return INVALID_STATE (not the
 * INTERNAL_ERROR a controller-level rejection would produce).
 */
void test_take_photo_rejected_during_streaming_only()
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
        static_cast<uint8_t>(CommandResult::INVALID_STATE),
        static_cast<uint8_t>(result));
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

    // The viewer connects through the transport response: the channel is
    // seeded with the multipart opening boundary.
    std::shared_ptr<StreamFrameChannel> viewer =
        f.web_hal.mock_request.stream_responses[0].channel;
    uint8_t buf[64];
    size_t n = viewer->pull(buf, sizeof(buf));
    TEST_ASSERT_TRUE(strstr(
        reinterpret_cast<const char*>(buf), "--CAM_BOUNDARY") != nullptr);
    TEST_ASSERT_GREATER_THAN(0, n);

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
    RUN_TEST(test_take_photo_fails_when_recording);
    RUN_TEST(test_take_photo_sd_write_failure);
    RUN_TEST(test_take_photo_uniquifies_same_second_collision);
    RUN_TEST(test_set_time_sets_offset);
    RUN_TEST(test_set_time_invalid_payload);
    RUN_TEST(test_set_config_returns_invalid_state);
    RUN_TEST(test_tick_does_not_crash);
    RUN_TEST(test_start_stream_delegates);
    RUN_TEST(test_stop_stream_delegates);
    RUN_TEST(test_stop_stream_closes_active_streams);
    RUN_TEST(test_stop_stream_closes_viewers_while_recording);
    RUN_TEST(test_list_streams_returns_count);
    RUN_TEST(test_take_photo_rejected_during_streaming_only);
    RUN_TEST(test_stream_route_registered);
    RUN_TEST(test_stream_route_rejects_without_streaming_mode);
    RUN_TEST(test_stream_route_rejects_bad_credentials);
    RUN_TEST(test_stream_route_uses_register_time_password_snapshot);
    RUN_TEST(test_get_status_reports_streaming_field);

    return UNITY_END();
}
