#ifndef TIMER_HAL_H
#define TIMER_HAL_H

#include <cstdint>
#include <functional>

/**
 * @file timer_hal.h
 * @brief Abstract interface for hardware timer operations.
 *
 * Provides a periodic timer with ISR-based callback. Used by
 * VideoController to drive frame capture at the configured FPS.
 */

namespace hal
{

/**
 * @class TimerHal
 * @brief Abstract interface for a periodic hardware timer.
 *
 * The timer fires at a configurable interval (in microseconds),
 * invoking a registered ISR callback. The ISR runs in interrupt
 * context and must be short (e.g. a FreeRTOS task notification).
 */
class TimerHal
{
    public:
        virtual ~TimerHal() = default;

        /**
         * @brief Initializes and configures the timer.
         *
         * @param interval_us Timer period in microseconds.
         * @param isr         Callback invoked on each timer interrupt.
         * @return true if initialization succeeded.
         */
        virtual bool init(uint32_t interval_us, std::function<void()> isr) = 0;

        /**
         * @brief Changes the timer period without reinitializing.
         *
         * @param interval_us New period in microseconds.
         */
        virtual void set_frequency(uint32_t interval_us) = 0;

        /**
         * @brief Starts the timer (begins firing interrupts).
         */
        virtual void start() = 0;

        /**
         * @brief Stops the timer (pauses interrupt generation).
         */
        virtual void stop() = 0;
};

} // namespace hal

#endif // TIMER_HAL_H
