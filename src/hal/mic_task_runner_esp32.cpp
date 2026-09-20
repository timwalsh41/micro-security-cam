#include "hal/mic_task_runner_esp32.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/**
 * @file mic_task_runner_esp32.cpp
 * @brief ESP32 FreeRTOS implementation of IMicTaskRunner.
 */

bool MicTaskRunnerEsp32::start(std::function<void()> loop_func)
{
    m_loop_func = loop_func;
    m_running = true;

    xTaskCreatePinnedToCore(mic_task_entry, "mic_task", 4096,
                            this, 5, &m_task_handle, 1);
    return m_task_handle != nullptr;
}

void MicTaskRunnerEsp32::stop()
{
    if (m_running && m_task_handle != nullptr)
    {
        m_running = false;
        vTaskDelete(m_task_handle);
        m_task_handle = nullptr;
    }
}

void MicTaskRunnerEsp32::delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void MicTaskRunnerEsp32::mic_task_entry(void* param)
{
    auto* self = static_cast<MicTaskRunnerEsp32*>(param);
    self->m_loop_func();
    self->m_task_handle = nullptr;
    vTaskDelete(nullptr);
}