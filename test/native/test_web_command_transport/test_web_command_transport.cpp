#include <unity.h>
#include "web_command_transport.h"
#include "command_handler.h"
#include "mock_web_server_hal.h"
#include "mock_log_backend.h"
#include "time_service.h"
#include <cstring>
#include <cstdio>

/**
 * @file test_web_command_transport.cpp
 * @brief Native unit tests for WebCommandTransport.
 *
 * Tests cover:
 *  - Route registration (/status, /control)
 *  - Auth checks (no password, missing/wrong/correct auth)
 *  - Status handler dispatches GET_STATUS
 *  - Control handler dispatches commands from "cmd" param
 *  - SET_TIME with "ts" param sends packed timestamp
 *  - Missing "cmd" returns 400
 *  - Unknown/missing command falls back to GET_STATUS
 */

// ============================================================
// Test fixture — uses real CommandHandler with captured dispatch
// ============================================================

/** Max payload we expect to capture (SET_TIME = 4 bytes). */
static constexpr size_t CAPTURE_BUF_SIZE = 32;

/** Tracks the last command dispatched to CommandHandler. */
static CommandCode g_last_code = CommandCode::GET_STATUS;
static uint8_t g_last_payload[CAPTURE_BUF_SIZE]{};
static size_t g_last_payload_len = 0;
static int g_dispatch_count = 0;

/**
 * @brief Handler that captures dispatch args and returns a canned result.
 */
static CommandResult capture_handler(
    const uint8_t* payload, size_t len,
    uint8_t* response, size_t& responseLen)
{
    size_t copy_len = (payload != nullptr && len < CAPTURE_BUF_SIZE)
                      ? len
                      : 0;
    if (copy_len > 0)
    {
        memcpy(g_last_payload, payload, copy_len);
    }
    g_last_payload_len = copy_len;
    g_dispatch_count++;

    // Write a canned JSON response for verification
    const char* json = "{\"result\":\"ok\",\"captured\":true}";
    size_t json_len = strlen(json);
    if (json_len + 1 <= CommandHandler::MAX_RESPONSE_SIZE)
    {
        memcpy(response, json, json_len + 1);
        responseLen = json_len;
    }
    return CommandResult::SUCCESS;
}

static void reset_capture()
{
    g_last_code = CommandCode::GET_STATUS;
    memset(g_last_payload, 0, CAPTURE_BUF_SIZE);
    g_last_payload_len = 0;
    g_dispatch_count = 0;
}

/**
 * @brief Handler that writes no response payload (exercises the fallback path).
 */
static CommandResult no_response_handler(
    const uint8_t* /*payload*/, size_t /*len*/,
    uint8_t* /*response*/, size_t& responseLen)
{
    responseLen = 0;
    return CommandResult::SUCCESS;
}

/**
 * @brief Handler that writes no response and returns an error result.
 */
static CommandResult error_no_response_handler(
    const uint8_t* /*payload*/, size_t /*len*/,
    uint8_t* /*response*/, size_t& responseLen)
{
    responseLen = 0;
    return CommandResult::INVALID_STATE;
}

struct TestFixture
{
    MockWebServerHal hal;
    MockLogBackend log_backend;
    RealTimeService time;
    LoggingService logger;
    CommandHandler cmd_handler;
    WebCommandTransport transport;

    TestFixture()
        : logger(log_backend, time)
        , cmd_handler(logger)
        , transport(hal, cmd_handler)
    {
        // Register the capture handler for all commands we test
        cmd_handler.register_handler(CommandCode::GET_STATUS, capture_handler);
        cmd_handler.register_handler(CommandCode::START_RECORDING, capture_handler);
        cmd_handler.register_handler(CommandCode::STOP_RECORDING, capture_handler);
        cmd_handler.register_handler(CommandCode::TAKE_PHOTO, capture_handler);
        cmd_handler.register_handler(CommandCode::GET_CONFIG, capture_handler);
        cmd_handler.register_handler(CommandCode::SET_CONFIG, capture_handler);
        cmd_handler.register_handler(CommandCode::GET_CONFIG_VALUE, capture_handler);
        cmd_handler.register_handler(CommandCode::SET_CONFIG_VALUE, capture_handler);
        cmd_handler.register_handler(CommandCode::SET_TIME, capture_handler);
        cmd_handler.register_handler(CommandCode::LIST_FILES, capture_handler);
    }
};

