#ifndef HAL_MIC_TASK_RUNNER_ESP32_H
#define HAL_MIC_TASK_RUNNER_ESP32_H

#include "mic_task_runner.h"

/**
 * @class MicTaskRunnerEsp32
 * @brief ESP32 FreeRTOS implementation of IMicTaskRunner.
 *
 * Creates a FreeRTOS task pinned to CPU1 for the mic capture loop.
 * The task is deleted via vTaskDelete when stop() is called.
 */
class MicTaskRunnerEsp32 : public IMicTaskRunner
{
public:
    bool start(std::function<void()> loop_func) override;
    void stop() override;
    void delay_ms(uint32_t ms) override;

private:
    static void mic_task_entry(void* param);

    std::function<void()> m_loop_func;
    TaskHandle_t m_task_handle{nullptr};
    volatile bool m_running{false};
};

#endif // HAL_MIC_TASK_RUNNER_ESP32_H