#include "peripheral_manager.h"
#include "pin_arbitrator.h"
#include "hal/gpio_hal.h"
#include "hal/time_hal.h"

using hal::millis_t;

void PeripheralManager::init()
{
    hal::pin_mode(LED_BUILTIN, OUTPUT);
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
    return m_sd.write_file(path, data, len, timeout_ms);
}

SdReadResult PeripheralManager::read_file(const char *path, uint8_t *buffer,
                                           size_t buffer_size, size_t *bytes_read,
                                           millis_t timeout_ms)
{
    return m_sd.read_file(path, buffer, buffer_size, bytes_read, timeout_ms);
}

void PeripheralManager::apply_led_state(bool pin_low)
{
    hal::digital_write(LED_BUILTIN, pin_low ? LOW : HIGH);
}