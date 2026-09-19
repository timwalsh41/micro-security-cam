#include <unity.h>
#include "time_service.h"
#include <Arduino.h>
#include <cstdlib>
#include <ctime>
#include <atomic>
#include <thread>
#include <utility>

/**
 * @brief elapsed_ms() returns the difference when now is after since.
 */
void test_elapsed_ms_normal_subtraction() 
{
    hal::millis_t since = 100;
    hal::millis_t now = 150;
    
    TEST_ASSERT_EQUAL_UINT32(50, TimeService::elapsed_ms(since, now));
} 
    
/**
 * @brief elapsed_ms() returns 0 when since equals now.
 */
void test_elapsed_ms_zero_elapsed()
{
    hal::millis_t since = 100;
    hal::millis_t now = 100;
    
    TEST_ASSERT_EQUAL_UINT32(0, TimeService::elapsed_ms(since, now));
}

/**
 * @brief elapsed_ms() handles rollover near the millis_t maximum.
 */
void test_elapsed_ms_rollover_near_max()
{
    hal::millis_t since = 0xFFFFFFF0;
    hal::millis_t now = 0x00000010;

    TEST_ASSERT_EQUAL_UINT32(32, TimeService::elapsed_ms(since, now));
}

/**
 * @brief elapsed_ms() handles an exact wrap of the millis_t counter.
 */
void test_elapsed_ms_rollover_exact_wrap()
{
    hal::millis_t since = 0xFFFFFFFF;
    hal::millis_t now = 0x00000000;
    
    TEST_ASSERT_EQUAL_UINT32(1, TimeService::elapsed_ms(since, now));
}

/**
 * @brief delay_ms() advances the clock by the requested duration.
 */
void test_delay_ms_advances_clock()
{
    RealTimeService time;
    set_mock_millis(0);
    time.delay_ms(100);
    TEST_ASSERT_EQUAL_UINT32(100, time.now());
}

/**
 * @brief Multiple delay_ms() calls accumulate on the clock.
 */
void test_delay_ms_multiple_calls_accumulate()
{
    RealTimeService time;
    set_mock_millis(0);
    time.delay_ms(50);
    time.delay_ms(30);
    TEST_ASSERT_EQUAL_UINT32(80, time.now());
}

/**
 * @brief delay_us() truncates sub-millisecond remainders down to ms.
 */
void test_delay_us_truncates_to_ms()
{
    RealTimeService time;
    set_mock_millis(0);
    time.delay_us(1500);
    TEST_ASSERT_EQUAL_UINT32(1, time.now());
}

/**
 * @brief delay_us() under 1ms truncates to zero.
 */
void test_delay_us_sub_ms_truncates_to_zero()
{
    RealTimeService time;
    set_mock_millis(0);
    time.delay_us(999);
    TEST_ASSERT_EQUAL_UINT32(0, time.now());
}

/**
 * @brief delay_us() with an exact millisecond multiple advances correctly.
 */
void test_delay_us_exact_ms()
{
    RealTimeService time;
    set_mock_millis(0);
    time.delay_us(3000);
    TEST_ASSERT_EQUAL_UINT32(3, time.now());
}

/**
 * @brief delay_ms() advances the clock so elapsed_ms() reflects the delay.
 */
void test_delay_ms_does_not_affect_elapsed_ms()
{
    RealTimeService time;
    set_mock_millis(100);
    time.delay_ms(50);
    TEST_ASSERT_EQUAL_UINT32(50, TimeService::elapsed_ms(100, time.now()));
}

/**
 * @brief unix_now() returns the offset time plus elapsed seconds.
 */
void test_unix_now_reflects_offset_and_elapsed()
{
    RealTimeService time;
    set_mock_millis(1000);
    time.set_unix_offset(1600000000, 1000);
    TEST_ASSERT_EQUAL_UINT32(1600000000, time.unix_now());

    set_mock_millis(6000);
    TEST_ASSERT_EQUAL_UINT32(1600000005, time.unix_now());
}

/**
 * @brief unix_now() honors a reference system_ms older than the current
 *        system time (offset sampled earlier than the read).
 */
void test_unix_now_uses_reference_system_ms()
{
    RealTimeService time;
    set_mock_millis(10000);
    time.set_unix_offset(2000000000, 8000);
    TEST_ASSERT_EQUAL_UINT32(2000000002, time.unix_now());
}

