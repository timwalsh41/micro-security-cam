#include <unity.h>
#include "web_server_service.h"
#include "web_auth_util.h"
#include "mock_web_server_hal.h"
#include "mock_log_backend.h"
#include "time_service.h"
#include <cstring>

/**
 * @file test_web_server_service.cpp
 * @brief Native unit tests for WebServerService and web_auth_util.
 *
 * Tests cover:
 *  - check_basic_auth() shared utility (Base64 decoding, edge cases)
 *  - WebServerService init, handle_index (via simulate_request), and auth flow
 */

// ============================================================
// Test fixture
// ============================================================

struct TestFixture
{
    MockWebServerHal hal;
    MockLogBackend log_backend;
    RealTimeService time;
    LoggingService logger;
    WebServerService service;

    TestFixture()
        : logger(log_backend, time)
        , service(hal, logger)
    {
    }
};

static ConfigData::WebConfig make_web_config(uint16_t port = 80,
                                              const char* password = "")
{
    ConfigData::WebConfig cfg{};
    cfg.port = port;
    strncpy(cfg.password, password, CONFIG_FIELD_MAX_LENGTH - 1);
    return cfg;
}

// ============================================================
// check_basic_auth() tests
// ============================================================

/**
 * @brief check_basic_auth() allows all requests when no password is set.
 */
void test_auth_empty_password_allows_all()
{
    TEST_ASSERT_TRUE(check_basic_auth(nullptr, ""));
    TEST_ASSERT_TRUE(check_basic_auth("", ""));
    TEST_ASSERT_TRUE(check_basic_auth("Basic dXNlcjpwYXNz", ""));
}

/**
 * @brief check_basic_auth() rejects a null auth header.
 */
void test_auth_null_header_rejected()
{
    TEST_ASSERT_FALSE(check_basic_auth(nullptr, "secret"));
}

/**
 * @brief check_basic_auth() rejects an empty auth header.
 */
void test_auth_empty_header_rejected()
{
    TEST_ASSERT_FALSE(check_basic_auth("", "secret"));
}

/**
 * @brief check_basic_auth() rejects a non-Basic auth scheme.
 */
void test_auth_non_basic_scheme_rejected()
{
    TEST_ASSERT_FALSE(check_basic_auth("Bearer abc123", "secret"));
}

/**
 * @brief check_basic_auth() accepts correct credentials.
 */
void test_auth_correct_credentials_pass()
{
    TEST_ASSERT_TRUE(check_basic_auth("Basic dXNlcjpzZWNyZXQ=", "secret"));
}

/**
 * @brief check_basic_auth() rejects a wrong password.
 */
void test_auth_wrong_password_rejected()
{
    TEST_ASSERT_FALSE(check_basic_auth("Basic dXNlcjpzZWNyZXQ=", "wrong"));
}

/**
 * @brief check_basic_auth() rejects a malformed header with no colon.
 */
void test_auth_malformed_no_colon_rejected()
{
    TEST_ASSERT_FALSE(check_basic_auth("Basic dXNlcnNlY3JldA==", "secret"));
}

/**
 * @brief check_basic_auth() rejects invalid base64.
 */
void test_auth_invalid_base64_rejected()
{
    TEST_ASSERT_FALSE(check_basic_auth("Basic !!!invalid!!!", "secret"));
}

/**
 * @brief check_basic_auth() handles base64 with padding.
 */
void test_auth_with_padding()
{
    // "a:b" → base64 = "YTpi"
    TEST_ASSERT_TRUE(check_basic_auth("Basic YTpi", "b"));
}

/**
 * @brief check_basic_auth() handles special characters in the password.
 */
void test_auth_special_characters()
{
    // "admin:p@ss w0rd!" → base64
    TEST_ASSERT_TRUE(
        check_basic_auth("Basic YWRtaW46cEBzcyB3MHJkIQ==", "p@ss w0rd!"));
}

// ============================================================
// WebServerService init tests
// ============================================================

/**
 * @brief init() registers the index route.
 */
void test_init_registers_index_route()
{
    TestFixture f;
    f.service.init(make_web_config());

    TEST_ASSERT_EQUAL_INT(1, f.hal.registered_routes.size());
    TEST_ASSERT_EQUAL_STRING("/", f.hal.registered_routes[0].uri.c_str());
    TEST_ASSERT_EQUAL(hal::HttpMethod::GET,
                      f.hal.registered_routes[0].method);
}

/**
 * @brief init() calls the HAL and logs success.
 */
void test_init_logs_success()
{
    TestFixture f;
    f.service.init(make_web_config(8080));

    TEST_ASSERT_EQUAL_INT(1, f.hal.init_call_count);
    TEST_ASSERT_TRUE(f.log_backend.messages.size() > 0);
}

/**
 * @brief init() logs an error but does not crash on HAL failure.
 */
void test_init_failure()
{
    TestFixture f;
    f.hal.init_result = false;
    f.service.init(make_web_config());

    // Should log an error but not crash
    TEST_ASSERT_TRUE(f.log_backend.messages.size() > 0);
}

/**
 * @brief The landing page route registers even when HAL init fails.
 *
 * The route attaches to the HAL before the server start is requested,
 * so a failed start must not drop the registration (it stays pending
 * for a later server start).
 */
