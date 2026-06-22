#include "time_service.h"
#include "hal/time_hal.h"

hal::millis_t TimeService::elapsed_ms(hal::millis_t since, hal::millis_t now)
{
    // rollover safe for unsigned integer types
    return now - since;
}

hal::millis_t RealTimeService::now()
{
    return hal::get_millis();
}