/**
 * @brief A second set_unix_offset() replaces the pair coherently.
 *
 * Regression pin for the two-store offset race: a reader between the
 * two prior stores would compute a timestamp skewed by the full
 * system-uptime delta. The packed pair must always read as one
 * consistent (offset, system_ms) sample.
 */
void test_set_unix_offset_replaces_pair_coherently()
{
    RealTimeService time;
    set_mock_millis(1000);
    time.set_unix_offset(1600000000, 1000);

    set_mock_millis(12345678);
    time.set_unix_offset(1600000100, 12345678);
    TEST_ASSERT_EQUAL_UINT32(1600000100, time.unix_now());

    set_mock_millis(12355678);
    TEST_ASSERT_EQUAL_UINT32(1600000110, time.unix_now());
}

/**
 * @brief unix_now() stays correct when the system clock wraps.
 */
void test_unix_now_handles_system_ms_rollover()
{
    RealTimeService time;
    set_mock_millis(0xFFFFFFF0);
    time.set_unix_offset(100, 0xFFFFFFF0);
    TEST_ASSERT_EQUAL_UINT32(100, time.unix_now());

    // +5000ms across the wrap
    set_mock_millis(0x000013C8);
    TEST_ASSERT_EQUAL_UINT32(105, time.unix_now());
}

/**
 * @brief localtime() returns the broken-down time matching the offset.
 *
 * Regression pin for the reentrant conversion change: the returned
 * std::tm must be a fully populated local copy of the current time.
 */
void test_localtime_returns_broken_down_time()
{
    setenv("TZ", "UTC", 1);
    tzset();

    RealTimeService time;
    set_mock_millis(1000);
    time.set_unix_offset(1600000000, 1000);

    std::tm broken_down = time.localtime();
    TEST_ASSERT_EQUAL_INT(120, broken_down.tm_year);   // 2020
    TEST_ASSERT_EQUAL_INT(8, broken_down.tm_mon);      // September
    TEST_ASSERT_EQUAL_INT(13, broken_down.tm_mday);
    TEST_ASSERT_EQUAL_INT(12, broken_down.tm_hour);
    TEST_ASSERT_EQUAL_INT(26, broken_down.tm_min);
    TEST_ASSERT_EQUAL_INT(40, broken_down.tm_sec);
}

/**
 * @brief Concurrent set_unix_offset() writers never expose a torn
 *        (offset, system_ms) pair to unix_now() readers.
 *
 * Stress regression for the two-store offset race: with a fixed mock
 * clock there are exactly two coherent readings (pair A sampled at its
 * own reference ms, pair B at its own). Any other value means a reader
 * observed half of one pair and half of the other.
 */
void test_unix_now_concurrent_with_offset_writers()
{
    setenv("TZ", "UTC", 1);
    tzset();
    set_mock_millis(61000);

    const uint32_t OFFSET_A = 1600000000;
    const uint32_t MS_A = 1000;
    const uint32_t OFFSET_B = 1600000100;
    const uint32_t MS_B = 61000;

    RealTimeService time;
    time.set_unix_offset(OFFSET_A, MS_A);

    std::atomic<bool> stop{false};
    std::atomic<int> torn_reads{0};

    std::thread writer([&]()
    {
        for (int i = 0; i < 200000 && !stop.load(std::memory_order_relaxed); ++i)
        {
            if (i & 1)
            {
                time.set_unix_offset(OFFSET_B, MS_B);
            }
            else
            {
                time.set_unix_offset(OFFSET_A, MS_A);
            }
        }
        stop.store(true, std::memory_order_relaxed);
    });

    std::thread reader([&]()
    {
        while (!stop.load(std::memory_order_relaxed))
        {
            uint32_t t = time.unix_now();
            // Coherent reads only: pair B read at its reference ms, or
            // pair A read at its reference ms (60 s of elapsed mock time).
            if (t != OFFSET_B && t != OFFSET_A + 60)
            {
                torn_reads.fetch_add(1, std::memory_order_relaxed);
                stop.store(true, std::memory_order_relaxed);
            }
        }
    });

    writer.join();
    reader.join();
    TEST_ASSERT_EQUAL_INT(0, torn_reads.load());
}

