#include "hal/time_hal.h"
#include <Arduino.h>

namespace hal
{

millis_t get_millis() 
{
    return millis();
}

void delay_ms(uint32_t ms)
{
    delay(ms);
}

void delay_us(uint32_t us)
{
    delayMicroseconds(us);
}

} // namespace hal