void test_init_failure_still_registers_index_route()
{
    TestFixture f;
    f.hal.init_result = false;
    f.service.init(make_web_config());

    TEST_ASSERT_EQUAL_INT(1, f.hal.registered_routes.size());
    TEST_ASSERT_EQUAL_STRING("/", f.hal.registered_routes[0].uri.c_str());
}

// ============================================================
// WebServerService handle_index (landing page + auth) tests
// ============================================================

/**
 * @brief handle_index() sends an HTML landing page.
 */
void test_handle_index_sends_html()
{
    TestFixture f;
    f.service.init(make_web_config());

    f.hal.simulate_request("/", hal::HttpMethod::GET);

    TEST_ASSERT_EQUAL_INT(1, f.hal.mock_request.responses.size());
    TEST_ASSERT_EQUAL_INT(200, f.hal.mock_request.responses[0].code);
    TEST_ASSERT_EQUAL_STRING("text/html",
                             f.hal.mock_request.responses[0].content_type.c_str());
    TEST_ASSERT_TRUE(f.hal.mock_request.responses[0].body.find("ESP32") !=
                     std::string::npos);
}

/**
 * @brief handle_index() serves the page when no password is set.
 */
void test_handle_index_no_password()
{
    TestFixture f;
    f.service.init(make_web_config());

    f.hal.simulate_request("/", hal::HttpMethod::GET);

    TEST_ASSERT_EQUAL_INT(1, f.hal.mock_request.responses.size());
    TEST_ASSERT_EQUAL_INT(200, f.hal.mock_request.responses[0].code);
}

/**
 * @brief handle_index() returns 401 when no auth header is present.
 */
void test_handle_index_no_auth_header_returns_401()
{
    TestFixture f;
    f.service.init(make_web_config(80, "mypass"));

    // No auth header set on mock_request — should get 401
    f.hal.simulate_request("/", hal::HttpMethod::GET);

    TEST_ASSERT_EQUAL_INT(1, f.hal.mock_request.responses.size());
    TEST_ASSERT_EQUAL_INT(401, f.hal.mock_request.responses[0].code);
}

/**
 * @brief handle_index() returns 401 for a wrong password.
 */
void test_handle_index_wrong_password_returns_401()
{
    TestFixture f;
    f.service.init(make_web_config(80, "mypass"));

    f.hal.mock_request.set_auth("user", "wrongpass");
    f.hal.simulate_request("/", hal::HttpMethod::GET);

    TEST_ASSERT_EQUAL_INT(1, f.hal.mock_request.responses.size());
    TEST_ASSERT_EQUAL_INT(401, f.hal.mock_request.responses[0].code);
}

/**
 * @brief handle_index() returns 200 for the correct password.
 */
void test_handle_index_correct_password()
{
    TestFixture f;
    f.service.init(make_web_config(80, "mypass"));

    f.hal.mock_request.set_auth("user", "mypass");
    f.hal.simulate_request("/", hal::HttpMethod::GET);

    TEST_ASSERT_EQUAL_INT(1, f.hal.mock_request.responses.size());
    TEST_ASSERT_EQUAL_INT(200, f.hal.mock_request.responses[0].code);
}

/**
 * @brief handle_index() returns 401 for a malformed auth header.
 */
void test_handle_index_malformed_auth_returns_401()
{
    TestFixture f;
    f.service.init(make_web_config(80, "mypass"));

    // Set a raw (non-Base64) auth header directly on mock_request
    strncpy(f.hal.mock_request.auth_value, "Basic garbage!!!",
            sizeof(f.hal.mock_request.auth_value) - 1);
    f.hal.simulate_request("/", hal::HttpMethod::GET);

    TEST_ASSERT_EQUAL_INT(1, f.hal.mock_request.responses.size());
    TEST_ASSERT_EQUAL_INT(401, f.hal.mock_request.responses[0].code);
}

// ============================================================
// main
// ============================================================

int main()
{
    UNITY_BEGIN();

    // check_basic_auth()
    RUN_TEST(test_auth_empty_password_allows_all);
    RUN_TEST(test_auth_null_header_rejected);
    RUN_TEST(test_auth_empty_header_rejected);
    RUN_TEST(test_auth_non_basic_scheme_rejected);
    RUN_TEST(test_auth_correct_credentials_pass);
    RUN_TEST(test_auth_wrong_password_rejected);
    RUN_TEST(test_auth_malformed_no_colon_rejected);
    RUN_TEST(test_auth_invalid_base64_rejected);
    RUN_TEST(test_auth_with_padding);
    RUN_TEST(test_auth_special_characters);

    // WebServerService init
    RUN_TEST(test_init_registers_index_route);
    RUN_TEST(test_init_logs_success);
    RUN_TEST(test_init_failure);
    RUN_TEST(test_init_failure_still_registers_index_route);

    // handle_index + auth (via simulate_request)
    RUN_TEST(test_handle_index_sends_html);
    RUN_TEST(test_handle_index_no_password);
    RUN_TEST(test_handle_index_no_auth_header_returns_401);
    RUN_TEST(test_handle_index_wrong_password_returns_401);
    RUN_TEST(test_handle_index_correct_password);
    RUN_TEST(test_handle_index_malformed_auth_returns_401);

    return UNITY_END();
}
