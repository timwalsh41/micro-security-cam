#include <Arduino.h>
#include <unity.h>
#include "hal/timer_hal_impl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
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

/** Semaphore signalling ISR callback observations (regression test). */
static SemaphoreHandle_t g_isr_semaphore = nullptr;

/**
 * @brief ISR callback that signals a binary semaphore on each firing.
 */
static void on_timer_isr_sem()
{
    if (g_isr_semaphore != nullptr)
    {
        xSemaphoreGiveFromISR(g_isr_semaphore, nullptr);
    }
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
 * @brief Repeated init/start/stop cycles re-arm the ISR callback every time.
 *
 * Regression test for the use-after-free on the second stop-then-start pair:
 * the ESP-IDF v4.4.7 timer driver frees the interrupt handle in
 * timer_isr_callback_remove() without nulling its internal stored handle,
 * so an unguarded second remove (previously performed by stop() followed
 * by init()'s repeat-teardown) dereferenced freed memory and crashed with
 * a Guru Meditation inside the next init(). The attach-flag guard in
 * TimerHalImpl makes removal idempotent; this test dies in cycle 2's
 * init() without that guard.
 */
void test_repeated_init_start_stop_cycles_rearm_isr()
{
    hal::TimerHalImpl timer;

    g_isr_semaphore = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(g_isr_semaphore);

    for (int cycle = 1; cycle <= 3; ++cycle)
    {
        TEST_ASSERT_TRUE_MESSAGE(timer.init(20000, on_timer_isr_sem),
                                 "init() failed");
        timer.start();

        // The ISR must actually fire after each re-arm (bounded wait).
        TEST_ASSERT_EQUAL_MESSAGE(
            pdTRUE, xSemaphoreTake(g_isr_semaphore, pdMS_TO_TICKS(500)),
            "ISR did not fire after re-arm");

        timer.stop();

        // Drain any token signalled just before stop(), then verify no
        // further callbacks fire once the timer is stopped.
        xSemaphoreTake(g_isr_semaphore, 0);
        TEST_ASSERT_NOT_EQUAL(pdTRUE,
                              xSemaphoreTake(g_isr_semaphore,
                                             pdMS_TO_TICKS(100)));
    }

    vSemaphoreDelete(g_isr_semaphore);
    g_isr_semaphore = nullptr;
}

/**
 * @brief set_frequency() changes the callback rate without reinit.
 *
 * set_frequency() only retargets the alarm value; per the TimerHalImpl
 * attach contract, init() and stop() are the sole toggle points for the
 * ISR callback (stop() removes it, init() re-arms it). The rate change
 * is therefore exercised on a running timer, with no stop() between the
 * two phases — a stop() would detach the callback and start() alone
 * would not dispatch any callbacks until the next init().
 */
void test_set_frequency_changes_period()
{
    hal::TimerHalImpl timer;

    g_isr_count.store(0, std::memory_order_relaxed);
    TEST_ASSERT_TRUE(timer.init(50000, on_timer_isr));
    timer.start();
    vTaskDelay(pdMS_TO_TICKS(200));

    // ~4 callbacks at 50 ms
    uint32_t slow_count = g_isr_count.load(std::memory_order_relaxed);
    TEST_ASSERT_GREATER_OR_EQUAL(2u, slow_count);
    TEST_ASSERT_LESS_OR_EQUAL(8u, slow_count);

    // Speed up to 10 ms while running and measure again. At most one
    // residual 50 ms period elapses before the new alarm value takes
    // effect (the counter reloads at the old alarm before matching the
    // new one), so ~15-20 callbacks fire over the 200 ms window.
    g_isr_count.store(0, std::memory_order_relaxed);
    timer.set_frequency(10000);
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
    RUN_TEST(test_repeated_init_start_stop_cycles_rearm_isr);
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
