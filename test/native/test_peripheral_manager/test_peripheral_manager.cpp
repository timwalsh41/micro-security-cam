#include <unity.h>
#include "peripheral_manager.h"
#include "time_service.h"
#include "mock_sd_card_hal.h"
#include <Arduino.h>
#include "hal/time_hal.h"
#include <vector>
#include <utility>

using hal::millis_t;

// ============================================================
// GPIO tracking helpers
// ============================================================

/**
 * @brief Returns true if the given pin was driven to the given value
 *        at any point in the GPIO log.
 */
static bool was_pin_driven_to(uint8_t pin, uint8_t value)
{
    const auto& log = get_gpio_log();
    for (const auto& entry : log)
    {
        if (entry.first == pin && entry.second == value)
        {
            return true;
        }
    }
    return false;
}

/**
 * @brief Returns the most recent value written to the given pin,
 *        or -1 if no writes have been recorded.
 */
static int8_t last_pin_value(uint8_t pin)
{
    const auto& log = get_gpio_log();
    for (int i = static_cast<int>(log.size()) - 1; i >= 0; --i)
    {
        if (log[i].first == pin)
        {
            return static_cast<int8_t>(log[i].second);
        }
    }
    return -1;
}

/**
 * @brief Returns how many times the given pin was driven to the given value.
 */
static int count_pin_driven_to(uint8_t pin, uint8_t value)
{
    int count = 0;
    const auto& log = get_gpio_log();
    for (const auto& entry : log)
    {
        if (entry.first == pin && entry.second == value)
        {
            count++;
        }
    }
    return count;
}

// ============================================================
// init()
// ============================================================

/**
 * @brief init() drives pin HIGH (LED off).
 */
void test_init_sets_pin_high()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    PeripheralManager pm(time, sd_hal);

    clear_gpio_log();
    pm.init();

    TEST_ASSERT_EQUAL(HIGH, last_pin_value(LED_BUILTIN));
}

// ============================================================
// LED convenience methods
// ============================================================

/**
 * @brief turn_led_on() drives pin LOW after tick.
 */
void test_turn_led_on_drives_pin_low()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    PeripheralManager pm(time, sd_hal);
    pm.init();

    pm.turn_led_on();
    clear_gpio_log();
    pm.tick();

    TEST_ASSERT_EQUAL(LOW, last_pin_value(LED_BUILTIN));
}

/**
 * @brief turn_led_off() drives pin HIGH after tick.
 */
void test_turn_led_off_drives_pin_high()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    PeripheralManager pm(time, sd_hal);
    pm.init();
    pm.turn_led_on();

    pm.turn_led_off();
    clear_gpio_log();
    pm.tick();

    TEST_ASSERT_EQUAL(HIGH, last_pin_value(LED_BUILTIN));
}

// ============================================================
// write_file() — basic result propagation
// ============================================================

/**
 * @brief write_file() completes successfully and returns SUCCESS.
 */
void test_write_file_success()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    PeripheralManager pm(time, sd_hal);
    pm.init();

    uint8_t data[] = {0xAA, 0xBB};
    SdWriteResult result = pm.write_file("/test.bin", data, 2, 500);

    TEST_ASSERT_EQUAL(SdWriteResult::SUCCESS, result);
    TEST_ASSERT_EQUAL_INT(1, sd_hal.open_call_count);
    TEST_ASSERT_EQUAL_INT(1, sd_hal.write_call_count);
    TEST_ASSERT_EQUAL_INT(1, sd_hal.close_call_count);
}

/**
 * @brief write_file() returns FILE_OPEN_FAILED when HAL open fails.
 */
void test_write_file_open_failed()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    sd_hal.open_result = false;
    PeripheralManager pm(time, sd_hal);
    pm.init();

    uint8_t data[] = {0x01, 0x02};
    SdWriteResult result = pm.write_file("/bad.bin", data, 2, 500);

    TEST_ASSERT_EQUAL(SdWriteResult::FILE_OPEN_FAILED, result);
    TEST_ASSERT_EQUAL_INT(1, sd_hal.open_call_count);
    TEST_ASSERT_EQUAL_INT(0, sd_hal.write_call_count);
    TEST_ASSERT_EQUAL_INT(1, sd_hal.close_call_count);
}

