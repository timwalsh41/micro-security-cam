#include <unity.h>
#include "led_state_machine.h"
#include <Arduino.h>

// ============================================================
// OFF mode
// ============================================================

/**
 * @brief OFF mode always returns false regardless of time.
 */
void test_mode_off_returns_false_always()
{
    RealTimeService time;
    set_mock_millis(0);
    LedStateMachine led(time);

    led.set_mode(LedMode::OFF);
    TEST_ASSERT_FALSE(led.tick());

    set_mock_millis(millis() + 10000);
    TEST_ASSERT_FALSE(led.tick());
}

// ============================================================
// ON mode
// ============================================================

/**
 * @brief ON mode always returns true regardless of time.
 */
void test_mode_on_returns_true_always()
{
    RealTimeService time;
    set_mock_millis(0);
    LedStateMachine led(time);

    led.set_mode(LedMode::ON);
    TEST_ASSERT_TRUE(led.tick());

    set_mock_millis(millis() + 10000);
    TEST_ASSERT_TRUE(led.tick());
}

// ============================================================
// PULSE_ON mode
// ============================================================

/**
 * @brief PULSE_ON stays true before the on_ms duration has elapsed.
 *        Uses strict-greater-than comparison (matches existing codebase).
 */
void test_pulse_stays_on_before_duration()
{
    RealTimeService time;
    set_mock_millis(100);
    LedStateMachine led(time);

    led.set_mode(LedMode::PULSE_ON, 500);

    // Immediately after set_mode, elapsed = 0, not > 500 → still on
    TEST_ASSERT_TRUE(led.tick());

    // After 250ms, elapsed = 250, not > 500 → still on
    set_mock_millis(millis() + 250);
    TEST_ASSERT_TRUE(led.tick());

    // After another 249ms, total elapsed = 499, not > 500 → still on
    set_mock_millis(millis() + 249);
    TEST_ASSERT_TRUE(led.tick());
}

/**
 * @brief PULSE_ON transitions to OFF once on_ms has been exceeded.
 */
void test_pulse_turns_off_after_duration()
{
    RealTimeService time;
    set_mock_millis(100);
    LedStateMachine led(time);

    led.set_mode(LedMode::PULSE_ON, 500);

    // Advance to 500ms elapsed → not > 500 → still on
    set_mock_millis(600);
    TEST_ASSERT_TRUE(led.tick());

    // Advance to 501ms elapsed → > 500 → off, transitions to OFF
    set_mock_millis(601);
    TEST_ASSERT_FALSE(led.tick());

    // Subsequent ticks remain OFF
    set_mock_millis(millis() + 100);
    TEST_ASSERT_FALSE(led.tick());
}

// ============================================================
// BLINK mode
// ============================================================

/**
 * @brief BLINK alternates on and off at correct intervals
 *        with symmetric on/off durations.
 */
void test_blink_cycles_on_and_off()
{
    RealTimeService time;
    set_mock_millis(0);
    LedStateMachine led(time);

    led.set_mode(LedMode::BLINK, 100, 100);

    // Phase: ON. Immediately → on
    TEST_ASSERT_TRUE(led.tick());

    // 50ms: still in ON phase
    set_mock_millis(50);
    TEST_ASSERT_TRUE(led.tick());

    // 100ms: elapsed == 100, not > 100 → still on
    set_mock_millis(100);
    TEST_ASSERT_TRUE(led.tick());

    // 101ms: elapsed > 100 → transitions to OFF phase, records new baseline
    set_mock_millis(101);
    TEST_ASSERT_FALSE(led.tick());

    // 150ms from start, but 49ms from OFF phase start (101+49=150), not > 100 → off
    set_mock_millis(150);
    TEST_ASSERT_FALSE(led.tick());

    // 201ms from start, 100ms from OFF phase start (101+100=201), not > 100 → off
    set_mock_millis(201);
    TEST_ASSERT_FALSE(led.tick());

    // 202ms from start, 101ms from OFF phase start → > 100 → transitions to ON phase
    set_mock_millis(202);
    TEST_ASSERT_TRUE(led.tick());
}

/**
 * @brief BLINK with asymmetric on/off durations produces the correct pattern.
 */
void test_blink_custom_durations()
{
    RealTimeService time;
    set_mock_millis(0);
    LedStateMachine led(time);

    led.set_mode(LedMode::BLINK, 200, 50);

    // ON phase starts at t=0
    TEST_ASSERT_TRUE(led.tick());

    // 200ms: still on (not > 200)
    set_mock_millis(200);
    TEST_ASSERT_TRUE(led.tick());

    // 201ms: > 200 → transition to OFF, baseline = 201
    set_mock_millis(201);
    TEST_ASSERT_FALSE(led.tick());

    // 250ms (49ms from OFF start): still off (not > 50)
    set_mock_millis(250);
    TEST_ASSERT_FALSE(led.tick());

    // 252ms (51ms from OFF start): > 50 → transition to ON, baseline = 252
    set_mock_millis(252);
    TEST_ASSERT_TRUE(led.tick());

    // 451ms (199ms from ON start): still on (not > 200)
    set_mock_millis(451);
    TEST_ASSERT_TRUE(led.tick());

    // 453ms (201ms from ON start): > 200 → transition to OFF
    set_mock_millis(453);
    TEST_ASSERT_FALSE(led.tick());
}

// ============================================================
// Mode transitions
// ============================================================

/**
 * @brief Switching from ON to OFF immediately takes effect.
 */
void test_mode_switch_on_to_off()
{
    RealTimeService time;
    set_mock_millis(0);
    LedStateMachine led(time);

    led.set_mode(LedMode::ON);
    TEST_ASSERT_TRUE(led.tick());

    led.set_mode(LedMode::OFF);
    TEST_ASSERT_FALSE(led.tick());
}

/**
 * @brief Switching from OFF to ON immediately takes effect.
 */
void test_mode_switch_off_to_on()
{
    RealTimeService time;
    set_mock_millis(0);
    LedStateMachine led(time);

    led.set_mode(LedMode::OFF);
    TEST_ASSERT_FALSE(led.tick());

    led.set_mode(LedMode::ON);
    TEST_ASSERT_TRUE(led.tick());
}

/**
 * @brief Switching from BLINK to OFF stops blinking immediately.
 */
void test_mode_switch_blink_to_off()
{
    RealTimeService time;
    set_mock_millis(0);
    LedStateMachine led(time);

    led.set_mode(LedMode::BLINK, 100, 100);
    TEST_ASSERT_TRUE(led.tick());

    led.set_mode(LedMode::OFF);
    TEST_ASSERT_FALSE(led.tick());
}

// ============================================================
// Unity boilerplate
// ============================================================

void setUp() {}
void tearDown() {}

int main(int argc, char **argv)
{
    UNITY_BEGIN();

    // OFF mode
    RUN_TEST(test_mode_off_returns_false_always);

    // ON mode
    RUN_TEST(test_mode_on_returns_true_always);

    // PULSE_ON mode
    RUN_TEST(test_pulse_stays_on_before_duration);
    RUN_TEST(test_pulse_turns_off_after_duration);

    // BLINK mode
    RUN_TEST(test_blink_cycles_on_and_off);
    RUN_TEST(test_blink_custom_durations);

    // Mode transitions
    RUN_TEST(test_mode_switch_on_to_off);
    RUN_TEST(test_mode_switch_off_to_on);
    RUN_TEST(test_mode_switch_blink_to_off);

    return UNITY_END();
}
