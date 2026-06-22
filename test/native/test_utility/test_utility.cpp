#include <unity.h>
#include "utility.h"
#include "Arduino.h"

void test_print_with_timestamp()
{
    // This is harder to verify without capturing stdout, 
    // but we can ensure it doesn't crash.
    set_mock_millis(1234);
    utility::print_with_timestamp("Test message");
}

void setup() 
{
}

void tearDown()
{
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_print_with_timestamp);
    return UNITY_END();
}