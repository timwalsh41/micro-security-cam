#include <unity.h>
#include "time_formatter.h"
#include <cstring>

// ============================================================
// format_date — YYYYMMDD (UTC)
// ============================================================

/**
 * @brief Epoch timestamp formats as 1970-01-01.
 */
void test_format_date_epoch()
{
    char out[16] = {0};
    TEST_ASSERT_TRUE(TimeFormatter::format_date(0, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("19700101", out);
}

/**
 * @brief Leap day (2000-02-29) is formatted correctly.
 *
 * 951782400 = 2000-02-29T00:00:00Z — exercises leap-year handling in
 * gmtime_r (which the previous day-of-year approximation in
 * VideoController got wrong for leap years).
 */
void test_format_date_leap_day()
{
    char out[16] = {0};
    TEST_ASSERT_TRUE(TimeFormatter::format_date(951782400u, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("20000229", out);
}

/**
 * @brief A recent timestamp formats correctly.
 *
 * 1710000000 = 2024-03-09T16:00:00Z.
 */
void test_format_date_recent()
{
    char out[16] = {0};
    TEST_ASSERT_TRUE(TimeFormatter::format_date(1710000000u, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("20240309", out);
}

/**
 * @brief A minimum-size buffer (9) is sufficient.
 */
void test_format_date_minimum_buffer_size()
{
    char out[9];
    TEST_ASSERT_TRUE(TimeFormatter::format_date(1710000000u, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("20240309", out);
}

/**
 * @brief An undersized buffer is rejected.
 */
void test_format_date_buffer_too_small()
{
    char out[8];
    TEST_ASSERT_FALSE(TimeFormatter::format_date(1710000000u, out, sizeof(out)));
}

/**
 * @brief A null buffer is rejected.
 */
void test_format_date_null_buffer()
{
    TEST_ASSERT_FALSE(TimeFormatter::format_date(1710000000u, nullptr, 16));
}

// ============================================================
// format_time — HHMMSS (UTC)
// ============================================================

/**
 * @brief Epoch timestamp formats as 00:00:00.
 */
void test_format_time_epoch_midnight()
{
    char out[16] = {0};
    TEST_ASSERT_TRUE(TimeFormatter::format_time(0, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("000000", out);
}

/**
 * @brief A known time-of-day formats correctly.
 *
 * 45296 = 12*3600 + 34*60 + 56 → 12:34:56Z.
 */
void test_format_time_known_time()
{
    char out[16] = {0};
    TEST_ASSERT_TRUE(TimeFormatter::format_time(45296u, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("123456", out);
}

/**
 * @brief A recent timestamp formats its time-of-day correctly.
 *
 * 1710000000 = 2024-03-09T16:00:00Z → 16:00:00.
 */
void test_format_time_recent()
{
    char out[16] = {0};
    TEST_ASSERT_TRUE(TimeFormatter::format_time(1710000000u, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("160000", out);
}

/**
 * @brief A minimum-size buffer (7) is sufficient.
 */
void test_format_time_minimum_buffer_size()
{
    char out[7];
    TEST_ASSERT_TRUE(TimeFormatter::format_time(45296u, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("123456", out);
}

/**
 * @brief An undersized buffer is rejected.
 */
void test_format_time_buffer_too_small()
{
    char out[6];
    TEST_ASSERT_FALSE(TimeFormatter::format_time(45296u, out, sizeof(out)));
}

/**
 * @brief A null buffer is rejected.
 */
void test_format_time_null_buffer()
{
    TEST_ASSERT_FALSE(TimeFormatter::format_time(45296u, nullptr, 16));
}

// ============================================================
// Test runner
// ============================================================

int main()
{
    UNITY_BEGIN();

    RUN_TEST(test_format_date_epoch);
    RUN_TEST(test_format_date_leap_day);
    RUN_TEST(test_format_date_recent);
    RUN_TEST(test_format_date_minimum_buffer_size);
    RUN_TEST(test_format_date_buffer_too_small);
    RUN_TEST(test_format_date_null_buffer);

    RUN_TEST(test_format_time_epoch_midnight);
    RUN_TEST(test_format_time_known_time);
    RUN_TEST(test_format_time_recent);
    RUN_TEST(test_format_time_minimum_buffer_size);
    RUN_TEST(test_format_time_buffer_too_small);
    RUN_TEST(test_format_time_null_buffer);

    return UNITY_END();
}
