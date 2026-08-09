#ifndef MOCK_TIMER_HAL_H
#define MOCK_TIMER_HAL_H

#include "hal/timer_hal.h"
#include <functional>

/**
 * @file mock_timer_hal.h
 * @brief Configurable mock TimerHal for native unit tests.
 *
 * Provides a trigger() method that invokes the registered ISR
 * callback synchronously, enabling deterministic testing of
 * timer-driven logic without hardware interrupts.
 */
class MockTimerHal : public hal::TimerHal
{
    public:
        // ============================================================
        // Configurable return values
        // ============================================================

        /** Value returned by init(). */
        bool init_result{true};

        // ============================================================
        // Call tracking
        // ============================================================

        /** Number of times init() has been called. */
        int init_call_count{0};

        /** Number of times start() has been called. */
        int start_call_count{0};

        /** Number of times stop() has been called. */
        int stop_call_count{0};

        /** The interval_us passed to the most recent init() or set_frequency() call. */
        uint32_t last_interval_us{0};

        /** Whether the timer is currently "started". */
        bool started{false};

        // ============================================================
        // Interface implementation
        // ============================================================

        bool init(uint32_t interval_us, std::function<void()> isr) override
        {
            init_call_count++;
            last_interval_us = interval_us;
            m_isr = isr;
            started = true;
            return init_result;
        }

        void set_frequency(uint32_t interval_us) override
        {
            last_interval_us = interval_us;
        }

        void start() override
        {
            start_call_count++;
            started = true;
        }

        void stop() override
        {
            stop_call_count++;
            started = false;
        }

        /**
         * @brief Simulates a timer interrupt by invoking the registered ISR.
         *
         * Safe to call even if init() was never called (no-op in that case).
         */
        void trigger()
        {
            if (m_isr)
            {
                m_isr();
            }
        }

    private:
        std::function<void()> m_isr;
};

#endif // MOCK_TIMER_HAL_H
