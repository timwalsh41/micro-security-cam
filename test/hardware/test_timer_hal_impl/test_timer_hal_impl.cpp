#include <Arduino.h>
#include <unity.h>
#include "hal/timer_hal_impl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <atomic>

/**
 * @file test_timer_hal_impl.cpp
 * @brief Hardware tests for hal::TimerHalImpl (real ESP32 timer).
 *
 * Exercises init/start/stop/set_frequency against the physical timer.
 * Callback counts are asserted with generous tolerances because the
 * ISR firing rate is subject to scheduler/timer jitter.
 */

/** Number of ISR callbacks observed. */
static std::atomic<uint32_t> g_isr_count{0};

/**
 * @brief ISR callback registered with the timer under test.
 */
static void on_timer_isr()
{
    g_isr_count.fetch_add(1, std::memory_order_relaxed);
}

// ============================================================
// Timer lifecycle
// ============================================================

/**
 * @brief A 20 ms timer fires ~15 callbacks over 300 ms and stops cleanly.
 */
void test_init_start_stop_fires_periodically()
{
    hal::TimerHalImpl timer;

    g_isr_count.store(0, std::memory_order_relaxed);
    TEST_ASSERT_TRUE(timer.init(20000, on_timer_isr));
    timer.start();

    vTaskDelay(pdMS_TO_TICKS(300));
    timer.stop();

    uint32_t count = g_isr_count.load(std::memory_order_relaxed);
    TEST_ASSERT_GREATER_OR_EQUAL(10u, count);
    TEST_ASSERT_LESS_OR_EQUAL(20u, count);

    // After stop() no further callbacks fire
    g_isr_count.store(0, std::memory_order_relaxed);
    vTaskDelay(pdMS_TO_TICKS(100));
    TEST_ASSERT_EQUAL_UINT32(0, g_isr_count.load(std::memory_order_relaxed));
}

/**
 * @brief set_frequency() changes the callback rate without reinit.
 */
void test_set_frequency_changes_period()
{
    hal::TimerHalImpl timer;

    g_isr_count.store(0, std::memory_order_relaxed);
    TEST_ASSERT_TRUE(timer.init(50000, on_timer_isr));
    timer.start();
    vTaskDelay(pdMS_TO_TICKS(200));
    timer.stop();

    // ~4 callbacks at 50 ms
    uint32_t slow_count = g_isr_count.load(std::memory_order_relaxed);
    TEST_ASSERT_GREATER_OR_EQUAL(2u, slow_count);
    TEST_ASSERT_LESS_OR_EQUAL(8u, slow_count);

    // Speed up to 10 ms and measure again
    g_isr_count.store(0, std::memory_order_relaxed);
    timer.set_frequency(10000);
    timer.start();
    vTaskDelay(pdMS_TO_TICKS(200));
    timer.stop();

    // ~20 callbacks at 10 ms — clearly faster than the 50 ms rate
    uint32_t fast_count = g_isr_count.load(std::memory_order_relaxed);
    TEST_ASSERT_GREATER_OR_EQUAL(12u, fast_count);
    TEST_ASSERT_LESS_OR_EQUAL(30u, fast_count);
    TEST_ASSERT_GREATER_THAN(slow_count, fast_count);
}

// ============================================================
// Test runner (Arduino entry point)
// ============================================================

/**
 * @brief Runs all timer tests once.
 *
 * @return Unity result code.
 */
int runUnityTests()
{
    UNITY_BEGIN();
    RUN_TEST(test_init_start_stop_fires_periodically);
    RUN_TEST(test_set_frequency_changes_period);
    return UNITY_END();
}

/**
 * @brief Arduino entry point; runs the Unity suite.
 */
void setup()
{
    delay(2000);
    runUnityTests();
}

/**
 * @brief Arduino idle loop; unused.
 */
void loop() {}
