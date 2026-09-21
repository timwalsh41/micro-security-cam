#ifndef MIC_TASK_RUNNER_H
#define MIC_TASK_RUNNER_H

#include <functional>
#include <cstdint>

/**
 * @class IMicTaskRunner
 * @brief Abstract interface for mic task lifecycle management.
 *
 * Manages the creation and destruction of a background task that
 * runs the mic capture loop. Platform-specific implementations
 * wrap FreeRTOS tasks (ESP32) or std::thread (native tests).
 */
class IMicTaskRunner
{
public:
    virtual ~IMicTaskRunner() = default;

    /**
     * @brief Starts the mic task.
     *
     * Creates a background task that runs the given loop function.
     * The loop function runs until stop() is called.
     *
     * @param loop_func Function to run in the background task.
     * @return true if the task was started successfully.
     */
    virtual bool start(std::function<void()> loop_func) = 0;

    /**
     * @brief Stops the mic task and waits for it to exit.
     */
    virtual void stop() = 0;

    /**
     * @brief Delays the current task for the given number of milliseconds.
     *
     * @param ms Number of milliseconds to delay.
     */
    virtual void delay_ms(uint32_t ms) = 0;
};

#endif // MIC_TASK_RUNNER_H