// ============================================================
// Route registration tests
// ============================================================

/**
 * @brief register_routes() creates /status and /control routes.
 */
void test_register_routes_creates_status_and_control()
{
    TestFixture f;
    f.transport.register_routes();

    TEST_ASSERT_EQUAL_INT(2, f.hal.registered_routes.size());

    // /status — GET
    TEST_ASSERT_EQUAL_STRING("/status",
                             f.hal.registered_routes[0].uri.c_str());
    TEST_ASSERT_EQUAL(hal::HttpMethod::GET,
                      f.hal.registered_routes[0].method);

    // /control — GET
    TEST_ASSERT_EQUAL_STRING("/control",
                             f.hal.registered_routes[1].uri.c_str());
    TEST_ASSERT_EQUAL(hal::HttpMethod::GET,
                      f.hal.registered_routes[1].method);
}

/**
 * @brief register_routes() handles a null password without crashing.
 */
void test_register_routes_with_null_password()
{
    TestFixture f;
    // Should not crash
    f.transport.register_routes(nullptr);

    TEST_ASSERT_EQUAL_INT(2, f.hal.registered_routes.size());
}

// ============================================================
// Auth tests
// ============================================================

/**
 * @brief /status allows all requests when no password is set.
 */
void test_status_no_password_allows_all()
{
    TestFixture f;
    f.transport.register_routes(""); // empty password

    reset_capture();
    f.hal.mock_request.auth_value[0] = '\0'; // no auth header
    f.hal.simulate_request("/status", hal::HttpMethod::GET);

    // Should proceed to dispatch (no auth required)
    TEST_ASSERT_EQUAL_INT(1, g_dispatch_count);
}

/**
 * @brief /status returns 401 when no auth header is present.
 */
void test_status_no_auth_header_returns_401()
{
    TestFixture f;
    f.transport.register_routes("secret");

    reset_capture();
    f.hal.mock_request.auth_value[0] = '\0'; // no auth header
    f.hal.simulate_request("/status", hal::HttpMethod::GET);

    // Should get 401, no dispatch
    TEST_ASSERT_EQUAL_INT(0, g_dispatch_count);
    TEST_ASSERT_EQUAL_INT(1, f.hal.mock_request.responses.size());
    TEST_ASSERT_EQUAL_INT(401, f.hal.mock_request.responses[0].code);
}

/**
 * @brief /status returns 401 for a wrong password.
 */
void test_status_wrong_auth_returns_401()
{
    TestFixture f;
    f.transport.register_routes("secret");

    reset_capture();
    f.hal.mock_request.set_auth("user", "wrongpass");
    f.hal.simulate_request("/status", hal::HttpMethod::GET);

    TEST_ASSERT_EQUAL_INT(0, g_dispatch_count);
    TEST_ASSERT_EQUAL_INT(1, f.hal.mock_request.responses.size());
    TEST_ASSERT_EQUAL_INT(401, f.hal.mock_request.responses[0].code);
}

/**
 * @brief /status succeeds with correct credentials.
 */
void test_status_correct_auth_succeeds()
{
    TestFixture f;
    f.transport.register_routes("secret");

    reset_capture();
    f.hal.mock_request.set_auth("admin", "secret");
    f.hal.simulate_request("/status", hal::HttpMethod::GET);

    TEST_ASSERT_EQUAL_INT(1, g_dispatch_count);
    TEST_ASSERT_EQUAL_INT(1, f.hal.mock_request.responses.size());
    TEST_ASSERT_EQUAL_INT(200, f.hal.mock_request.responses[0].code);
}

// ============================================================
// Status handler tests
// ============================================================

/**
 * @brief /status dispatches GET_STATUS and forwards the response.
 */
