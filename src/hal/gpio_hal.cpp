#include "hal/gpio_hal.h"
#include <Arduino.h>

namespace hal
{

void set_pin_as_output(uint8_t pin)
{
    pinMode(pin, OUTPUT);
}

void set_pin_as_input(uint8_t pin)
{
    pinMode(pin, INPUT);
}

void digital_write(uint8_t pin, uint8_t value)
{
    digitalWrite(pin, value);
}

} // namespace hal
