#ifndef TIME_SERVICE_H
#define TIME_SERVICE_H

#include "hal/time_hal.h"

#include <atomic>
#include <cstdint>
#include <ctime>

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
         * The (unix_time, system_ms) pair is published as a single
         * atomic 64-bit value so concurrent unix_now() readers never
         * observe a torn pair.
         *
         * @param unix_time The Unix timestamp at the given system time.
         * @param system_ms The system millisecond timestamp corresponding to unix_time.
         */
        virtual void set_unix_offset(uint32_t unix_time, uint32_t system_ms) = 0;

        /**
         * @brief Returns the current Unix timestamp (seconds since epoch).
         */
        virtual uint32_t unix_now() const = 0;

        /**
         * @brief Returns the current time as a broken-down std::tm struct in local time.
         *
         * The time is derived from the Unix offset set via set_unix_offset().
         * Uses the reentrant conversion function, so concurrent callers
         * each get a consistent broken-down time.
         */
        virtual std::tm localtime() const = 0;
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
        std::tm localtime() const override;

    private:
        /** (unix_offset << 32) | system_ms_at_offset, published atomically. */
        std::atomic<uint64_t> m_offset_pair{0};
};

#endif // TIME_SERVICE_H