void test_status_dispatches_get_status()
{
    TestFixture f;
    f.transport.register_routes("");

    reset_capture();
    f.hal.simulate_request("/status", hal::HttpMethod::GET);

    TEST_ASSERT_EQUAL_INT(1, g_dispatch_count);
    // The dispatch_and_respond for GET_STATUS should forward the response
    TEST_ASSERT_EQUAL_INT(200, f.hal.mock_request.responses[0].code);
    TEST_ASSERT(f.hal.mock_request.responses[0].body.find("captured") !=
                std::string::npos);
}

// ============================================================
// Control handler — command mapping tests
// ============================================================

/**
 * @brief /control returns 400 when the cmd arg is missing.
 */
void test_control_missing_cmd_returns_400()
{
    TestFixture f;
    f.transport.register_routes("");

    reset_capture();
    // No "cmd" arg set — defaults to ""
    f.hal.simulate_request("/control", hal::HttpMethod::GET);

    // Missing cmd should return 400
    TEST_ASSERT_EQUAL_INT(0, g_dispatch_count);
    TEST_ASSERT_EQUAL_INT(1, f.hal.mock_request.responses.size());
    TEST_ASSERT_EQUAL_INT(400, f.hal.mock_request.responses[0].code);
}

/**
 * @brief /control returns 400 when the cmd arg is empty.
 */
void test_control_empty_cmd_returns_400()
{
    TestFixture f;
    f.transport.register_routes("");

    reset_capture();
    f.hal.mock_request.set_arg("cmd", "");
    f.hal.simulate_request("/control", hal::HttpMethod::GET);

    // Empty cmd is treated as missing — returns 400
    // (map_cmd_string defaults to GET_STATUS for empty, but
    //  handle_control rejects empty before mapping)
    TEST_ASSERT_EQUAL_INT(0, g_dispatch_count);
    TEST_ASSERT_EQUAL_INT(1, f.hal.mock_request.responses.size());
    TEST_ASSERT_EQUAL_INT(400, f.hal.mock_request.responses[0].code);
}

/**
 * @brief /control maps cmd=start to START_RECORDING.
 */
void test_control_start_recording()
{
    TestFixture f;
    f.transport.register_routes("");

    reset_capture();
    f.hal.mock_request.set_arg("cmd", "start");
    f.hal.simulate_request("/control", hal::HttpMethod::GET);

    TEST_ASSERT_EQUAL_INT(1, g_dispatch_count);
    TEST_ASSERT_EQUAL_INT(200, f.hal.mock_request.responses[0].code);

    // Verify response body contains canned handler output
    TEST_ASSERT(f.hal.mock_request.responses[0].body.find("ok") !=
                std::string::npos);
}

/**
 * @brief /control maps cmd=stop to STOP_RECORDING.
 */
void test_control_stop_recording()
{
    TestFixture f;
    f.transport.register_routes("");

    reset_capture();
    f.hal.mock_request.set_arg("cmd", "stop");
    f.hal.simulate_request("/control", hal::HttpMethod::GET);

    TEST_ASSERT_EQUAL_INT(1, g_dispatch_count);
}

/**
 * @brief /control maps cmd=photo to TAKE_PHOTO.
 */
void test_control_take_photo()
{
    TestFixture f;
    f.transport.register_routes("");

    reset_capture();
    f.hal.mock_request.set_arg("cmd", "photo");
    f.hal.simulate_request("/control", hal::HttpMethod::GET);

    TEST_ASSERT_EQUAL_INT(1, g_dispatch_count);
}

/**
 * @brief /control refuses cmd=config while no web password is configured.
 *
 * Config commands carry credentials and the full config body, so the
 * web transport refuses them instead of serving them unauthenticated.
 */
void test_control_config_refused_without_password()
{
    TestFixture f;
    f.transport.register_routes("");

    reset_capture();
    f.hal.mock_request.set_arg("cmd", "config");
    f.hal.simulate_request("/control", hal::HttpMethod::GET);

    TEST_ASSERT_EQUAL_INT(0, g_dispatch_count);
    TEST_ASSERT_EQUAL_INT(1, f.hal.mock_request.responses.size());
    TEST_ASSERT_EQUAL_INT(200, f.hal.mock_request.responses[0].code);
    TEST_ASSERT_EQUAL_STRING("application/json",
                             f.hal.mock_request.responses[0].content_type.c_str());
    // UNAUTHORIZED = 0x05
    TEST_ASSERT_EQUAL_STRING("{\"result\":\"error\",\"code\":5}",
                             f.hal.mock_request.responses[0].body.c_str());
}

