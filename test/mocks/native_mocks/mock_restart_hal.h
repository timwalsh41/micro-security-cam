#ifndef MOCK_RESTART_HAL_H
#define MOCK_RESTART_HAL_H

#include "hal/restart_hal.h"

/**
 * @file mock_restart_hal.h
 * @brief Mock RestartHal for native unit tests.
 *
 * Records scheduled restarts instead of restarting anything, so
 * consumers can be tested without a real device restart.
 */
class MockRestartHal : public hal::RestartHal
{
    public:
        /** Number of times schedule_restart() was called. */
        int schedule_count{0};

        /** Delay passed to the most recent schedule_restart() call. */
        hal::millis_t last_delay_ms{0};

        void schedule_restart(hal::millis_t delay_ms) override
        {
            schedule_count++;
            last_delay_ms = delay_ms;
        }
};

#endif // MOCK_RESTART_HAL_H
