#ifndef TIME_FORMATTER_H
#define TIME_FORMATTER_H

#include <cstddef>
#include <cstdint>

/**
 * @file time_formatter.h
 * @brief UTC date/time formatting helpers.
 *
 * Provides civil-time formatting of Unix timestamps for file names.
 * Uses gmtime_r() so dates account for leap years and month lengths,
 * with no timezone dependency (all times are UTC).
 */

/**
 * @class TimeFormatter
 * @brief Formats Unix timestamps into YYYYMMDD / HHMMSS strings (UTC).
 *
 * Static helpers used by file-naming code. Safe to call from task
 * context; not safe from ISR context (uses gmtime_r, which may take a
 * lock in newlib).
 */
class TimeFormatter
{
    public:
        /**
         * @brief Formats a Unix timestamp as YYYYMMDD (UTC).
         *
         * @param unix_seconds Unix timestamp (seconds since epoch).
         * @param out          Output buffer.
         * @param size         Size of the output buffer (must be >= 9).
         * @return true if the date was written, false if the buffer was too small.
         */
        static bool format_date(uint32_t unix_seconds, char* out, size_t size);

        /**
         * @brief Formats a Unix timestamp as HHMMSS (UTC).
         *
         * @param unix_seconds Unix timestamp (seconds since epoch).
         * @param out          Output buffer.
         * @param size         Size of the output buffer (must be >= 7).
         * @return true if the time was written, false if the buffer was too small.
         */
        static bool format_time(uint32_t unix_seconds, char* out, size_t size);
};

#endif // TIME_FORMATTER_H