/**
 * @brief /control refuses cmd=config_save while no web password is
 *        configured, even with credentials supplied.
 */
void test_control_config_save_refused_without_password()
{
    TestFixture f;
    f.transport.register_routes("");

    reset_capture();
    f.hal.mock_request.set_arg("cmd", "config_save");
    f.hal.mock_request.set_arg("dest", "nvs");
    f.hal.mock_request.set_auth("admin", "anything");
    f.hal.simulate_request("/control", hal::HttpMethod::GET);

    TEST_ASSERT_EQUAL_INT(0, g_dispatch_count);
    TEST_ASSERT_EQUAL_INT(1, f.hal.mock_request.responses.size());
    TEST_ASSERT_EQUAL_STRING("{\"result\":\"error\",\"code\":5}",
                             f.hal.mock_request.responses[0].body.c_str());
}

/**
 * @brief /control serves cmd=config with correct auth behind a
 *        configured password, as INI text.
 */
void test_control_config_succeeds_with_password()
{
    TestFixture f;
    f.transport.register_routes("secret");

    reset_capture();
    f.hal.mock_request.set_auth("admin", "secret");
    f.hal.mock_request.set_arg("cmd", "config");
    f.hal.simulate_request("/control", hal::HttpMethod::GET);

    TEST_ASSERT_EQUAL_INT(1, g_dispatch_count);
    TEST_ASSERT_EQUAL_INT(1, f.hal.mock_request.responses.size());
    TEST_ASSERT_EQUAL_INT(200, f.hal.mock_request.responses[0].code);
    TEST_ASSERT_EQUAL_STRING("text/plain",
                             f.hal.mock_request.responses[0].content_type.c_str());
    TEST_ASSERT(f.hal.mock_request.responses[0].body.find("captured") !=
                std::string::npos);
}

/**
 * @brief /control refuses cmd=files while no web password is configured.
 */
void test_control_files_refused_without_password()
{
    TestFixture f;
    f.transport.register_routes("");

    reset_capture();
    f.hal.mock_request.set_arg("cmd", "files");
    f.hal.mock_request.set_auth("admin", "anything");
    f.hal.simulate_request("/control", hal::HttpMethod::GET);

    TEST_ASSERT_EQUAL_INT(0, g_dispatch_count);
    TEST_ASSERT_EQUAL_STRING("{\"result\":\"error\",\"code\":5}",
                             f.hal.mock_request.responses[0].body.c_str());
}

/**
 * @brief /control dispatches cmd=files with the dir+page payload when
 *        a password is configured and auth passes.
 */
void test_control_files_succeeds_with_password()
{
    TestFixture f;
    f.transport.register_routes("secret");

    reset_capture();
    f.hal.mock_request.set_auth("admin", "secret");
    f.hal.mock_request.set_arg("cmd", "files");
    f.hal.mock_request.set_arg("dir", "/20250101");
    f.hal.mock_request.set_arg("page", "2");
    f.hal.simulate_request("/control", hal::HttpMethod::GET);

    TEST_ASSERT_EQUAL_INT(1, g_dispatch_count);
    TEST_ASSERT_EQUAL_INT(1, f.hal.mock_request.responses.size());
    TEST_ASSERT_EQUAL_INT(200, f.hal.mock_request.responses[0].code);
    TEST_ASSERT_EQUAL_UINT32(11, g_last_payload_len);
    TEST_ASSERT_TRUE(strncmp(reinterpret_cast<const char*>(g_last_payload),
                             "/20250101 2", 11) == 0);
}

/**
 * @brief cmd=files without dir/page defaults to the SD root, page 0.
 */
