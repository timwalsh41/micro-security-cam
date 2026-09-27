#ifndef RESTART_HAL_IMPL_H
#define RESTART_HAL_IMPL_H

#include "hal/restart_hal.h"

/**
 * @file restart_hal_impl.h
 * @brief ESP32 implementation of the deferred restart interface.
 */

namespace hal
{

/**
 * @class RestartHalImpl
 * @brief Restarts the ESP32 via a one-shot esp_timer callback.
 *
 * schedule_restart() arms a one-shot timer that calls esp_restart()
 * from the timer task once the delay elapses, so a request handler can
 * deliver its HTTP response before the device goes down.
 */
class RestartHalImpl : public RestartHal
{
    public:
        bool schedule_restart(millis_t delay_ms) override;
};

} // namespace hal

#endif // RESTART_HAL_IMPL_H
