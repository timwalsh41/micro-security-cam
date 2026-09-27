#include "hal/restart_hal_impl.h"
#include <esp_timer.h>
#include <esp_system.h>

namespace hal
{

namespace
{

/**
 * @brief esp_timer callback that restarts the device.
 */
void on_reboot_timer(void* /* arg */)
{
    esp_restart();
}

} // namespace

void RestartHalImpl::schedule_restart(millis_t delay_ms)
{
    esp_timer_create_args_t timer_args = {};
    timer_args.callback = &on_reboot_timer;
    timer_args.name = "reboot";

    esp_timer_handle_t timer_handle = nullptr;
    if (esp_timer_create(&timer_args, &timer_handle) != ESP_OK)
    {
        return;
    }

    // One-shot fire time is in microseconds. The timer handle is
    // intentionally not deleted: the device restarts when it fires.
    esp_timer_start_once(timer_handle,
                         static_cast<uint64_t>(delay_ms) * 1000ULL);
}

} // namespace hal