void test_control_files_default_dir_and_page()
{
    TestFixture f;
    f.transport.register_routes("secret");

    reset_capture();
    f.hal.mock_request.set_auth("admin", "secret");
    f.hal.mock_request.set_arg("cmd", "files");
    f.hal.simulate_request("/control", hal::HttpMethod::GET);

    TEST_ASSERT_EQUAL_INT(1, g_dispatch_count);
    TEST_ASSERT_EQUAL_UINT32(1, g_last_payload_len);
    TEST_ASSERT_EQUAL_UINT8('/', g_last_payload[0]);
}

/**
 * @brief /control dispatches cmd=config_save with the dest payload
 *        when a password is configured and auth passes.
 */
void test_control_config_save_succeeds_with_password()
{
    TestFixture f;
    f.transport.register_routes("secret");

    reset_capture();
    f.hal.mock_request.set_auth("admin", "secret");
    f.hal.mock_request.set_arg("cmd", "config_save");
    f.hal.mock_request.set_arg("dest", "nvs");
    f.hal.simulate_request("/control", hal::HttpMethod::GET);

    TEST_ASSERT_EQUAL_INT(1, g_dispatch_count);
    TEST_ASSERT_EQUAL_INT(3, g_last_payload_len);
    TEST_ASSERT_EQUAL_UINT8('n', g_last_payload[0]);
    TEST_ASSERT_EQUAL_UINT8('v', g_last_payload[1]);
    TEST_ASSERT_EQUAL_UINT8('s', g_last_payload[2]);
}

/**
 * @brief /control refuses cmd=config_get_value and cmd=config_set_value
 *        while no web password is configured.
 */
void test_control_config_value_refused_without_password()
{
    TestFixture f;
    f.transport.register_routes("");

    reset_capture();
    f.hal.mock_request.set_arg("cmd", "config_get_value");
    f.hal.mock_request.set_arg("key", "wifi.ssid");
    f.hal.simulate_request("/control", hal::HttpMethod::GET);
    TEST_ASSERT_EQUAL_INT(0, g_dispatch_count);
    TEST_ASSERT_EQUAL_STRING("{\"result\":\"error\",\"code\":5}",
                             f.hal.mock_request.responses[0].body.c_str());

    reset_capture();
    f.hal.mock_request.responses.clear();
    f.hal.mock_request.set_arg("cmd", "config_set_value");
    f.hal.mock_request.set_arg("key", "wifi.ssid");
    f.hal.mock_request.set_arg("value", "Home");
    f.hal.simulate_request("/control", hal::HttpMethod::GET);
    TEST_ASSERT_EQUAL_INT(0, g_dispatch_count);
    TEST_ASSERT_EQUAL_STRING("{\"result\":\"error\",\"code\":5}",
                             f.hal.mock_request.responses[0].body.c_str());
}

/**
 * @brief /control dispatches cmd=config_get_value with the key payload
 *        when a password is configured and auth passes.
 */
void test_control_config_get_value_succeeds_with_password()
{
    TestFixture f;
    f.transport.register_routes("secret");

    reset_capture();
    f.hal.mock_request.set_auth("admin", "secret");
    f.hal.mock_request.set_arg("cmd", "config_get_value");
    f.hal.mock_request.set_arg("key", "wifi.ssid");
    f.hal.simulate_request("/control", hal::HttpMethod::GET);

    TEST_ASSERT_EQUAL_INT(1, g_dispatch_count);
    TEST_ASSERT_EQUAL_INT(9, g_last_payload_len);
    TEST_ASSERT_EQUAL_UINT8('w', g_last_payload[0]);
    TEST_ASSERT_EQUAL_UINT8('i', g_last_payload[1]);
    TEST_ASSERT_EQUAL_UINT8('f', g_last_payload[2]);
    TEST_ASSERT_EQUAL_UINT8('i', g_last_payload[3]);
}

/**
 * @brief /control dispatches cmd=config_set_value with the
 *        "key=value" payload when a password is configured and auth
 *        passes.
 */
