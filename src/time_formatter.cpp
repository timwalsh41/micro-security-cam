#include "time_formatter.h"
#include <cstdio>
#include <ctime>

// ============================================================
// TimeFormatter
// ============================================================

bool TimeFormatter::format_date(uint32_t unix_seconds, char* out, size_t size)
{
    if (out == nullptr || size < 9)
    {
        return false;
    }

    time_t t = static_cast<time_t>(unix_seconds);
    struct tm utc;
    gmtime_r(&t, &utc);

    snprintf(out, size, "%04d%02d%02d",
             utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday);
    return true;
}

bool TimeFormatter::format_time(uint32_t unix_seconds, char* out, size_t size)
{
    if (out == nullptr || size < 7)
    {
        return false;
    }

    time_t t = static_cast<time_t>(unix_seconds);
    struct tm utc;
    gmtime_r(&t, &utc);

    snprintf(out, size, "%02d%02d%02d",
             utc.tm_hour, utc.tm_min, utc.tm_sec);
    return true;
}
