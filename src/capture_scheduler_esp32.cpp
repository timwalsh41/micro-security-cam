#include "capture_scheduler_esp32.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ============================================================
// Construction
// ============================================================

CaptureSchedulerEsp32::CaptureSchedulerEsp32(hal::TimerHal& timer)
    : m_timer(timer)
{
}

// ============================================================
// Start / stop
// ============================================================

void CaptureSchedulerEsp32::start(uint32_t interval_ms,
                                  std::function<void()> frame_callback)
{
    m_callback = frame_callback;
    m_stop_requested = false;

    // Configure timer ISR to call on_frame_capture()
    m_timer.init(interval_ms * 1000, [this]() { on_frame_capture(); });

    // Create FreeRTOS capture task pinned to CPU0 (PRO_CPU).
    // Arduino setup()/loop() run on APP_CPU (CPU1); pinning the
    // capture task to PRO_CPU keeps CPU1 free for WiFi, web server,
    // and LED management.
    xTaskCreatePinnedToCore(
        capture_task_entry,
        "capture",
        4096,
        this,
        configMAX_PRIORITIES - 1,
        &m_task,
        PRO_CPU_NUM);

    m_timer.start();
}

void CaptureSchedulerEsp32::stop()
{
    m_stop_requested = true;
    m_timer.stop();

    // Wake the task so it can observe the stop flag and exit
    if (m_task != nullptr)
    {
        xTaskNotifyGive(m_task);
    }
}

// ============================================================
// ISR notification
// ============================================================

void CaptureSchedulerEsp32::on_frame_capture()
{
    if (m_task != nullptr)
    {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        vTaskNotifyGiveFromISR(m_task, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken)
        {
            portYIELD_FROM_ISR();
        }
    }
}

// ============================================================
// FreeRTOS task
// ============================================================

void CaptureSchedulerEsp32::capture_task_entry(void* pvParameters)
{
    auto* self = static_cast<CaptureSchedulerEsp32*>(pvParameters);

    for (;;)
    {
        // Block until notified by the timer ISR
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Run the frame callback first — this lets VideoController process
        // any pending clip closure (m_stop_clip) before we check the stop flag.
        if (self->m_callback)
        {
            self->m_callback();
        }

        // Check if stop was requested — exit after the final callback
        if (self->m_stop_requested)
        {
            vTaskDelete(nullptr);
            return;
        }
    }
}