/**
 * @brief localtime() stays coherent while another task republishes the
 *        offset pair concurrently.
 *
 * Stress regression for the shared static buffer of std::localtime:
 * with a fixed mock clock only two broken-down results are valid
 * (12:27:40 for pair A, 12:28:20 for pair B, both 2020-09-13 UTC).
 * A mixed minute/second pair means the std::tm was torn.
 */
void test_localtime_concurrent_with_offset_writers()
{
    setenv("TZ", "UTC", 1);
    tzset();
    set_mock_millis(61000);

    RealTimeService time;
    time.set_unix_offset(1600000000, 1000);

    std::atomic<bool> stop{false};
    std::atomic<int> torn_calls{0};

    std::thread writer([&]()
    {
        for (int i = 0; i < 200000 && !stop.load(std::memory_order_relaxed); ++i)
        {
            if (i & 1)
            {
                time.set_unix_offset(1600000100, 61000);
            }
            else
            {
                time.set_unix_offset(1600000000, 1000);
            }
        }
        stop.store(true, std::memory_order_relaxed);
    });

    std::thread reader([&]()
    {
        while (!stop.load(std::memory_order_relaxed))
        {
            std::tm t = time.localtime();
            bool valid_a = (t.tm_hour == 12 && t.tm_min == 27 && t.tm_sec == 40);
            bool valid_b = (t.tm_hour == 12 && t.tm_min == 28 && t.tm_sec == 20);
            if (!(valid_a || valid_b))
            {
                torn_calls.fetch_add(1, std::memory_order_relaxed);
                stop.store(true, std::memory_order_relaxed);
            }
        }
    });

    writer.join();
    reader.join();
    TEST_ASSERT_EQUAL_INT(0, torn_calls.load());
}

// ============================================================
// is_time_set() tests
// ============================================================

/**
 * @brief is_time_set() returns false before set_unix_offset() is called.
 */
void test_is_time_set_returns_false_before_set()
{
    RealTimeService time;
    TEST_ASSERT_FALSE(time.is_time_set());
}

/**
 * @brief is_time_set() returns true after set_unix_offset() is called.
 */
void test_is_time_set_returns_true_after_set()
{
    RealTimeService time;
    set_mock_millis(1000);
    time.set_unix_offset(1600000000, 1000);
    TEST_ASSERT_TRUE(time.is_time_set());
}

/**
 * @brief is_time_set() returns true after a second set_unix_offset().
 */
void test_is_time_set_stays_true_after_second_set()
{
    RealTimeService time;
    set_mock_millis(1000);
    time.set_unix_offset(1600000000, 1000);
    set_mock_millis(12345678);
    time.set_unix_offset(1600000100, 12345678);
    TEST_ASSERT_TRUE(time.is_time_set());
}

void setUp()
{
}

void tearDown() 
{
}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_elapsed_ms_normal_subtraction);
    RUN_TEST(test_elapsed_ms_zero_elapsed);
    RUN_TEST(test_elapsed_ms_rollover_near_max);
    RUN_TEST(test_elapsed_ms_rollover_exact_wrap);
    RUN_TEST(test_delay_ms_advances_clock);
    RUN_TEST(test_delay_ms_multiple_calls_accumulate);
    RUN_TEST(test_delay_us_truncates_to_ms);
    RUN_TEST(test_delay_us_sub_ms_truncates_to_zero);
    RUN_TEST(test_delay_us_exact_ms);
    RUN_TEST(test_delay_ms_does_not_affect_elapsed_ms);
    RUN_TEST(test_unix_now_reflects_offset_and_elapsed);
    RUN_TEST(test_unix_now_uses_reference_system_ms);
    RUN_TEST(test_set_unix_offset_replaces_pair_coherently);
    RUN_TEST(test_unix_now_handles_system_ms_rollover);
    RUN_TEST(test_localtime_returns_broken_down_time);
    RUN_TEST(test_unix_now_concurrent_with_offset_writers);
    RUN_TEST(test_localtime_concurrent_with_offset_writers);

    // is_time_set() tests
    RUN_TEST(test_is_time_set_returns_false_before_set);
    RUN_TEST(test_is_time_set_returns_true_after_set);
    RUN_TEST(test_is_time_set_stays_true_after_second_set);

    return UNITY_END();
}
