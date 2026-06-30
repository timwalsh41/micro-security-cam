#include "hal/time_hal.h"
#include <Arduino.h>

namespace hal
{

millis_t get_millis() 
{
    return millis();
}

void delay_ms(millis_t ms)
{
    delay(ms);
}

void delay_us(millis_t us)
{
    delayMicroseconds(us);
}

} // namespace hal
