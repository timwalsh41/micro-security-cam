#ifndef SERIAL_LOG_BACKEND_H
#define SERIAL_LOG_BACKEND_H

#include "log_backend.h"
#include "hal/serial_hal.h"
#include <cstddef>

/**
 * @file serial_log_backend.h
 * @brief LogBackend implementation that outputs to a SerialHal interface.
 *
 * Delegates write output to an injected SerialHal. The serial port is
 * initialised by the concrete SerialHal constructor.
 */

namespace hal
{

/**
 * @class SerialLogBackend
 * @brief Concrete LogBackend that writes log lines via a SerialHal.
 *
 * Accepts a SerialHal reference for output.
 */
class SerialLogBackend : public LogBackend
{
    public:
        /**
         * @brief Maximum length of a formatted log message (including null terminator).
         *
         * Must match LoggingService::MAX_LOG_LINE_LENGTH
         */
        static constexpr size_t MAX_LOG_LINE_LENGTH = 256;

        /**
         * @brief Constructor
         *
         * @param serial Reference to a SerialHal instance.
         */
        explicit SerialLogBackend(SerialHal& serial);

        /**
         * @brief Write a formatted log line via the SerialHal.
         *
         * @param level The severity level of the log message.
         * @param line  The formatted log line to output. Null-terminated.
         */
        void write(hal::LogLevel level, const char* line) override;

    private:
        SerialHal& m_serial;
};

} // namespace hal

#endif // SERIAL_LOG_BACKEND_H
