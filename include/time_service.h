#ifndef TIME_SERVICE_H
#define TIME_SERVICE_H

#include "hal/time_hal.h"

/**
 * @brief Interface for time acquisition and elapsed-time calculations.
 */
class TimeService {
public:
    virtual ~TimeService() = default;

    /**
     * @brief Returns current time in milliseconds.
     */
    virtual hal::millis_t now() = 0;

    /**
     * @brief Blocks execution for the given number of milliseconds.
     */
    virtual void delay_ms(uint32_t ms) = 0;

    /**
     * @brief Blocks execution for the given number of microseconds.
     */
    virtual void delay_us(uint32_t us) = 0;

    /**
     * @brief Calculates elapsed time between two timestamps, handling rollover correctly.
     * 
     * @param since The starting timestamp in milliseconds.
     * @param now The current timestamp in milliseconds.
     * @return hal::millis_t Elapsed time in milliseconds.
     */
    static hal::millis_t elapsed_ms(hal::millis_t since, hal::millis_t now);
};

/**
 * @brief Concrete implementation of TimeService that delegates to the hardware HAL.
 */
class RealTimeService : public TimeService {
    public:
        hal::millis_t now() override;
        void delay_ms(uint32_t ms) override;
        void delay_us(uint32_t us) override;
};

#endif // TIME_SERVICE_H