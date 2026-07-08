#include "peripheral_manager.h"
#include "pin_arbitrator.h"
#include "hal/gpio_hal.h"
#include "hal/time_hal.h"

using hal::millis_t;

PeripheralManager::PeripheralManager(TimeService &time, hal::SdCardHal &sd_hal)
    : m_time(time)
    , m_sd_hal(sd_hal)
    , m_led(m_time)
    , m_sd(m_time, m_sd_hal)
{
}

void PeripheralManager::init()
{
    hal::set_pin_as_output(hal::LED_PIN);
    m_led.set_mode(LedMode::OFF);
    apply_led_state(false);
}

hal::SdInitResult PeripheralManager::init_sd_card()
{
    return m_sd.init_card();
}

bool PeripheralManager::is_sd_card_initialized() const
{
    return m_sd_hal.is_initialized();
}

void PeripheralManager::tick()
{
    bool led_wants_on = m_led.tick();
    peripherals::SdSignal sd_signal = m_sd.tick();
    bool pin_low = peripherals::PinArbitrator::resolve(led_wants_on, sd_signal);
    apply_led_state(pin_low);
}

void PeripheralManager::turn_led_on()
{
    m_led.set_mode(LedMode::ON);
}

void PeripheralManager::turn_led_off()
{
    m_led.set_mode(LedMode::OFF);
}

void PeripheralManager::pulse_led(millis_t ms)
{
    m_led.set_mode(LedMode::PULSE_ON, ms);
}

void PeripheralManager::blink(millis_t on_ms, millis_t off_ms)
{
    m_led.set_mode(LedMode::BLINK, on_ms, off_ms);
}

SdWriteResult PeripheralManager::write_file(const char *path, const uint8_t *data,
                                             size_t len, millis_t timeout_ms)
{
    m_sd.start_write(path, data, len);

    millis_t start_time = m_time.now();

    while (!m_sd.is_idle())
    {
        if (TimeService::elapsed_ms(start_time, m_time.now()) > timeout_ms)
        {
            return SdWriteResult::TIMEOUT;
        }

        tick();
        m_time.delay_ms(1);
    }

    return m_sd.get_write_result();
}

SdReadResult PeripheralManager::read_file(const char *path, uint8_t *buffer,
                                           size_t buffer_size, size_t *bytes_read,
                                           millis_t timeout_ms)
{
    m_sd.start_read(path, buffer, buffer_size);

    millis_t start_time = m_time.now();

    while (!m_sd.is_idle())
    {
        if (TimeService::elapsed_ms(start_time, m_time.now()) > timeout_ms)
        {
            if (bytes_read != nullptr)
            {
                *bytes_read = 0;
            }
            return SdReadResult::TIMEOUT;
        }

        tick();
        m_time.delay_ms(1);
    }

    if (bytes_read != nullptr)
    {
        *bytes_read = m_sd.get_bytes_read();
    }

    return m_sd.get_read_result();
}

void PeripheralManager::apply_led_state(bool pin_low)
{
    hal::digital_write(hal::LED_PIN, pin_low ? hal::PIN_LOW : hal::PIN_HIGH);
}