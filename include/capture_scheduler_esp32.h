#ifndef CAPTURE_SCHEDULER_ESP32_H
#define CAPTURE_SCHEDULER_ESP32_H

#include "capture_scheduler.h"
#include "hal/timer_hal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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
         */
        void start(uint32_t interval_ms,
                   std::function<void()> frame_callback) override;

        /**
         * @brief Stops the hardware timer and signals the task to exit.
         *
         * The capture task will wake, observe the stop flag, and
         * delete itself via vTaskDelete().
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
         * stop flag is set.
         *
         * @param pvParameters  Pointer to the CaptureSchedulerEsp32 instance.
         */
        static void capture_task_entry(void* pvParameters);

        hal::TimerHal& m_timer;
        std::function<void()> m_callback;
        TaskHandle_t m_task{nullptr};
        volatile bool m_stop_requested{false};
};

#endif // CAPTURE_SCHEDULER_ESP32_H