void test_control_config_set_value_succeeds_with_password()
{
    TestFixture f;
    f.transport.register_routes("secret");

    reset_capture();
    f.hal.mock_request.set_auth("admin", "secret");
    f.hal.mock_request.set_arg("cmd", "config_set_value");
    f.hal.mock_request.set_arg("key", "wifi.ssid");
    f.hal.mock_request.set_arg("value", "Home");
    f.hal.simulate_request("/control", hal::HttpMethod::GET);

    TEST_ASSERT_EQUAL_INT(1, g_dispatch_count);
    TEST_ASSERT_EQUAL_INT(14, g_last_payload_len);
    TEST_ASSERT_TRUE(std::string(
        reinterpret_cast<const char*>(g_last_payload),
        g_last_payload_len) == "wifi.ssid=Home");
}

/**
 * @brief config_get_value without a key arg returns 400.
 */
void test_control_config_get_value_missing_key_returns_400()
{
    TestFixture f;
    f.transport.register_routes("secret");

    reset_capture();
    f.hal.mock_request.set_auth("admin", "secret");
    f.hal.mock_request.set_arg("cmd", "config_get_value");
    f.hal.simulate_request("/control", hal::HttpMethod::GET);

    TEST_ASSERT_EQUAL_INT(0, g_dispatch_count);
    TEST_ASSERT_EQUAL_INT(400, f.hal.mock_request.responses[0].code);
}

/**
 * @brief config_set_value without a key or value arg returns 400.
 */
/**
 * @brief config_set_value without a key arg returns 400; an empty
 *        value is a legitimate blank and dispatches.
 */
void test_control_config_set_value_missing_args_returns_400()
{
    // Missing "value" arg maps to an empty (blank) value and dispatches.
    TestFixture f_blank;
    f_blank.transport.register_routes("secret");

    reset_capture();
    f_blank.hal.mock_request.set_auth("admin", "secret");
    f_blank.hal.mock_request.set_arg("cmd", "config_set_value");
    f_blank.hal.mock_request.set_arg("key", "wifi.ssid");
    f_blank.hal.simulate_request("/control", hal::HttpMethod::GET);

    TEST_ASSERT_EQUAL_INT(1, g_dispatch_count);
    TEST_ASSERT_TRUE(std::string(
        reinterpret_cast<const char*>(g_last_payload),
        g_last_payload_len) == "wifi.ssid=");

    // Missing "key" arg.
    TestFixture f2;
    f2.transport.register_routes("secret");

    reset_capture();
    f2.hal.mock_request.set_auth("admin", "secret");
    f2.hal.mock_request.set_arg("cmd", "config_set_value");
    f2.hal.mock_request.set_arg("value", "Home");
    f2.hal.simulate_request("/control", hal::HttpMethod::GET);

    TEST_ASSERT_EQUAL_INT(0, g_dispatch_count);
    TEST_ASSERT_EQUAL_INT(400, f2.hal.mock_request.responses[0].code);
}

/**
 * @brief /control falls back to GET_STATUS for an unknown cmd.
 */
void test_control_unknown_cmd_falls_back_to_get_status()
{
    TestFixture f;
    f.transport.register_routes("");

    reset_capture();
    f.hal.mock_request.set_arg("cmd", "unknown_command");
    f.hal.simulate_request("/control", hal::HttpMethod::GET);

    // Unknown cmd defaults to GET_STATUS
    TEST_ASSERT_EQUAL_INT(1, g_dispatch_count);
}

// ============================================================
// SET_TIME specific tests
// ============================================================

/**
 * @brief set_time returns 400 when the ts arg is missing.
 */
void test_set_time_missing_ts_returns_400()
{
    TestFixture f;
    f.transport.register_routes("");

    reset_capture();
    f.hal.mock_request.set_arg("cmd", "set_time");
    // No "ts" arg set
    f.hal.simulate_request("/control", hal::HttpMethod::GET);

    TEST_ASSERT_EQUAL_INT(0, g_dispatch_count);
    TEST_ASSERT_EQUAL_INT(1, f.hal.mock_request.responses.size());
    TEST_ASSERT_EQUAL_INT(400, f.hal.mock_request.responses[0].code);
}

/**
 * @brief set_time returns 400 when the ts arg is empty.
 */
