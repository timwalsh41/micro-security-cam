#include <unity.h>
#include "time_service.h"
#include <Arduino.h>

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
    
    return UNITY_END();
}