/**
 * @brief write_file() returns FILE_WRITE_ERROR when HAL write fails.
 */
void test_write_file_write_error()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    sd_hal.write_result = false;
    PeripheralManager pm(time, sd_hal);
    pm.init();

    uint8_t data[] = {0x01, 0x02, 0x03};
    SdWriteResult result = pm.write_file("/err.bin", data, 3, 500);

    TEST_ASSERT_EQUAL(SdWriteResult::FILE_WRITE_ERROR, result);
    TEST_ASSERT_EQUAL_INT(1, sd_hal.write_call_count);
}

/**
 * @brief write_file() returns TIMEOUT when timeout_ms is too short.
 */
void test_write_file_timeout()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    PeripheralManager pm(time, sd_hal);
    pm.init();

    uint8_t data[] = {0x01};
    SdWriteResult result = pm.write_file("/slow.bin", data, 1, 1);

    TEST_ASSERT_EQUAL(SdWriteResult::TIMEOUT, result);
    TEST_ASSERT_EQUAL_INT(0, sd_hal.open_call_count);
}

// ============================================================
// write_file() — pin arbitration during blocking call
// ============================================================

/**
 * @brief During a blocking write with LED off, the pin is driven LOW
 *        for CS_ACTIVE, then returns HIGH after completion.
 *
 * This is the critical test: it verifies that PeripheralManager::tick()
 * (including PinArbitrator::resolve() and apply_led_state()) is called
 * during the blocking write loop. Without this, the pin would never be
 * driven LOW for CS_ACTIVE.
 */
void test_write_file_drives_pin_through_arbitrator()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    PeripheralManager pm(time, sd_hal);
    pm.init();
    pm.turn_led_off();  // LED wants pin HIGH

    clear_gpio_log();
    uint8_t data[] = {0x01};
    pm.write_file("/test.bin", data, 1, 500);

    // SD needs CS_ACTIVE (pin LOW) during ACTIVATE_CS / IO_ACTIVE phases.
    // If PeripheralManager::tick() was not called during the blocking loop,
    // pin would never be driven LOW.
    TEST_ASSERT_TRUE(was_pin_driven_to(LED_BUILTIN, LOW));

    // After completion: LED off + SD idle → pin HIGH
    TEST_ASSERT_EQUAL(HIGH, last_pin_value(LED_BUILTIN));
}

/**
 * @brief During a blocking write with LED on, the pin is driven LOW
 *        for CS_ACTIVE and LOW for LED, but HIGH during CS_INACTIVE phases.
 *
 * Both LED-on and CS_ACTIVE want pin LOW. During CS_INACTIVE phases
 * (PRE_DEACTIVATE_CS, POST_DEACTIVATE_CS), the pin is driven HIGH
 * per the SD timing protocol. After completion, pin returns to LOW
 * (LED on + SD idle).
 */
void test_write_file_led_on_pin_stays_low()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    PeripheralManager pm(time, sd_hal);
    pm.init();
    pm.turn_led_on();  // LED wants pin LOW

    clear_gpio_log();
    uint8_t data[] = {0x01};
    pm.write_file("/test.bin", data, 1, 500);

    // After completion: LED on + SD idle → pin LOW
    TEST_ASSERT_EQUAL(LOW, last_pin_value(LED_BUILTIN));

    // Pin was driven HIGH during CS_INACTIVE phases (timing protocol)
    TEST_ASSERT_TRUE(was_pin_driven_to(LED_BUILTIN, HIGH));

    // Pin was also driven LOW during CS_ACTIVE phases and LED tick
    TEST_ASSERT_TRUE(was_pin_driven_to(LED_BUILTIN, LOW));
}

/**
 * @brief Pin returns to LED-determined state after write completes.
 *
 * Verifies the full lifecycle: pin reflects SD during I/O,
 * then returns to LED state after SD becomes IDLE.
 */
