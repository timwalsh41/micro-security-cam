#ifndef MOCK_TIME_SERVICE_H
#define MOCK_TIME_SERVICE_H

#include "time_service.h"

/**
 * @file mock_time_service.h
 * @brief Configurable mock TimeService for native unit tests.
 */

class MockTimeService : public TimeService
{
    public:
        explicit MockTimeService(hal::time_t initial = 0)
            : m_current(initial)
        {
        }

        hal::time_t now() override
        {
            return m_current;
        }

        /**
         * @brief Advances the mock clock by the given number of milliseconds.
         */
        void advance(hal::time_t ms)
        {
            m_current += ms;
        }

        /**
         * @brief Sets the mock clock to an absolute value.
         */
        void set(hal::time_t ms)
        {
            m_current = ms;
        }

    private:
        hal::time_t m_current;
};

#endif // MOCK_TIME_SERVICE_H
