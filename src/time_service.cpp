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
    // Publish the (offset, system-ms) pair as one 64-bit value: two
    // separate stores would let a concurrent unix_now() reader between
    // them compute a timestamp skewed by the full system-uptime delta.
    uint64_t packed =
        (static_cast<uint64_t>(unix_time) << 32) |
        static_cast<uint64_t>(system_ms);
    // Relaxed suffices — the pair is self-contained and no other
    // shared state is ordered against this store.
    m_offset_pair.store(packed, std::memory_order_relaxed);
}

uint32_t RealTimeService::unix_now() const
{
    uint64_t packed = m_offset_pair.load(std::memory_order_relaxed);
    uint32_t unix_offset = static_cast<uint32_t>(packed >> 32);
    uint32_t system_ms = static_cast<uint32_t>(packed & 0xFFFFFFFFULL);
    return unix_offset + (now() - system_ms) / 1000;
}

std::tm RealTimeService::localtime() const
{
    std::time_t t = static_cast<std::time_t>(unix_now());

    // Reentrant variant: std::localtime() returns a pointer to a
    // statically allocated internal buffer, so a concurrent call from
    // another task could overwrite the broken-down time between the
    // call and the copy.
    std::tm out{};
    ::localtime_r(&t, &out);
    return out;
}