void test_write_file_pin_restores_after_completion()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    PeripheralManager pm(time, sd_hal);
    pm.init();
    pm.turn_led_off();  // LED wants HIGH

    uint8_t data[] = {0x01, 0x02};
    pm.write_file("/test.bin", data, 2, 500);

    // Verify pin ended at the correct state (LED off → HIGH)
    TEST_ASSERT_EQUAL(HIGH, last_pin_value(LED_BUILTIN));
}

// ============================================================
// read_file() — basic result propagation
// ============================================================

/**
 * @brief read_file() completes successfully with correct data.
 */
void test_read_file_success()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    sd_hal.file_content = {0xDE, 0xAD};
    PeripheralManager pm(time, sd_hal);
    pm.init();

    uint8_t buf[4] = {0};
    size_t bytes_read = 0;
    SdReadResult result = pm.read_file("/data.bin", buf, sizeof(buf),
                                        &bytes_read, 500);

    TEST_ASSERT_EQUAL(SdReadResult::SUCCESS, result);
    TEST_ASSERT_EQUAL_INT(2, bytes_read);
    TEST_ASSERT_EQUAL_UINT8(0xDE, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0xAD, buf[1]);
}

/**
 * @brief read_file() returns FILE_OPEN_FAILED when HAL open fails.
 */
void test_read_file_open_failed()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    sd_hal.open_result = false;
    PeripheralManager pm(time, sd_hal);
    pm.init();

    uint8_t buf[4] = {0xFF};
    size_t bytes_read = 99;
    SdReadResult result = pm.read_file("/missing.bin", buf, sizeof(buf),
                                        &bytes_read, 500);

    TEST_ASSERT_EQUAL(SdReadResult::FILE_OPEN_FAILED, result);
    TEST_ASSERT_EQUAL_INT(0, bytes_read);
    TEST_ASSERT_EQUAL_UINT8(0xFF, buf[0]);  // untouched
}

/**
 * @brief read_file() returns TIMEOUT when timeout_ms is too short.
 */
void test_read_file_timeout()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    PeripheralManager pm(time, sd_hal);
    pm.init();

    uint8_t buf[4] = {0};
    size_t bytes_read = 99;
    SdReadResult result = pm.read_file("/slow.bin", buf, sizeof(buf),
                                        &bytes_read, 1);

    TEST_ASSERT_EQUAL(SdReadResult::TIMEOUT, result);
    TEST_ASSERT_EQUAL_INT(0, bytes_read);
}

/**
 * @brief read_file() with nullptr bytes_read still works.
 */
void test_read_file_null_bytes_read()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    sd_hal.file_content = {0x42};
    PeripheralManager pm(time, sd_hal);
    pm.init();

    uint8_t buf[4] = {0};
    SdReadResult result = pm.read_file("/simple.bin", buf, sizeof(buf),
                                        nullptr, 500);

    TEST_ASSERT_EQUAL(SdReadResult::SUCCESS, result);
    TEST_ASSERT_EQUAL_UINT8(0x42, buf[0]);
}

// ============================================================
// read_file() — pin arbitration during blocking call
// ============================================================

/**
 * @brief During a blocking read with LED off, the pin is driven LOW
 *        for CS_ACTIVE, then returns HIGH after completion.
 *
 * Mirrors the write_file arbitration test for the read path.
 */
void test_read_file_drives_pin_through_arbitrator()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    sd_hal.file_content = {0x01};
    PeripheralManager pm(time, sd_hal);
    pm.init();
    pm.turn_led_off();  // LED wants pin HIGH

    clear_gpio_log();
    uint8_t buf[1] = {0};
    size_t bytes_read = 0;
    pm.read_file("/data.bin", buf, sizeof(buf), &bytes_read, 500);

    // SD needs CS_ACTIVE (pin LOW) during I/O phases
    TEST_ASSERT_TRUE(was_pin_driven_to(LED_BUILTIN, LOW));

    // After completion: LED off + SD idle → pin HIGH
    TEST_ASSERT_EQUAL(HIGH, last_pin_value(LED_BUILTIN));
}

