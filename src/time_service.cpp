#include "time_service.h"
#include "hal/time_hal.h"

using hal::millis_t;

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

std::tm RealTimeService::localtime() const
{
    std::time_t t = static_cast<std::time_t>(unix_now());

    // Note this is not thread-safe, std::localtime(&t) returns a std::tm* — a pointer to a
    // statically allocated internal buffer (shared between std::localtime and std::gmtime).
    // The static buffer that std::localtime writes into is shared with std::gmtime, and a
    // subsequent call to either function will overwrite it. But since we dereference
    // immediately (*std::localtime(...)) and return a copy, the buffer is only held
    // momentarily — no issue here for single-threaded applications. However, on a
    // multitasking system, if another task calls std::localtime concurrently, the data
    // could be overwritten between the pointer return and the copy. In practice the window
    // is tiny, but it's a theoretical non-thread-safety concern with this family of
    // functions. If that ever becomes an issue, the reentrant variant localtime_r (POSIX)
    // or std::localtime in C++ with a mutex would be the fix.
    return *std::localtime(&t);
}
