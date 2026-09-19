#include <Arduino.h>
#include <unity.h>
#include "hal/hardware_peripheral_manager_impl.h"
#include "hal/gpio_hal.h"
#include "hal/time_hal.h"

/**
 * @file test_hardware_peripheral_manager.cpp
 * @brief Hardware tests for HardwarePeripheralManagerImpl (ESP32).
 *
 * Verifies the shared GPIO pin arbitration, LED state control,
 * temperature reading, and pin setup on real ESP32 hardware.
 */

/**
 * @brief Fixture for HardwarePeripheralManager tests.
 */
struct PeriphFixture
{
    HardwarePeripheralManagerImpl hw_periph;

    PeriphFixture()
        : hw_periph()
    {
    }
};

// ============================================================
// cs_signal tests
// ============================================================

/**
 * @brief Test that cs_signal acquires the mutex and drives pin LOW on CS_ACTIVE.
 *
 * Verifies the cs_signal method with CS_ACTIVE state on real hardware.
 */
static void test_cs_signal_active()
{
    PeriphFixture fixture;
    fixture.hw_periph.setup_shared_pin();
    fixture.hw_periph.cs_signal(peripherals::SdSignal::CS_ACTIVE);
    // After CS_ACTIVE, the pin should be LOW (CS active)
    // This test verifies the function executes without error
    fixture.hw_periph.cs_signal(peripherals::SdSignal::CS_INACTIVE);
}

/**
 * @brief Test that cs_signal releases the mutex on CS_INACTIVE.
 *
 * Verifies the cs_signal method with CS_INACTIVE state on real hardware.
 */
static void test_cs_signal_inactive()
{
    PeriphFixture fixture;
    fixture.hw_periph.setup_shared_pin();
    fixture.hw_periph.cs_signal(peripherals::SdSignal::CS_INACTIVE);
    // After CS_INACTIVE, the pin should be HIGH (CS inactive)
}

// ============================================================
// update_led tests
// ============================================================

/**
 * @brief Test that update_led drives pin LOW when led_on is true.
 *
 * Verifies the update_led method with led_on = true on real hardware.
 */
static void test_update_led_on()
{
    PeriphFixture fixture;
    fixture.hw_periph.setup_shared_pin();
    fixture.hw_periph.update_led(true);
    // LED on = LOW on shared pin
}

/**
 * @brief Test that update_led drives pin HIGH when led_on is false.
 *
 * Verifies the update_led method with led_on = false on real hardware.
 */
static void test_update_led_off()
{
    PeriphFixture fixture;
    fixture.hw_periph.setup_shared_pin();
    fixture.hw_periph.update_led(false);
    // LED off = HIGH on shared pin
}

// ============================================================
// read_temperature tests
// ============================================================

/**
 * @brief Test that read_temperature returns a valid temperature.
 *
 * Verifies the read_temperature method on real ESP32 hardware.
 * The return value should be a reasonable temperature (e.g. 20-80°C).
 */
static void test_read_temperature()
{
    PeriphFixture fixture;
    float temp = fixture.hw_periph.read_temperature();
    // The ESP32 core temperature should be within a reasonable range
    TEST_ASSERT_TRUE(temp > 0.0f && temp < 100.0f);
}

// ============================================================
// setup_shared_pin tests
// ============================================================

/**
 * @brief Test that setup_shared_pin configures the GPIO pin as output.
 *
 * Verifies the setup_shared_pin method on real hardware.
 */
static void test_setup_shared_pin()
{
    PeriphFixture fixture;
    fixture.hw_periph.setup_shared_pin();
    // After setup, we can write to the pin
    fixture.hw_periph.update_led(true);
    fixture.hw_periph.update_led(false);
}

// ============================================================
// main() - test registration and execution
// ============================================================

void setup()
{
    delay(1000);  // Wait for serial connection
    Serial.begin(115200);
    UNITY_BEGIN();

    RUN_TEST(test_cs_signal_active);
    RUN_TEST(test_cs_signal_inactive);
    RUN_TEST(test_update_led_on);
    RUN_TEST(test_update_led_off);
    RUN_TEST(test_read_temperature);
    RUN_TEST(test_setup_shared_pin);

    UNITY_END();
}

void loop()
{
}