#ifndef RESTART_HAL_H
#define RESTART_HAL_H

#include "hal/time_hal.h"

/**
 * @file restart_hal.h
 * @brief Abstract interface for deferred device restart.
 *
 * Decouples restart scheduling from the platform implementation so
 * consumers can be unit tested with a mock (no real restart occurs).
 */

namespace hal
{

/**
 * @class RestartHal
 * @brief Abstract interface for scheduling a device restart.
 *
 * Implementations must defer the actual restart: the caller may still
 * be inside a request handler that has yet to deliver its response, so
 * the restart fires only after the requested delay has elapsed.
 */
class RestartHal
{
    public:
        virtual ~RestartHal() = default;

        /**
         * @brief Schedules a device restart after a delay.
         *
         * Non-blocking: returns immediately, and the platform restarts
         * once the delay elapses. Multiple calls may stack additional
         * restarts; the first one to fire wins.
         *
         * @param delay_ms Milliseconds to wait before restarting.
         */
        virtual void schedule_restart(millis_t delay_ms) = 0;
};

} // namespace hal

#endif // RESTART_HAL_H
