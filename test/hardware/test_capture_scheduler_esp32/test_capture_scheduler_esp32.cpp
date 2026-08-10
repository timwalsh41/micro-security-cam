#include <Arduino.h>
#include <unity.h>
#include "capture_scheduler_esp32.h"
#include "hal/timer_hal_impl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <atomic>

/**
 * @file test_capture_scheduler_esp32.cpp
 * @brief Hardware tests for CaptureSchedulerEsp32 (real timer + task).
 *
 * Regression coverage for the synchronous-stop and double-start
 * contracts on real hardware: stop() joins the capture task (running
 * one final callback) before returning, and start() refuses to spawn
 * a second task while one is running.
 */

/** Number of frame callbacks observed. */
static std::atomic<uint32_t> g_callback_count{0};

/**
 * @brief Frame callback registered with the scheduler under test.
 */
static void on_frame_callback()
{
    g_callback_count.fetch_add(1, std::memory_order_relaxed);
}

// ============================================================
// Interval pacing
// ============================================================

/**
 * @brief A 50 ms interval fires ~12 callbacks over 600 ms.
 *
 * True interval pacing is only exercisable on hardware — the native
 * mock scheduler is synchronous.
 */
void test_callbacks_fire_at_interval()
{
    hal::TimerHalImpl timer;
    CaptureSchedulerEsp32 scheduler(timer);

    g_callback_count.store(0, std::memory_order_relaxed);
    TEST_ASSERT_TRUE(scheduler.start(50, on_frame_callback));

    vTaskDelay(pdMS_TO_TICKS(600));
    scheduler.stop();

    uint32_t count = g_callback_count.load(std::memory_order_relaxed);
    TEST_ASSERT_GREATER_OR_EQUAL(6u, count);
    TEST_ASSERT_LESS_OR_EQUAL(18u, count);
}

// ============================================================
// Synchronous stop (issue 2 regression)
// ============================================================

/**
 * @brief stop() runs one final callback and joins the task before returning.
 *
 * Uses a long interval so the timer never fires during the window;
 * the only callback is the final one triggered by stop() itself.
 * start() succeeding immediately after proves the task fully exited
 * before stop() returned (task handle cleared post-join).
 */
void test_stop_runs_final_callback_and_joins()
{
    hal::TimerHalImpl timer;
    CaptureSchedulerEsp32 scheduler(timer);

    g_callback_count.store(0, std::memory_order_relaxed);
    TEST_ASSERT_TRUE(scheduler.start(1000, on_frame_callback));

    vTaskDelay(pdMS_TO_TICKS(100));
    scheduler.stop();

    // Exactly one callback: the final one run during stop()
    TEST_ASSERT_EQUAL_UINT32(1, g_callback_count.load(std::memory_order_relaxed));

    // Task has exited: a fresh start() succeeds
    TEST_ASSERT_TRUE(scheduler.start(1000, on_frame_callback));
    scheduler.stop();
}

// ============================================================
// Double-start protection
// ============================================================

/**
 * @brief A second start() while running returns false and adds no frames.
 *
 * If a second task were spawned at the faster rate the callback count
 * would roughly double; a count consistent with a single 100 ms task
 * proves the guard held.
 */
void test_double_start_rejected()
{
    hal::TimerHalImpl timer;
    CaptureSchedulerEsp32 scheduler(timer);

    g_callback_count.store(0, std::memory_order_relaxed);
    TEST_ASSERT_TRUE(scheduler.start(100, on_frame_callback));
    TEST_ASSERT_FALSE(scheduler.start(20, on_frame_callback));

    vTaskDelay(pdMS_TO_TICKS(500));
    scheduler.stop();

    // ~5 callbacks at 100 ms; a second 20 ms task would give ~30
    uint32_t count = g_callback_count.load(std::memory_order_relaxed);
    TEST_ASSERT_GREATER_OR_EQUAL(3u, count);
    TEST_ASSERT_LESS_OR_EQUAL(12u, count);
}

// ============================================================
// Restart after stop
// ============================================================

/**
 * @brief A scheduler can be restarted after a full stop.
 */
void test_restart_after_stop()
{
    hal::TimerHalImpl timer;
    CaptureSchedulerEsp32 scheduler(timer);

    g_callback_count.store(0, std::memory_order_relaxed);
    TEST_ASSERT_TRUE(scheduler.start(50, on_frame_callback));
    vTaskDelay(pdMS_TO_TICKS(200));
    scheduler.stop();
    uint32_t first_run = g_callback_count.load(std::memory_order_relaxed);
    TEST_ASSERT_GREATER_THAN(0u, first_run);

    g_callback_count.store(0, std::memory_order_relaxed);
    TEST_ASSERT_TRUE(scheduler.start(50, on_frame_callback));
    vTaskDelay(pdMS_TO_TICKS(200));
    scheduler.stop();
    uint32_t second_run = g_callback_count.load(std::memory_order_relaxed);
    TEST_ASSERT_GREATER_THAN(0u, second_run);
}

// ============================================================
// Test runner (Arduino entry point)
// ============================================================

/**
 * @brief Runs all scheduler tests once.
 *
 * @return Unity result code.
 */
int runUnityTests()
{
    UNITY_BEGIN();
    RUN_TEST(test_callbacks_fire_at_interval);
    RUN_TEST(test_stop_runs_final_callback_and_joins);
    RUN_TEST(test_double_start_rejected);
    RUN_TEST(test_restart_after_stop);
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
