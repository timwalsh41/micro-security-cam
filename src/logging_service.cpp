#include "logging_service.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <ctime>

// Maximum length of the user-formatted message body before being
// incorporated into the full log line.
static constexpr size_t MAX_MSG_LENGTH = LoggingService::MAX_LOG_LINE_LENGTH - 64;

LoggingService::LoggingService(hal::LogBackend& backend, TimeService& time)
    : m_backend(backend)
    , m_time(time)
{
}

void LoggingService::log(LogLevel level, const char* subsystem,
                         const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_impl(level, subsystem, fmt, args);
    va_end(args);
}

void LoggingService::log_impl(LogLevel level, const char* subsystem,
                              const char* fmt, va_list args)
{
    char msg_buf[MAX_MSG_LENGTH];
    vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);

    String line;
    format_line(line, level, subsystem, msg_buf, m_time.now(), m_time.unix_now());
    m_backend.write(level, line.c_str());
}

void LoggingService::debug(const char* subsystem, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_impl(LogLevel::DEBUG, subsystem, fmt, args);
    va_end(args);
}

void LoggingService::info(const char* subsystem, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_impl(LogLevel::INFO, subsystem, fmt, args);
    va_end(args);
}

void LoggingService::warn(const char* subsystem, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_impl(LogLevel::WARNING, subsystem, fmt, args);
    va_end(args);
}

void LoggingService::error(const char* subsystem, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_impl(LogLevel::ERROR, subsystem, fmt, args);
    va_end(args);
}

void LoggingService::format_line(String& out, LogLevel level,
                                 const char* subsystem, const char* msg,
                                 hal::millis_t timestamp,
                                 uint32_t unix_seconds)
{
    // Format wall-clock time as YYYY-MM-DD HH:MM:SS (UTC)
    time_t t = static_cast<time_t>(unix_seconds);
    struct tm utc;
    gmtime_r(&t, &utc);

    char wallclock_buf[21];
    snprintf(wallclock_buf, sizeof(wallclock_buf), "%04d-%02d-%02d %02d:%02d:%02d",
             utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
             utc.tm_hour, utc.tm_min, utc.tm_sec);

    // Build the formatted line: [YYYY-MM-DD HH:MM:SS] [uptime] [subsystem] [LEVEL ] message
    char line_buf[MAX_LOG_LINE_LENGTH];
    snprintf(line_buf, sizeof(line_buf), "[%s] [%08u] [%s] [%s] %s",
             wallclock_buf, timestamp, subsystem, level_string(level), msg);
    out = String(line_buf);
}

const char* LoggingService::level_string(LogLevel level)
{
    switch (level)
    {
        case LogLevel::DEBUG:
            return "DEBUG";
        case LogLevel::INFO:
            return "INFO ";
        case LogLevel::WARNING:
            return "WARN ";
        case LogLevel::ERROR:
            return "ERROR";
        default:
            return "?????";
    }
}
