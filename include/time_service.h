#ifndef TIME_SERVICE_H
#define TIME_SERVICE_H

#include "hal/time_hal.h"

#include <cstdint>

/**
 * @brief Calendar date/time fields derived from a Unix timestamp.
 */
struct Calendar
{
    uint16_t year;
    uint8_t month;  ///< 1-12
    uint8_t day;    ///< 1-31
    uint8_t hour;   ///< 0-23
    uint8_t minute; ///< 0-59
    uint8_t second; ///< 0-59
};

/**
 * @brief Converts a Unix timestamp to calendar date/time fields.
 *
 * @param unix_time Seconds since 1970-01-01 00:00:00 UTC.
 * @return Calendar populated with year, month, day, hour, minute, second.
 */
Calendar calendar_from_unix(uint32_t unix_time);

/**
 * @brief Interface for time acquisition and elapsed-time calculations.
 */
class TimeService
{
    public:
        virtual ~TimeService() = default;

        /**
         * @brief Returns current time in milliseconds.
         */
        virtual hal::millis_t now() const = 0;

        /**
         * @brief Blocks execution for the given number of milliseconds.
         */
        virtual void delay_ms(hal::millis_t ms) = 0;

        /**
         * @brief Blocks execution for the given number of microseconds.
         */
        virtual void delay_us(hal::millis_t us) = 0;

        /**
         * @brief Calculates elapsed time between two timestamps, handling rollover correctly.
         * 
         * @param since The starting timestamp in milliseconds.
         * @param now The current timestamp in milliseconds.
         * @return hal::millis_t Elapsed time in milliseconds.
         */
        static hal::millis_t elapsed_ms(hal::millis_t since, hal::millis_t now);

        /**
         * @brief Sets the Unix time offset using a known reference point.
         *
         * @param unix_time The Unix timestamp at the given system time.
         * @param system_ms The system millisecond timestamp corresponding to unix_time.
         */
        virtual void set_unix_offset(uint32_t unix_time, uint32_t system_ms) = 0;

        /**
         * @brief Returns the current Unix timestamp (seconds since epoch).
         */
        virtual uint32_t unix_now() const = 0;
};

/**
 * @brief Concrete implementation of TimeService that delegates to the hardware HAL.
 */
class RealTimeService : public TimeService
{
    public:
        hal::millis_t now() const override;
        void delay_ms(hal::millis_t ms) override;
        void delay_us(hal::millis_t us) override;
        void set_unix_offset(uint32_t unix_time, uint32_t system_ms) override;
        uint32_t unix_now() const override;

    private:
        uint32_t m_unix_offset{0};
        uint32_t m_system_ms_at_offset{0};
};

#endif // TIME_SERVICE_H