#include <unity.h>
#include "logging_service.h"
#include "mock_log_backend.h"
#include "time_service.h"
#include <Arduino.h>
#include <string>

// ============================================================
// Test fixtures
// ============================================================

static MockLogBackend mock_backend;
static RealTimeService real_time;

// ============================================================
// Level formatting tests
// ============================================================

/**
 * @brief debug() emits a DEBUG level tag.
 */
void test_debug_level_format()
{
    LoggingService log(mock_backend, real_time);
    set_mock_millis(5000);
    log.debug("Test", "hello");

    TEST_ASSERT_EQUAL_INT(1, mock_backend.write_call_count);
    TEST_ASSERT_TRUE(mock_backend.last_message.find("[DEBUG]") != std::string::npos);
}

/**
 * @brief info() emits an INFO level tag.
 */
void test_info_level_format()
{
    LoggingService log(mock_backend, real_time);
    set_mock_millis(5000);
    log.info("Test", "hello");

    TEST_ASSERT_EQUAL_INT(1, mock_backend.write_call_count);
    TEST_ASSERT_TRUE(mock_backend.last_message.find("[INFO ]") != std::string::npos);
}

/**
 * @brief warn() emits a WARN level tag.
 */
void test_warn_level_format()
{
    LoggingService log(mock_backend, real_time);
    set_mock_millis(5000);
    log.warn("Test", "hello");

    TEST_ASSERT_EQUAL_INT(1, mock_backend.write_call_count);
    TEST_ASSERT_TRUE(mock_backend.last_message.find("[WARN ]") != std::string::npos);
}

/**
 * @brief error() emits an ERROR level tag.
 */
void test_error_level_format()
{
    LoggingService log(mock_backend, real_time);
    set_mock_millis(5000);
    log.error("Test", "hello");

    TEST_ASSERT_EQUAL_INT(1, mock_backend.write_call_count);
    TEST_ASSERT_TRUE(mock_backend.last_message.find("[ERROR]") != std::string::npos);
}

/**
 * @brief log() with a WARNING level emits a WARN tag.
 */
void test_generic_log_method_level()
{
    LoggingService log(mock_backend, real_time);
    set_mock_millis(100);
    log.log(LogLevel::WARNING, "Sub", "msg");

    TEST_ASSERT_TRUE(mock_backend.last_message.find("[WARN ]") != std::string::npos);
}

// ============================================================
// Subsystem tests
// ============================================================

/**
 * @brief The subsystem name appears in the output.
 */
void test_subsystem_appears_in_output()
{
    LoggingService log(mock_backend, real_time);
    set_mock_millis(0);
    log.info("Camera", "capture started");

    TEST_ASSERT_TRUE(mock_backend.last_message.find("[Camera]") != std::string::npos);
}

/**
 * @brief Different subsystem names are preserved in the output.
 */
void test_subsystem_different_names()
{
    LoggingService log(mock_backend, real_time);
    set_mock_millis(0);
    log.info("SD", "card ready");

    TEST_ASSERT_TRUE(mock_backend.last_message.find("[SD]") != std::string::npos);
}

// ============================================================
// Printf formatting tests
// ============================================================

/**
 * @brief printf-style formatting substitutes a string argument.
 */
void test_printf_formatting_string()
{
    LoggingService log(mock_backend, real_time);
    set_mock_millis(0);
    log.info("Main", "Photo %s", "saved");

    TEST_ASSERT_TRUE(mock_backend.last_message.find("Photo saved") != std::string::npos);
}

/**
 * @brief printf-style formatting substitutes an integer argument.
 */
void test_printf_formatting_int()
{
    LoggingService log(mock_backend, real_time);
    set_mock_millis(0);
    log.info("Main", "Count: %d", 42);

    TEST_ASSERT_TRUE(mock_backend.last_message.find("Count: 42") != std::string::npos);
}

/**
 * @brief printf-style formatting substitutes multiple arguments.
 */
void test_printf_formatting_multiple_args()
{
    LoggingService log(mock_backend, real_time);
    set_mock_millis(0);
    log.info("Main", "File: %s, size: %d", "/img.jpg", 1234);

    TEST_ASSERT_TRUE(mock_backend.last_message.find("File: /img.jpg, size: 1234") != std::string::npos);
}

