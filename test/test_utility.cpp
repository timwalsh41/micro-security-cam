#include <unity.h>
#include "utility.h"
#include "Arduino.h"

void test_time_elapsed_ms_normal() {
    set_mock_millis(1000);
    uint32_t since = 500;
    TEST_ASSERT_EQUAL_UINT32(500, utility::time_elapsed_ms(since));
}

void test_time_elapsed_ms_rollover() {
    // Simulate millis rollover: 0xFFFFFFFF -> 0x00000001
    set_mock_millis(1); 
    uint32_t since = 0xFFFFFFF0; // 16ms before rollover
    // Expected: (UINT32_MAX - 0xFFFFFFF0) + 1 = 15 + 1 = 16
    TEST_ASSERT_EQUAL_UINT32(16, utility::time_elapsed_ms(since));
}

void test_print_with_timestamp() {
    // This is harder to verify without capturing stdout, 
    // but we can ensure it doesn't crash.
    set_mock_millis(1234);
    utility::print_with_timestamp("Test message");
}

void setup() {
    // set up stuff here
}

void tearDown() {
    // clean up stuff here
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_time_elapsed_ms_normal);
    RUN_TEST(test_time_elapsed_ms_rollover);
    RUN_TEST(test_print_with_timestamp);
    return UNITY_END();
}
