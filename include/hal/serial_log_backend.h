#ifndef SERIAL_LOG_BACKEND_H
#define SERIAL_LOG_BACKEND_H

#include "log_backend.h"

/**
 * @file serial_log_backend.h
 * @brief LogBackend implementation that outputs to Arduino Serial.
 */

namespace hal
{

/**
 * @class SerialLogBackend
 * @brief Concrete LogBackend that writes log lines to the Arduino Serial port.
 *
 * Delegates to Serial.println() for each formatted log line. Suitable for
 * development and debugging on the embedded target.
 */
class SerialLogBackend : public LogBackend
{
    public:
        /**
         * @brief Constructor
         *
         * Initializes the Arduino Serial port for output.
         */
        SerialLogBackend();

        /**
         * @brief Write a formatted log line to the Serial port.
         *
         * @param line The formatted log line to output. Null-terminated.
         */
        void write(const char* line) override;
};

} // namespace hal

#endif // SERIAL_LOG_BACKEND_H