void test_set_time_empty_ts_returns_400()
{
    TestFixture f;
    f.transport.register_routes("");

    reset_capture();
    f.hal.mock_request.set_arg("cmd", "set_time");
    f.hal.mock_request.set_arg("ts", "");
    f.hal.simulate_request("/control", hal::HttpMethod::GET);

    TEST_ASSERT_EQUAL_INT(0, g_dispatch_count);
    TEST_ASSERT_EQUAL_INT(400, f.hal.mock_request.responses[0].code);
}

/**
 * @brief set_time packs the timestamp into a 4-byte little-endian payload.
 */
void test_set_time_with_timestamp()
{
    TestFixture f;
    f.transport.register_routes("");

    reset_capture();
    f.hal.mock_request.set_arg("cmd", "set_time");
    f.hal.mock_request.set_arg("ts", "1700000000");
    f.hal.simulate_request("/control", hal::HttpMethod::GET);

    TEST_ASSERT_EQUAL_INT(1, g_dispatch_count);
    TEST_ASSERT_EQUAL_INT(200, f.hal.mock_request.responses[0].code);

    // Verify payload is 4 bytes (little-endian uint32 of 1700000000)
    // 1700000000 = 0x6553F100 → LE: 0x00, 0xF1, 0x53, 0x65
    TEST_ASSERT_EQUAL_INT(4, g_last_payload_len);
    TEST_ASSERT_EQUAL_UINT8(0x00, g_last_payload[0]);
    TEST_ASSERT_EQUAL_UINT8(0xF1, g_last_payload[1]);
    TEST_ASSERT_EQUAL_UINT8(0x53, g_last_payload[2]);
    TEST_ASSERT_EQUAL_UINT8(0x65, g_last_payload[3]);
}

/**
 * @brief set_time with a zero timestamp sends a zeroed payload.
 */
void test_set_time_zero_timestamp()
{
    TestFixture f;
    f.transport.register_routes("");

    reset_capture();
    f.hal.mock_request.set_arg("cmd", "set_time");
    f.hal.mock_request.set_arg("ts", "0");
    f.hal.simulate_request("/control", hal::HttpMethod::GET);

    TEST_ASSERT_EQUAL_INT(1, g_dispatch_count);
    TEST_ASSERT_EQUAL_INT(200, f.hal.mock_request.responses[0].code);

    // All 4 bytes should be 0
    TEST_ASSERT_EQUAL_INT(4, g_last_payload_len);
    TEST_ASSERT_EQUAL_UINT8(0, g_last_payload[0]);
    TEST_ASSERT_EQUAL_UINT8(0, g_last_payload[1]);
    TEST_ASSERT_EQUAL_UINT8(0, g_last_payload[2]);
    TEST_ASSERT_EQUAL_UINT8(0, g_last_payload[3]);
}

// ============================================================
// Fallback JSON response path (handler writes no payload)
// ============================================================

/**
 * @brief A successful command with no response payload sends {"result":"ok"}.
 */
void test_fallback_success_no_payload()
{
    // Use a dedicated HAL + handler so only the no-response route is registered.
    MockWebServerHal hal;
    MockLogBackend log_backend;
    RealTimeService time;
    LoggingService logger(log_backend, time);
    CommandHandler no_resp_handler(logger);
    no_resp_handler.register_handler(CommandCode::GET_STATUS,
                                     no_response_handler);
    WebCommandTransport no_resp_transport(hal, no_resp_handler);
    no_resp_transport.register_routes("");

    hal.simulate_request("/status", hal::HttpMethod::GET);

    TEST_ASSERT_EQUAL_INT(1, hal.mock_request.responses.size());
    TEST_ASSERT_EQUAL_INT(200, hal.mock_request.responses[0].code);
    TEST_ASSERT_EQUAL_STRING("application/json",
                             hal.mock_request.responses[0].content_type.c_str());
    TEST_ASSERT_EQUAL_STRING("{\"result\":\"ok\"}",
                             hal.mock_request.responses[0].body.c_str());
}

/**
 * @brief A failing command with no payload sends {"result":"error","code":N}.
 */