// ============================================================
// Convenience method tests
// ============================================================

/**
 * @brief Convenience methods route to the correct log level.
 */
void test_convenience_methods_route_to_correct_level()
{
    LoggingService log(mock_backend, real_time);
    set_mock_millis(0);

    log.debug("S", "d");
    log.info("S", "i");
    log.warn("S", "w");
    log.error("S", "e");

    TEST_ASSERT_EQUAL_INT(4, mock_backend.write_call_count);
    TEST_ASSERT_TRUE(mock_backend.messages[0].find("[DEBUG]") != std::string::npos);
    TEST_ASSERT_TRUE(mock_backend.messages[1].find("[INFO ]") != std::string::npos);
    TEST_ASSERT_TRUE(mock_backend.messages[2].find("[WARN ]") != std::string::npos);
    TEST_ASSERT_TRUE(mock_backend.messages[3].find("[ERROR]") != std::string::npos);
}

// ============================================================
// Timestamp tests
// ============================================================

/**
 * @brief The timestamp appears in the output.
 */
void test_timestamp_appears_in_output()
{
    LoggingService log(mock_backend, real_time);
    set_mock_millis(12345);
    log.info("Test", "msg");

    TEST_ASSERT_TRUE(mock_backend.last_message.find("[00012345]") != std::string::npos);
}

/**
 * @brief A zero timestamp is formatted correctly.
 */
void test_timestamp_zero()
{
    LoggingService log(mock_backend, real_time);
    set_mock_millis(0);
    log.info("Test", "msg");

    TEST_ASSERT_TRUE(mock_backend.last_message.find("[00000000]") != std::string::npos);
}

/**
 * @brief The timestamp updates across successive log calls.
 */
void test_timestamp_updates()
{
    LoggingService log(mock_backend, real_time);
    set_mock_millis(1000);
    log.info("Test", "first");

    set_mock_millis(2000);
    log.info("Test", "second");

    TEST_ASSERT_TRUE(mock_backend.messages[0].find("[00001000]") != std::string::npos);
    TEST_ASSERT_TRUE(mock_backend.messages[1].find("[00002000]") != std::string::npos);
}

// ============================================================
// Full line structure test
// ============================================================

/**
 * @brief A full log line has the expected structure.
 */
void test_full_line_format()
{
    LoggingService log(mock_backend, real_time);
    set_mock_millis(1234);
    log.info("Camera", "Photo saved");

    TEST_ASSERT_EQUAL_STRING("[00001234] [Camera] [INFO ] Photo saved",
                             mock_backend.last_message.c_str());
}

/**
 * @brief Multiple log calls accumulate in the backend.
 */
void test_multiple_calls_accumulate()
{
    LoggingService log(mock_backend, real_time);
    set_mock_millis(100);
    log.info("A", "first");
    log.warn("B", "second");

    TEST_ASSERT_EQUAL_INT(2, mock_backend.write_call_count);
}

// ============================================================
// Main
// ============================================================

void setUp()
{
    mock_backend.reset();
    set_mock_millis(0);
}

void tearDown()
{
}

int main()
{
    UNITY_BEGIN();

    // Level formatting
    RUN_TEST(test_debug_level_format);
    RUN_TEST(test_info_level_format);
    RUN_TEST(test_warn_level_format);
    RUN_TEST(test_error_level_format);
    RUN_TEST(test_generic_log_method_level);

    // Subsystem
    RUN_TEST(test_subsystem_appears_in_output);
    RUN_TEST(test_subsystem_different_names);

    // Printf formatting
    RUN_TEST(test_printf_formatting_string);
    RUN_TEST(test_printf_formatting_int);
    RUN_TEST(test_printf_formatting_multiple_args);

    // Convenience methods
    RUN_TEST(test_convenience_methods_route_to_correct_level);

    // Timestamp
    RUN_TEST(test_timestamp_appears_in_output);
    RUN_TEST(test_timestamp_zero);
    RUN_TEST(test_timestamp_updates);

    // Full line structure
    RUN_TEST(test_full_line_format);
    RUN_TEST(test_multiple_calls_accumulate);

    return UNITY_END();
}
