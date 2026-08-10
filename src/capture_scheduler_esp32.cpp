#include "capture_scheduler_esp32.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ============================================================
// Construction
// ============================================================

CaptureSchedulerEsp32::CaptureSchedulerEsp32(hal::TimerHal& timer)
    : m_timer(timer)
{
    m_exit_sem = xSemaphoreCreateBinary();
}

// ============================================================
// Start / stop
// ============================================================

bool CaptureSchedulerEsp32::start(uint32_t interval_ms,
                                  std::function<void()> frame_callback)
{
    // Guard against a second task while one is still running (e.g. from
    // a stop() that timed out). Never overwrite the live callback.
    if (m_task.load(std::memory_order_relaxed) != nullptr)
    {
        return false;
    }

    m_callback = frame_callback;
    m_stop_requested.store(false, std::memory_order_relaxed);

    // Configure timer ISR to call on_frame_capture()
    m_timer.init(interval_ms * 1000, [this]() { on_frame_capture(); });

    // Create FreeRTOS capture task pinned to CPU0 (PRO_CPU).
    // Arduino setup()/loop() run on APP_CPU (CPU1); pinning the
    // capture task to PRO_CPU keeps CPU1 free for WiFi, web server,
    // and LED management.
    TaskHandle_t task = nullptr;
    xTaskCreatePinnedToCore(
        capture_task_entry,
        "capture",
        4096,
        this,
        configMAX_PRIORITIES - 1,
        &task,
        PRO_CPU_NUM);

    m_task.store(task, std::memory_order_relaxed);
    m_timer.start();
    return true;
}

void CaptureSchedulerEsp32::stop()
{
    TaskHandle_t task = m_task.load(std::memory_order_relaxed);
    if (task == nullptr)
    {
        return;
    }

    m_stop_requested.store(true, std::memory_order_relaxed);
    m_timer.stop();

    // Wake the task so it can run its final callback and observe the
    // stop flag before exiting.
    xTaskNotifyGive(task);

    // Drain any stale exit signal before waiting (e.g. from a previous
    // stop() that timed out), then join the task with a bounded wait.
    if (m_exit_sem != nullptr)
    {
        xSemaphoreTake(m_exit_sem, 0);
        if (xSemaphoreTake(m_exit_sem,
                           pdMS_TO_TICKS(STOP_JOIN_TIMEOUT_MS)) == pdTRUE)
        {
            // Task has exited; allow a future start()
            m_task.store(nullptr, std::memory_order_relaxed);
        }
        // On timeout the task handle is retained so start() refuses to
        // create a second task; the consumer's fail-safe is that the
        // next frame callback still closes its clip.
    }
    else
    {
        m_task.store(nullptr, std::memory_order_relaxed);
    }
}

// ============================================================
// ISR notification
// ============================================================

void CaptureSchedulerEsp32::on_frame_capture()
{
    // Relaxed load: m_task is only cleared after the task has fully
    // exited (post-join), so the ISR can never act on a dangling handle.
    TaskHandle_t task = m_task.load(std::memory_order_relaxed);
    if (task != nullptr)
    {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        vTaskNotifyGiveFromISR(task, &xHigherPriorityTaskWoken);
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
        if (self->m_stop_requested.load(std::memory_order_relaxed))
        {
            if (self->m_exit_sem != nullptr)
            {
                xSemaphoreGive(self->m_exit_sem);
            }
            vTaskDelete(nullptr);
            return;
        }
    }
}
