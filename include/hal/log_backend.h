#ifndef LOG_BACKEND_H
#define LOG_BACKEND_H

/**
 * @file log_backend.h
 * @brief Abstract interface for log output backends.
 *
 * Defines the minimal contract that any log transport must satisfy.
 * Concrete implementations include SerialLogBackend (for serial output)
 * and potential future backends such as SD card file logging or network
 * logging.
 *
 * The LoggingService formats log messages and delegates the final
 * formatted string to an instance of this interface.
 */

namespace hal
{

/**
 * @class LogBackend
 * @brief Abstract interface for log output destinations.
 *
 * Implementations receive a fully formatted log line and are responsible
 * for delivering it to the underlying transport (serial, file, network, etc.).
 */
class LogBackend
{
    public:
        virtual ~LogBackend() = default;

        /**
         * @brief Write a fully formatted log line to the backend.
         *
         * The caller is responsible for formatting the message before
         * calling this method. The backend should write the entire string
         * as a single line (including any transport-specific newline).
         *
         * @param line The formatted log line to output. Null-terminated.
         */
        virtual void write(const char* line) = 0;
};

} // namespace hal

#endif // LOG_BACKEND_H
