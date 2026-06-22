#include <unity.h>
#include "time_service.h"

void test_elapsed_ms_normal_subtraction() 
{
    hal::millis_t since = 100;
    hal::millis_t now = 150;
    
    TEST_ASSERT_EQUAL_UINT32(50, TimeService::elapsed_ms(since, now));
} 
    
void test_elapsed_ms_zero_elapsed()
{
    hal::millis_t since = 100;
    hal::millis_t now = 100;
    
    TEST_ASSERT_EQUAL_UINT32(0, TimeService::elapsed_ms(since, now));
}

void test_elapsed_ms_rollover_near_max()
{
    hal::millis_t since = 0xFFFFFFF0;
    hal::millis_t now = 0x00000010;

    TEST_ASSERT_EQUAL_UINT32(32, TimeService::elapsed_ms(since, now));
}

void test_elapsed_ms_rollover_exact_wrap()
{
    hal::millis_t since = 0xFFFFFFFF;
    hal::millis_t now = 0x00000000;
    
    TEST_ASSERT_EQUAL_UINT32(1, TimeService::elapsed_ms(since, now));
}

void setup()
{
    // set up while unable to run tests in main()
}

void tearDown() 
{
    // clean up after test is completed
}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_elapsed_ms_normal_subtraction);
    RUN_TEST(test_elapsed_ms_zero_elapsed);
    RUN_TEST(test_elapsed_ms_rollover_near_max);
    RUN_TEST(test_elapsed_ms_rollover_exact_wrap);
    
    return UNITY_END();
}
