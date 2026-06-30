#include "time_service.h"
#include "hal/time_hal.h"

using hal::millis_t;

millis_t TimeService::elapsed_ms(hal::millis_t since, hal::millis_t now)
{
    // rollover safe for unsigned integer types
    return now - since;
}

millis_t RealTimeService::now()
{
    return hal::get_millis();
}

void RealTimeService::delay_ms(millis_t ms)
{
    hal::delay_ms(ms);
}

void RealTimeService::delay_us(millis_t us)
{
    hal::delay_us(us);
}
