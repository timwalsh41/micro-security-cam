#include <Arduino.h>
#include <unity.h>
#include "capture_scheduler_esp32.h"
#include "hal/timer_hal_impl.h"
#include "hal/timer_hal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <atomic>
#include <functional>

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
// Join-timeout branch (wedge recovery)
// ============================================================

/** Duration the slow callback blocks, past the 1000 ms join window. */
static constexpr uint32_t SLOW_BLOCK_MS = 1300;

/** Binary semaphore the slow callback blocks on (created per-test). */
static SemaphoreHandle_t g_slow_sem{nullptr};

/** Number of times the slow frame callback has been entered. */
static std::atomic<uint32_t> g_slow_callback_count{0};

/** Set true once the slow callback has returned (task may still exit). */
static std::atomic<bool> g_slow_callback_done{false};

/**
 * @brief Frame callback that blocks past the stop() join timeout.
 *
 * Blocks on a semaphore take for SLOW_BLOCK_MS. A task blocked on a
 * semaphore is NOT woken by stop()'s xTaskNotifyGive (task notifications
 * and semaphore events are independent RTOS mechanisms), so the capture
 * task stays alive past the 1000 ms bounded join — deterministically
 * forcing the timeout branch that the on-device wedge hits.
 */
static void on_slow_frame_callback()
{
    g_slow_callback_count.fetch_add(1, std::memory_order_relaxed);
    g_slow_callback_done.store(false, std::memory_order_relaxed);
    xSemaphoreTake(g_slow_sem, pdMS_TO_TICKS(SLOW_BLOCK_MS));
    g_slow_callback_done.store(true, std::memory_order_relaxed);
}

// ============================================================
// Timer mock (failure injection)
// ============================================================

/**
 * @brief Configurable hal::TimerHal mock for failure-path tests.
 *
 * Records init/start/stop calls and optionally fails init(), which is
 * the injectable seam for the scheduler's timer collaborator. The ISR
 * callback is stored but never fires on its own; tests invoke it
 * manually to model timer activations.
 */
class MockTimerHal : public hal::TimerHal
{
    public:
        bool init_result{true};
        int init_call_count{0};
        int start_call_count{0};
        int stop_call_count{0};
        std::function<void()> isr;

        bool init(uint32_t interval_us,
                  std::function<void()> callback) override
        {
            (void)interval_us;
            init_call_count++;
            if (!init_result)
            {
                return false;
            }
            isr = callback;
            return true;
        }

        void set_frequency(uint32_t interval_us) override
        {
            (void)interval_us;
        }

        void start() override
        {
            start_call_count++;
        }

        void stop() override
        {
            stop_call_count++;
        }

        /**
         * @brief Fires the registered ISR callback once (models a timer tick).
         */
        void fire_isr()
        {
            if (isr)
            {
                isr();
            }
        }
};

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
// Join-timeout recovery (wedge contract)
// ============================================================

/**
 * @brief start() succeeds after a stop() whose join timed out.
 *
 * A frame callback that blocks past the 1000 ms join window
 * (STOP_JOIN_TIMEOUT_MS) forces stop()'s bounded xSemaphoreTake to
 * expire while the capture task is still alive — the timeout branch
 * exercised by the on-device wedge. stop() clears m_task on timeout,
 * so the start() guard no longer refuses and a fresh start() 
 * succeeds; without the clear, m_task is retained and start()
 * returns false, causing a wedge.
 *
 * The slow callback is given time to return and the stale task to
 * self-exit (m_stop_requested is still true) before the restart, so the
 * fresh start() never races a live double-task.
 */
void test_start_succeeds_after_join_timeout()
{
    hal::TimerHalImpl timer;
    CaptureSchedulerEsp32 scheduler(timer);

    g_slow_sem = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(g_slow_sem);

    g_slow_callback_count.store(0, std::memory_order_relaxed);
    g_slow_callback_done.store(false, std::memory_order_relaxed);

    TEST_ASSERT_TRUE(scheduler.start(50, on_slow_frame_callback));
    // Let the 50 ms timer fire and the task enter the blocking callback.
    vTaskDelay(pdMS_TO_TICKS(120));
    TEST_ASSERT_EQUAL_UINT32(1, g_slow_callback_count.load(std::memory_order_relaxed));

    scheduler.stop();
    // The task was still inside the slow callback when stop() returned,
    // proving the join timed out (timeout branch), not a clean exit.
    TEST_ASSERT_FALSE(g_slow_callback_done.load(std::memory_order_relaxed));

    // Wait for the stale task's callback to return and the task to
    // self-delete (stop_requested is still true here) before restarting.
    uint32_t waited = 0;
    while (!g_slow_callback_done.load(std::memory_order_relaxed) && waited < 2000)
    {
        vTaskDelay(pdMS_TO_TICKS(20));
        waited += 20;
    }
    TEST_ASSERT_TRUE(g_slow_callback_done.load(std::memory_order_relaxed));
    vTaskDelay(pdMS_TO_TICKS(50));

    // Core wedge-recovery assertion: m_task was cleared on timeout, so
    // start() is not refused and the pipeline is usable again.
    g_callback_count.store(0, std::memory_order_relaxed);
    TEST_ASSERT_TRUE(scheduler.start(50, on_frame_callback));
    scheduler.stop();

    vSemaphoreDelete(g_slow_sem);
    g_slow_sem = nullptr;
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
// Timer-init failure propagation
// ============================================================

/**
 * @brief A timer init failure propagates as a start failure.
 *
 * start() must return false without creating a capture task or starting
 * the timer — otherwise the task would block forever on a never-firing
 * notification while the caller reports success. Also proves the failure
 * leaves no wedged state: after the timer recovers, a fresh start()
 * succeeds and ticks.
 */
void test_start_fails_when_timer_init_fails()
{
    MockTimerHal timer;
    CaptureSchedulerEsp32 scheduler(timer);

    timer.init_result = false;
    g_callback_count.store(0, std::memory_order_relaxed);
    TEST_ASSERT_FALSE(scheduler.start(50, on_frame_callback));

    // The timer was configured but never started and no task was
    // created: scheduler stop() is a no-op and does not touch the timer.
    TEST_ASSERT_EQUAL_INT(1, timer.init_call_count);
    TEST_ASSERT_EQUAL_INT(0, timer.start_call_count);
    scheduler.stop();
    TEST_ASSERT_EQUAL_INT(0, timer.stop_call_count);

    // Recovery: once the timer initializes again, start succeeds and
    // a manual ISR fire runs the frame callback.
    timer.init_result = true;
    TEST_ASSERT_TRUE(scheduler.start(50, on_frame_callback));
    timer.fire_isr();
    vTaskDelay(pdMS_TO_TICKS(100));
    scheduler.stop();
    TEST_ASSERT_GREATER_OR_EQUAL(1u,
        g_callback_count.load(std::memory_order_relaxed));
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
    RUN_TEST(test_start_succeeds_after_join_timeout);
    RUN_TEST(test_double_start_rejected);
    RUN_TEST(test_restart_after_stop);
    RUN_TEST(test_start_fails_when_timer_init_fails);
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
