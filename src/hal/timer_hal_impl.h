#ifndef TIMER_HAL_IMPL_H
#define TIMER_HAL_IMPL_H

#include "hal/timer_hal.h"
#include "driver/timer.h"

/**
 * @file timer_hal_impl.h
 * @brief ESP32-S3 hardware timer implementation.
 *
 * Wraps the ESP-IDF timer_group APIs behind the TimerHal interface.
 * Uses TIMER_GROUP_0 / TIMER_0 with a 1 MHz clock (divider 80)
 * for microsecond-precision periodic interrupts.
 */

namespace hal
{

class TimerHalImpl : public TimerHal
{
    public:
        bool init(uint32_t interval_us, std::function<void()> isr) override;
        void set_frequency(uint32_t interval_us) override;
        void start() override;
        void stop() override;

    private:
        static bool IRAM_ATTR timer_isr_trampoline(void* arg);
        std::function<void()> m_isr;
        bool m_initialized{false};
        bool m_callback_attached{false};
};

} // namespace hal

#endif // TIMER_HAL_IMPL_H
