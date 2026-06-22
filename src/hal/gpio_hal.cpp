#include "hal/gpio_hal.h"
#include <Arduino.h>

namespace hal
{

void pin_mode(uint8_t pin, uint8_t mode)
{
    pinMode(pin, mode);
}

void digital_write(uint8_t pin, uint8_t value)
{
    digitalWrite(pin, value);
}

} // namespace hal
