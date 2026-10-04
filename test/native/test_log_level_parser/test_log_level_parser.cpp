#include <unity.h>
#include "log_level_parser.h"

// ============================================================
// Name mapping
// ============================================================

/**
 * @brief "debug" maps to DEBUG.
 */
void test_parse_debug_maps_to_debug()
{
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(hal::LogLevel::DEBUG),
        static_cast<uint8_t>(log_level::parse("debug")));
}

/**
 * @brief "info" maps to INFO.
 */
void test_parse_info_maps_to_info()
{
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(hal::LogLevel::INFO),
        static_cast<uint8_t>(log_level::parse("info")));
}

/**
 * @brief "warn" maps to WARNING.
 */
void test_parse_warn_maps_to_warning()
{
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(hal::LogLevel::WARNING),
        static_cast<uint8_t>(log_level::parse("warn")));
}

/**
 * @brief "warning" maps to WARNING.
 */
void test_parse_warning_maps_to_warning()
{
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(hal::LogLevel::WARNING),
        static_cast<uint8_t>(log_level::parse("warning")));
}

/**
 * @brief "error" maps to ERROR.
 */
void test_parse_error_maps_to_error()
{
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(hal::LogLevel::ERROR),
        static_cast<uint8_t>(log_level::parse("error")));
}

/**
 * @brief Matching is case-insensitive for every alias.
 */
void test_parse_matching_is_case_insensitive()
{
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(hal::LogLevel::DEBUG),
        static_cast<uint8_t>(log_level::parse("DEBUG")));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(hal::LogLevel::INFO),
        static_cast<uint8_t>(log_level::parse("Info")));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(hal::LogLevel::WARNING),
        static_cast<uint8_t>(log_level::parse("WaRn")));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(hal::LogLevel::ERROR),
        static_cast<uint8_t>(log_level::parse("Error")));
}

/**
 * @brief A nullptr name defaults to INFO.
 */
void test_parse_null_defaults_to_info()
{
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(hal::LogLevel::INFO),
        static_cast<uint8_t>(log_level::parse(nullptr)));
}

/**
 * @brief An unrecognized name defaults to INFO.
 */
void test_parse_unknown_name_defaults_to_info()
{
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(hal::LogLevel::INFO),
        static_cast<uint8_t>(log_level::parse("trace")));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(hal::LogLevel::INFO),
        static_cast<uint8_t>(log_level::parse("")));
}

// ============================================================
// Test runner
// ============================================================

void setUp()
{
}

void tearDown()
{
}

int main()
{
    UNITY_BEGIN();

    RUN_TEST(test_parse_debug_maps_to_debug);
    RUN_TEST(test_parse_info_maps_to_info);
    RUN_TEST(test_parse_warn_maps_to_warning);
    RUN_TEST(test_parse_warning_maps_to_warning);
    RUN_TEST(test_parse_error_maps_to_error);
    RUN_TEST(test_parse_matching_is_case_insensitive);
    RUN_TEST(test_parse_null_defaults_to_info);
    RUN_TEST(test_parse_unknown_name_defaults_to_info);

    return UNITY_END();
}
