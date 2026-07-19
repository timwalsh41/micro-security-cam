#include "time_service.h"
#include "hal/time_hal.h"

using hal::millis_t;

// ============================================================
// Calendar conversion
// ============================================================

static bool is_leap_year(uint16_t year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

Calendar calendar_from_unix(uint32_t unix_time)
{
    // Days in each month (index 1 = January)
    static const uint8_t DAYS_IN_MONTH[] =
    {
        0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };

    uint32_t remaining = unix_time;

    // Compute time-of-day
    uint8_t second = static_cast<uint8_t>(remaining % 60);
    remaining /= 60;
    uint8_t minute = static_cast<uint8_t>(remaining % 60);
    remaining /= 60;
    uint8_t hour = static_cast<uint8_t>(remaining % 24);
    uint32_t days = remaining / 24;

    // Compute year
    uint16_t year = 1970;
    while (true)
    {
        uint16_t days_in_year = is_leap_year(year) ? 366 : 365;
        if (days < days_in_year)
        {
            break;
        }
        days -= days_in_year;
        year++;
    }

    // Compute month and day
    uint8_t month = 1;
    for (uint8_t m = 1; m <= 12; m++)
    {
        uint8_t dim = DAYS_IN_MONTH[m];
        if (m == 2 && is_leap_year(year))
        {
            dim = 29;
        }
        if (days < dim)
        {
            month = m;
            break;
        }
        days -= dim;
    }

    uint8_t day = static_cast<uint8_t>(days + 1);

    return Calendar{year, month, day, hour, minute, second};
}

// ============================================================
// TimeService
// ============================================================

millis_t TimeService::elapsed_ms(hal::millis_t since, hal::millis_t now)
{
    // rollover safe for unsigned integer types
    return now - since;
}

// ============================================================
// RealTimeService
// ============================================================

millis_t RealTimeService::now() const
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

void RealTimeService::set_unix_offset(uint32_t unix_time, uint32_t system_ms)
{
    m_unix_offset = unix_time;
    m_system_ms_at_offset = system_ms;
}

uint32_t RealTimeService::unix_now() const
{
    return m_unix_offset + (now() - m_system_ms_at_offset) / 1000;
}
