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
 *  - WebServerService init, handle_index, and auth flow
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
 * @brief Empty password allows all requests.
 */
void test_auth_empty_password_allows_all()
{
    TEST_ASSERT_TRUE(check_basic_auth(nullptr, ""));
    TEST_ASSERT_TRUE(check_basic_auth("", ""));
    TEST_ASSERT_TRUE(check_basic_auth("Basic dXNlcjpwYXNz", ""));
}

/**
 * @brief Null auth header is rejected when password is set.
 */
void test_auth_null_header_rejected()
{
    TEST_ASSERT_FALSE(check_basic_auth(nullptr, "secret"));
}

/**
 * @brief Empty auth header is rejected when password is set.
 */
void test_auth_empty_header_rejected()
{
    TEST_ASSERT_FALSE(check_basic_auth("", "secret"));
}

/**
 * @brief Non-Basic auth scheme is rejected.
 */
void test_auth_non_basic_scheme_rejected()
{
    TEST_ASSERT_FALSE(check_basic_auth("Bearer abc123", "secret"));
}

/**
 * @brief Correct credentials pass.
 */
void test_auth_correct_credentials_pass()
{
    // "user:secret" → base64
    TEST_ASSERT_TRUE(check_basic_auth("Basic dXNlcjpzZWNyZXQ=", "secret"));
}

/**
 * @brief Wrong password is rejected.
 */
void test_auth_wrong_password_rejected()
{
    TEST_ASSERT_FALSE(check_basic_auth("Basic dXNlcjpzZWNyZXQ=", "wrong"));
}

/**
 * @brief Malformed Base64 (no colon separator) is rejected.
 */
void test_auth_malformed_no_colon_rejected()
{
    // "usersecret" base64-encoded (no colon)
    TEST_ASSERT_FALSE(check_basic_auth("Basic dXNlcnNlY3JldA==", "secret"));
}

/**
 * @brief Invalid Base64 characters are rejected.
 */
void test_auth_invalid_base64_rejected()
{
    TEST_ASSERT_FALSE(check_basic_auth("Basic !!!invalid!!!", "secret"));
}

/**
 * @brief Base64 with padding works correctly.
 */
void test_auth_with_padding()
{
    // "a:b" → base64 = "YTpi"
    TEST_ASSERT_TRUE(check_basic_auth("Basic YTpi", "b"));
}

/**
 * @brief Longer password with special characters works.
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
 * @brief init() logs server start on success.
 */
void test_init_logs_success()
{
    TestFixture f;
    f.service.init(make_web_config(8080));

    TEST_ASSERT_EQUAL_INT(1, f.hal.begin_call_count);
    TEST_ASSERT_TRUE(f.log_backend.messages.size() > 0);
}

/**
 * @brief init() handles begin() failure gracefully.
 */
void test_init_begin_failure()
{
    TestFixture f;
    f.hal.begin_result = false;
    f.service.init(make_web_config());

    // Should log an error but not crash
    TEST_ASSERT_TRUE(f.log_backend.messages.size() > 0);
}

// ============================================================
// WebServerService handle_client tests
// ============================================================

/**
 * @brief handle_client() delegates to HAL.
 */
void test_handle_client_delegates_to_hal()
{
    TestFixture f;
    f.service.init(make_web_config());

    f.service.handle_client();
    f.service.handle_client();
    f.service.handle_client();

    TEST_ASSERT_EQUAL_INT(3, f.hal.handle_client_call_count);
}

// ============================================================
// WebServerService handle_index (landing page + auth) tests
// ============================================================

/**
 * @brief handle_index() sends HTML landing page.
 */
void test_handle_index_sends_html()
{
    TestFixture f;
    f.service.init(make_web_config());

    f.service.handle_index();

    TEST_ASSERT_EQUAL_INT(1, f.hal.responses.size());
    TEST_ASSERT_EQUAL_INT(200, f.hal.responses[0].code);
    TEST_ASSERT_EQUAL_STRING("text/html",
                             f.hal.responses[0].content_type.c_str());
    TEST_ASSERT_TRUE(f.hal.responses[0].body.find("ESP32") !=
                     std::string::npos);
}

/**
 * @brief handle_index() succeeds with no password configured.
 */
void test_handle_index_no_password()
{
    TestFixture f;
    f.service.init(make_web_config());

    f.service.handle_index();

    TEST_ASSERT_EQUAL_INT(1, f.hal.responses.size());
    TEST_ASSERT_EQUAL_INT(200, f.hal.responses[0].code);
}

/**
 * @brief handle_index() returns 401 when no auth header and password set.
 */
void test_handle_index_no_auth_header_returns_401()
{
    TestFixture f;
    f.service.init(make_web_config(80, "mypass"));

    // No auth header set — should get 401
    f.service.handle_index();

    TEST_ASSERT_EQUAL_INT(1, f.hal.responses.size());
    TEST_ASSERT_EQUAL_INT(401, f.hal.responses[0].code);
}

/**
 * @brief handle_index() returns 401 with wrong password.
 */
void test_handle_index_wrong_password_returns_401()
{
    TestFixture f;
    f.service.init(make_web_config(80, "mypass"));

    f.hal.set_auth("user", "wrongpass");
    f.service.handle_index();

    TEST_ASSERT_EQUAL_INT(1, f.hal.responses.size());
    TEST_ASSERT_EQUAL_INT(401, f.hal.responses[0].code);
}

/**
 * @brief handle_index() succeeds with correct password.
 */
void test_handle_index_correct_password()
{
    TestFixture f;
    f.service.init(make_web_config(80, "mypass"));

    f.hal.set_auth("user", "mypass");
    f.service.handle_index();

    TEST_ASSERT_EQUAL_INT(1, f.hal.responses.size());
    TEST_ASSERT_EQUAL_INT(200, f.hal.responses[0].code);
}

/**
 * @brief handle_index() rejects malformed Authorization header.
 */
void test_handle_index_malformed_auth_returns_401()
{
    TestFixture f;
    f.service.init(make_web_config(80, "mypass"));

    // Set a raw (non-Base64) auth header
    strncpy(f.hal.auth_buffer, "Basic garbage!!!",
            sizeof(f.hal.auth_buffer) - 1);
    f.service.handle_index();

    TEST_ASSERT_EQUAL_INT(1, f.hal.responses.size());
    TEST_ASSERT_EQUAL_INT(401, f.hal.responses[0].code);
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
    RUN_TEST(test_init_begin_failure);

    // handle_client
    RUN_TEST(test_handle_client_delegates_to_hal);

    // handle_index + auth
    RUN_TEST(test_handle_index_sends_html);
    RUN_TEST(test_handle_index_no_password);
    RUN_TEST(test_handle_index_no_auth_header_returns_401);
    RUN_TEST(test_handle_index_wrong_password_returns_401);
    RUN_TEST(test_handle_index_correct_password);
    RUN_TEST(test_handle_index_malformed_auth_returns_401);

    return UNITY_END();
}
