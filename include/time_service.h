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
    virtual hal::time_t now() = 0;

    /**
     * @brief Calculates elapsed time between two timestamps, handling rollover correctly.
     * 
     * @param since The starting timestamp in milliseconds.
     * @param now The current timestamp in milliseconds.
     * @return hal::time_t Elapsed time in milliseconds.
     */
    static hal::time_t elapsed_ms(hal::time_t since, hal::time_t now);
};

/**
 * @brief Concrete implementation of TimeService that delegates to the hardware HAL.
 */
class RealTimeService : public TimeService {
public:
    hal::time_t now() override;
};

#endif // TIME_SERVICE_H