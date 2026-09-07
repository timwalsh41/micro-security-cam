#include "hal/timer_hal_impl.h"

#include <Arduino.h>

namespace hal
{

bool TimerHalImpl::init(uint32_t interval_us, std::function<void()> isr)
{
    m_isr = isr;

    // Tear down any prior start-cycle configuration before reconfiguring.
    // The ISR callback remove is guarded by the attach flag: the ESP-IDF
    // v4.4.7 driver frees the interrupt handle in timer_isr_callback_remove()
    // but leaves its internal stored handle field dangling, so a second
    // unguarded remove would free an already-freed handle (use-after-free).
    if (m_initialized)
    {
        if (m_callback_attached)
        {
            timer_isr_callback_remove(TIMER_GROUP_0, TIMER_0);
            m_callback_attached = false;
        }
        timer_deinit(TIMER_GROUP_0, TIMER_0);
        m_initialized = false;
    }

    timer_config_t config{};
    config.divider = 80;               // 80 MHz / 80 = 1 MHz
    config.counter_dir = TIMER_COUNT_UP;
    config.auto_reload = TIMER_AUTORELOAD_EN;
    config.alarm_en = TIMER_ALARM_EN;
    config.counter_en = TIMER_PAUSE;

    esp_err_t err = timer_init(TIMER_GROUP_0, TIMER_0, &config);
    if (err != ESP_OK)
    {
        return false;
    }

    timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0);
    timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, interval_us);
    timer_set_alarm(TIMER_GROUP_0, TIMER_0, TIMER_ALARM_EN);

    err = timer_isr_callback_add(TIMER_GROUP_0, TIMER_0,
                                  timer_isr_trampoline, this, 0);
    if (err != ESP_OK)
    {
        return false;
    }
    m_callback_attached = true;

    m_initialized = true;
    return true;
}

void TimerHalImpl::set_frequency(uint32_t interval_us)
{
    if (!m_initialized)
    {
        return;
    }
    timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, interval_us);
}

void TimerHalImpl::start()
{
    if (!m_initialized)
    {
        return;
    }
    timer_start(TIMER_GROUP_0, TIMER_0);
}

void TimerHalImpl::stop()
{
    if (!m_initialized)
    {
        return;
    }
    timer_pause(TIMER_GROUP_0, TIMER_0);

    // Remove the ISR callback along with pausing the counter: pairs
    // with init()'s timer_isr_callback_add() and guarantees no delayed
    // alarm dispatch can invoke the callback after stop() returns.
    // The attach-flag guard makes this idempotent — the ESP-IDF v4.4.7
    // driver leaves a dangling internal handle after the first remove,
    // so a second remove would use freed memory.
    if (m_callback_attached)
    {
        timer_isr_callback_remove(TIMER_GROUP_0, TIMER_0);
        m_callback_attached = false;
    }
    else
    {
        Serial.printf("[%08lu] [TMRL] [WARN] ISR callback remove skipped: "
                      "no callback attached\r\n",
                      static_cast<unsigned long>(millis()));
    }
}

bool IRAM_ATTR TimerHalImpl::timer_isr_trampoline(void* arg)
{
    auto* self = static_cast<TimerHalImpl*>(arg);
    if (self && self->m_isr)
    {
        self->m_isr();
    }
    return false;
}

} // namespace hal
