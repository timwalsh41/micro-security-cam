#ifndef LOG_BACKEND_H
#define LOG_BACKEND_H

#include <cstdint>

/**
 * @file log_backend.h
 * @brief Abstract interface for log output backends and log severity levels.
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
 * @brief Severity levels for log messages.
 */
enum class LogLevel : uint8_t
{
    DEBUG,
    INFO,
    WARNING,
    ERROR
};

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
         * @param level The severity level of the log message.
         * @param line  The formatted log line to output. Null-terminated.
         */
        virtual void write(hal::LogLevel level, const char* line) = 0;

        /**
         * @brief Periodic tick for buffered backends.
         *
         * Called from the main loop to flush buffered log data.
         * Backends that do not buffer (e.g. serial) can leave this
         * as the default no-op implementation.
         */
        virtual void tick() {}
};

} // namespace hal

#endif // LOG_BACKEND_H