void test_fallback_error_no_payload()
{
    // Use a fresh HAL + command so only the /status route is registered.
    MockWebServerHal hal;
    MockLogBackend log_backend;
    RealTimeService time;
    LoggingService logger(log_backend, time);
    CommandHandler err_handler(logger);
    err_handler.register_handler(CommandCode::GET_STATUS,
                                 error_no_response_handler);
    WebCommandTransport err_transport(hal, err_handler);
    err_transport.register_routes("");

    hal.simulate_request("/status", hal::HttpMethod::GET);

    TEST_ASSERT_EQUAL_INT(1, hal.mock_request.responses.size());
    TEST_ASSERT_EQUAL_INT(200, hal.mock_request.responses[0].code);
    // INVALID_STATE = 0x03
    TEST_ASSERT_EQUAL_STRING("{\"result\":\"error\",\"code\":3}",
                             hal.mock_request.responses[0].body.c_str());
}

/**
 * @brief SD_UNAVAILABLE (0x04) renders as {"result":"error","code":4}.
 */
void test_fallback_error_sd_unavailable()
{
    MockWebServerHal hal;
    MockLogBackend log_backend;
    RealTimeService time;
    LoggingService logger(log_backend, time);
    CommandHandler err_handler(logger);
    err_handler.register_handler(CommandCode::GET_STATUS,
                                 [](const uint8_t*, size_t, uint8_t*,
                                    size_t& responseLen) -> CommandResult
                                 {
                                     responseLen = 0;
                                     return CommandResult::SD_UNAVAILABLE;
                                 });
    WebCommandTransport err_transport(hal, err_handler);
    err_transport.register_routes("");

    hal.simulate_request("/status", hal::HttpMethod::GET);

    TEST_ASSERT_EQUAL_INT(1, hal.mock_request.responses.size());
    TEST_ASSERT_EQUAL_INT(200, hal.mock_request.responses[0].code);
    // SD_UNAVAILABLE = 0x04
    TEST_ASSERT_EQUAL_STRING("{\"result\":\"error\",\"code\":4}",
                             hal.mock_request.responses[0].body.c_str());
}

// ============================================================
// main
// ============================================================

int main()
{
    UNITY_BEGIN();

    // Route registration
    RUN_TEST(test_register_routes_creates_status_and_control);
    RUN_TEST(test_register_routes_with_null_password);

    // Auth
    RUN_TEST(test_status_no_password_allows_all);
    RUN_TEST(test_status_no_auth_header_returns_401);
    RUN_TEST(test_status_wrong_auth_returns_401);
    RUN_TEST(test_status_correct_auth_succeeds);

    // Status
    RUN_TEST(test_status_dispatches_get_status);

    // Control — command mapping
    RUN_TEST(test_control_missing_cmd_returns_400);
    RUN_TEST(test_control_empty_cmd_returns_400);
    RUN_TEST(test_control_start_recording);
    RUN_TEST(test_control_stop_recording);
    RUN_TEST(test_control_take_photo);
    RUN_TEST(test_control_config_refused_without_password);
    RUN_TEST(test_control_config_save_refused_without_password);
    RUN_TEST(test_control_config_succeeds_with_password);
    RUN_TEST(test_control_config_save_succeeds_with_password);
    RUN_TEST(test_control_config_value_refused_without_password);
    RUN_TEST(test_control_config_get_value_succeeds_with_password);
    RUN_TEST(test_control_config_set_value_succeeds_with_password);
    RUN_TEST(test_control_config_get_value_missing_key_returns_400);
    RUN_TEST(test_control_config_set_value_missing_args_returns_400);
    RUN_TEST(test_control_unknown_cmd_falls_back_to_get_status);

    // LIST_FILES (files)
    RUN_TEST(test_control_files_refused_without_password);
    RUN_TEST(test_control_files_succeeds_with_password);
    RUN_TEST(test_control_files_default_dir_and_page);

    // SET_TIME
    RUN_TEST(test_set_time_missing_ts_returns_400);
    RUN_TEST(test_set_time_empty_ts_returns_400);
    RUN_TEST(test_set_time_with_timestamp);
    RUN_TEST(test_set_time_zero_timestamp);

    // Fallback JSON response path
    RUN_TEST(test_fallback_success_no_payload);
    RUN_TEST(test_fallback_error_no_payload);
    RUN_TEST(test_fallback_error_sd_unavailable);

    return UNITY_END();
}
