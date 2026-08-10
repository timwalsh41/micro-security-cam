#ifndef CAPTURE_SCHEDULER_ESP32_H
#define CAPTURE_SCHEDULER_ESP32_H

#include "capture_scheduler.h"
#include "hal/timer_hal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include <atomic>
#include <functional>

/**
 * @file capture_scheduler_esp32.h
 * @brief ESP32 implementation of ICaptureScheduler.
 *
 * Uses a FreeRTOS task pinned to CPU0 and a hardware timer ISR
 * for periodic frame capture. The timer ISR sends a task
 * notification to wake the capture task, which invokes the
 * registered frame callback.
 *
 * Excluded from native builds — the native test environment
 * uses MockCaptureScheduler instead.
 */

class CaptureSchedulerEsp32 : public ICaptureScheduler
{
    public:
        /**
         * @brief Constructs a CaptureSchedulerEsp32 with a hardware timer.
         *
         * @param timer  Reference to TimerHal for periodic ISR generation.
         */
        explicit CaptureSchedulerEsp32(hal::TimerHal& timer);

        /**
         * @brief Starts the capture task and hardware timer.
         *
         * Creates a FreeRTOS task pinned to CPU0 that blocks on task
         * notifications. Configures the timer ISR to call
         * on_frame_capture() at the given interval.
         *
         * @param interval_ms  Capture interval in milliseconds.
         * @param frame_callback  Function to call on each frame capture.
         * @return true if started, false if the scheduler is already
         *         running (no second task is created).
         */
        bool start(uint32_t interval_ms,
                   std::function<void()> frame_callback) override;

        /**
         * @brief Stops the hardware timer and joins the capture task.
         *
         * The capture task runs one final callback (so the consumer can
         * process pending clip closure), then deletes itself. This
         * method blocks until the task has exited or the join timeout
         * elapses; on timeout the task handle is retained so a later
         * start() fails instead of leaking a second task.
         */
        void stop() override;

        /**
         * @brief ISR-safe notification that a frame is due.
         *
         * Sends a FreeRTOS task notification to wake the capture task.
         * Safe to call from timer interrupt context.
         */
        void on_frame_capture() override;

    private:
        /**
         * @brief FreeRTOS task entry point.
         *
         * Loops forever, blocking on ulTaskNotifyTake(). On each
         * notification, invokes the frame callback. Exits when the
         * stop flag is set, giving the exit semaphore before deleting
         * itself.
         *
         * @param pvParameters  Pointer to the CaptureSchedulerEsp32 instance.
         */
        static void capture_task_entry(void* pvParameters);

        /** Maximum time stop() waits for the capture task to exit (ms). */
        static constexpr uint32_t STOP_JOIN_TIMEOUT_MS = 1000;

        hal::TimerHal& m_timer;
        std::function<void()> m_callback;
        std::atomic<TaskHandle_t> m_task{nullptr};
        std::atomic<bool> m_stop_requested{false};
        SemaphoreHandle_t m_exit_sem{nullptr};
};

#endif // CAPTURE_SCHEDULER_ESP32_H
