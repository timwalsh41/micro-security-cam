#include "time_service.h"
#include "hal/time_hal.h"

hal::time_t TimeService::elapsed_ms(hal::time_t since, hal::time_t now)
{
    // rollover safe for unsigned integer types
    return now - since;
}

hal::time_t RealTimeService::now()
{
    return hal::get_millis();
}
