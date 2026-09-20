#ifndef MOCK_MIC_TASK_RUNNER_H
#define MOCK_MIC_TASK_RUNNER_H

#include "mic_task_runner.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>
#include <condition_variable>

/**
 * @file mock_mic_task_runner.h
 * @brief Native mock of IMicTaskRunner for unit tests.
 *
 * Uses std::thread to simulate FreeRTOS task creation. Provides
 * call tracking for test assertions.
 */

class MockMicTaskRunner : public IMicTaskRunner
{
public:
    MockMicTaskRunner() = default;

    ~MockMicTaskRunner() override
    {
        stop();
    }

    bool start(std::function<void()> loop_func) override
    {
        m_start_call_count++;
        if (!m_start_result)
        {
            return false;
        }

        m_running = true;
        m_thread = std::thread([this, loop_func]()
        {
            m_loop_func = loop_func;
            m_loop_func();
            m_running = false;
        });
        return true;
    }

    void stop() override
    {
        m_stop_call_count++;
        if (!m_running)
        {
            return;
        }

        m_running = false;
        if (m_thread.joinable())
        {
            m_thread.join();
        }
    }

    void delay_ms(uint32_t ms) override
    {
        m_delay_call_count++;
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }

    // ============================================================
    // Call tracking
    // ============================================================

    /** Number of times start() has been called. */
    int m_start_call_count{0};

    /** Number of times stop() has been called. */
    int m_stop_call_count{0};

    /** Number of times delay_ms() has been called. */
    int m_delay_call_count{0};

    /** Value returned by start() (simulates failure). */
    bool m_start_result{true};

    /** Whether the task is currently running. */
    std::atomic<bool> m_running{false};

private:
    std::thread m_thread;
    std::function<void()> m_loop_func;
};

#endif // MOCK_MIC_TASK_RUNNER_H