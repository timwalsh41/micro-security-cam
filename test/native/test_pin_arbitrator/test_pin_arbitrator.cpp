#include <unity.h>
#include "pin_arbitrator.h"

using peripherals::PinArbitrator;
using peripherals::SdSignal;

/**
 * @brief LED off, SD idle → neither wants pin active → HIGH (false).
 */
void test_both_off_idle_returns_false()
{
    TEST_ASSERT_FALSE(PinArbitrator::resolve(false, SdSignal::IDLE));
}

/**
 * @brief LED on, SD idle → LED wins → LOW (true).
 */
void test_led_on_idle_returns_true()
{
    TEST_ASSERT_TRUE(PinArbitrator::resolve(true, SdSignal::IDLE));
}

/**
 * @brief LED off, SD CS_ACTIVE → SD overrides → LOW (true).
 */
void test_cs_active_overrides_led_off()
{
    TEST_ASSERT_TRUE(PinArbitrator::resolve(false, SdSignal::CS_ACTIVE));
}

/**
 * @brief LED on, SD CS_ACTIVE → SD overrides → LOW (true).
 */
void test_cs_active_overrides_led_on()
{
    TEST_ASSERT_TRUE(PinArbitrator::resolve(true, SdSignal::CS_ACTIVE));
}

/**
 * @brief LED off, SD CS_INACTIVE → SD overrides → HIGH (false).
 */
void test_cs_inactive_overrides_led_off()
{
    TEST_ASSERT_FALSE(PinArbitrator::resolve(false, SdSignal::CS_INACTIVE));
}

/**
 * @brief LED on, SD CS_INACTIVE → SD overrides → HIGH (false).
 */
void test_cs_inactive_overrides_led_on()
{
    TEST_ASSERT_FALSE(PinArbitrator::resolve(true, SdSignal::CS_INACTIVE));
}

/**
 * @brief LED on, SD CS_ACTIVE → both want pin active → LOW (true).
 */
void test_both_active_returns_true()
{
    TEST_ASSERT_TRUE(PinArbitrator::resolve(true, SdSignal::CS_ACTIVE));
}

void setup()
{
    // No setup required for pure logic tests.
}

void tearDown()
{
    // No cleanup required for pure logic tests.
}

int main(int argc, char **argv)
{
    UNITY_BEGIN();

    // IDLE state tests (LED wins)
    RUN_TEST(test_both_off_idle_returns_false);
    RUN_TEST(test_led_on_idle_returns_true);

    // CS_ACTIVE tests (SD overrides to LOW)
    RUN_TEST(test_cs_active_overrides_led_off);
    RUN_TEST(test_cs_active_overrides_led_on);

    // CS_INACTIVE tests (SD overrides to HIGH)
    RUN_TEST(test_cs_inactive_overrides_led_off);
    RUN_TEST(test_cs_inactive_overrides_led_on);

    // Combined active test
    RUN_TEST(test_both_active_returns_true);

    return UNITY_END();
}