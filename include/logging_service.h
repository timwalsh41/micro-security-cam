#ifndef LOGGING_SERVICE_H
#define LOGGING_SERVICE_H

#include <Arduino.h>
#include "hal/log_backend.h"
#include "time_service.h"
#include <cstdarg>

/**
 * @file logging_service.h
 * @brief Syslog-style logging service with severity levels and subsystem tagging.
 *
 * Provides a structured logging interface with:
 * - Timestamp from TimeService (millisecond resolution)
 * - Subsystem name (e.g. "Camera", "SD", "Main")
 * - Severity level (DEBUG, INFO, WARNING, ERROR)
 * - Printf-style variadic message formatting
 *
 * Formatted output example:
 * @code
 *   [00001234] [Camera] [INFO ] Photo saved successfully
 * @endcode
 *
 * The service delegates formatted output to a pluggable LogBackend
 * (e.g. SerialLogBackend, or a future SD/network backend).
 */

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
 * @class LoggingService
 * @brief Syslog-style logging with configurable backend.
 *
 * Formats log messages with timestamp, subsystem, and severity,
 * then delegates to an injected LogBackend for output.
 */
class LoggingService
{
    public:
        /**
         * @brief Construct a LoggingService with the given backend and time source.
         *
         * @param backend The output backend (SerialLogBackend, etc.).
         * @param time    The time service for timestamps.
         */
        LoggingService(hal::LogBackend& backend, TimeService& time);

        /**
         * @brief Log a message at the given severity level.
         *
         * @param level      Severity level.
         * @param subsystem  Subsystem name (e.g. "Camera", "SD").
         * @param fmt        printf-style format string.
         * @param ...        Format arguments.
         */
        void log(LogLevel level, const char* subsystem, const char* fmt, ...);

        /**
         * @brief Log a message at DEBUG level.
         *
         * @param subsystem  Subsystem name.
         * @param fmt        printf-style format string.
         * @param ...        Format arguments.
         */
        void debug(const char* subsystem, const char* fmt, ...);

        /**
         * @brief Log a message at INFO level.
         *
         * @param subsystem  Subsystem name.
         * @param fmt        printf-style format string.
         * @param ...        Format arguments.
         */
        void info(const char* subsystem, const char* fmt, ...);

        /**
         * @brief Log a message at WARNING level.
         *
         * @param subsystem  Subsystem name.
         * @param fmt        printf-style format string.
         * @param ...        Format arguments.
         */
        void warn(const char* subsystem, const char* fmt, ...);

        /**
         * @brief Log a message at ERROR level.
         *
         * @param subsystem  Subsystem name.
         * @param fmt        printf-style format string.
         * @param ...        Format arguments.
         */
        void error(const char* subsystem, const char* fmt, ...);

        /**
         * @brief Maximum length of a formatted log message (including null terminator).
         */
        static constexpr size_t MAX_LOG_LINE_LENGTH = 256;

    private:
        hal::LogBackend& m_backend;
        TimeService& m_time;

        /**
         * @brief Core logging implementation that accepts a va_list.
         *
         * @param level     Severity level.
         * @param subsystem Subsystem name.
         * @param fmt       printf-style format string.
         * @param args      Pre-initialized va_list.
         */
        void log_impl(LogLevel level, const char* subsystem,
                      const char* fmt, va_list args);

        /**
         * @brief Format a complete log line into the output buffer.
         *
         * @param out       Output string buffer.
         * @param level     Severity level.
         * @param subsystem Subsystem name.
         * @param msg       Pre-formatted message body.
         */
        static void format_line(String& out, LogLevel level,
                                const char* subsystem, const char* msg,
                                hal::millis_t timestamp);

        /**
         * @brief Get the string representation of a log level.
         *
         * @param level Severity level.
         * @return Pointer to a fixed-width level string.
         */
        static const char* level_string(LogLevel level);
};

#endif // LOGGING_SERVICE_H