// ============================================================
// LED state machine ticks during blocking operation
// ============================================================

/**
 * @brief LED blink pattern advances during a blocking write.
 *
 * Sets LED to BLINK (1ms on, 100ms off), then performs a blocking
 * write that takes several ms. Verifies the LED state machine was
 * not frozen — the blink pattern progressed.
 */
void test_led_ticks_during_blocking_write()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    PeripheralManager pm(time, sd_hal);
    pm.init();

    // Configure blink: 1ms on, 100ms off
    pm.blink(1, 100);

    // Tick once to start the blink (phase on)
    pm.tick();
    TEST_ASSERT_EQUAL(LOW, last_pin_value(LED_BUILTIN));  // on = LOW

    // Advance past the 1ms on-phase so next tick will turn off
    set_mock_millis(2);

    clear_gpio_log();
    uint8_t data[] = {0x01};
    pm.write_file("/test.bin", data, 1, 500);

    // During the write, tick() was called repeatedly with delay_ms(1),
    // so time advanced by ~500ms. The blink cycle is 1ms on + 100ms off
    // = 101ms per cycle. After 500ms, many cycles have elapsed.
    // The LED should have been driven HIGH at some point (off phase).
    TEST_ASSERT_TRUE(was_pin_driven_to(LED_BUILTIN, HIGH));
}

// ============================================================
// SdCard initialization delegation
// ============================================================

/**
 * @brief init_sd_card() delegates to SdCardHal::init().
 */
void test_init_sd_card_delegates_to_hal()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    PeripheralManager pm(time, sd_hal);

    sd_hal.init_result = hal::SdInitResult::SUCCESS;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(hal::SdInitResult::SUCCESS),
        static_cast<int>(pm.init_sd_card()));
    TEST_ASSERT_EQUAL_INT(1, sd_hal.init_call_count);
}

/**
 * @brief is_sd_card_initialized() delegates to SdCardHal.
 */
void test_is_sd_card_initialized_delegates_to_hal()
{
    RealTimeService time;
    set_mock_millis(0);
    MockSdCardHal sd_hal;
    PeripheralManager pm(time, sd_hal);

    sd_hal.initialized_state = true;
    TEST_ASSERT_TRUE(pm.is_sd_card_initialized());

    sd_hal.initialized_state = false;
    TEST_ASSERT_FALSE(pm.is_sd_card_initialized());
}

// ============================================================
// Unity boilerplate
// ============================================================

void setUp() {}
void tearDown() {}

int main(int argc, char **argv)
{
    UNITY_BEGIN();

    // init
    RUN_TEST(test_init_sets_pin_high);

    // LED convenience methods
    RUN_TEST(test_turn_led_on_drives_pin_low);
    RUN_TEST(test_turn_led_off_drives_pin_high);

    // write_file — result propagation
    RUN_TEST(test_write_file_success);
    RUN_TEST(test_write_file_open_failed);
    RUN_TEST(test_write_file_write_error);
    RUN_TEST(test_write_file_timeout);

    // write_file — pin arbitration (the critical gap test)
    RUN_TEST(test_write_file_drives_pin_through_arbitrator);
    RUN_TEST(test_write_file_led_on_pin_stays_low);
    RUN_TEST(test_write_file_pin_restores_after_completion);

    // read_file — result propagation
    RUN_TEST(test_read_file_success);
    RUN_TEST(test_read_file_open_failed);
    RUN_TEST(test_read_file_timeout);
    RUN_TEST(test_read_file_null_bytes_read);

    // read_file — pin arbitration
    RUN_TEST(test_read_file_drives_pin_through_arbitrator);

    // LED state machine ticks during blocking operation
    RUN_TEST(test_led_ticks_during_blocking_write);

    // SD card initialization delegation
    RUN_TEST(test_init_sd_card_delegates_to_hal);
    RUN_TEST(test_is_sd_card_initialized_delegates_to_hal);

    return UNITY_END();
}
