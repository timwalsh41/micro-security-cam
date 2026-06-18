#include "hal/time_hal.h"
#include <Arduino.h>

namespace hal
{

time_t get_millis() 
{
    return millis();
}

} // namespace hal
