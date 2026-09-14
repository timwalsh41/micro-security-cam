#include "hal/sd_log_writer.h"
#include <cstdio>
#include <cstring>
#include <ctime>

// Out-of-class definition for ODR-use (C++11 constexpr static member)
constexpr char SdLogWriter::LOG_DIR[];

// ============================================================
// Construction
// ============================================================

SdLogWriter::SdLogWriter(SdController& sd_controller,
                         TimeService& time,
                         hal::LogLevel min_level,
                         uint8_t retention_days)
    : m_sd_controller(sd_controller)
    , m_time(time)
    , m_min_level(min_level)
    , m_retention_days(retention_days)
    , m_buffer_pos(0)
{
    // Initialize the current date to an empty string
    m_current_date[0] = '\0';
    m_log_path[0] = '\0';
}

// ============================================================
// Public interface
// ============================================================

void SdLogWriter::write(hal::LogLevel level, const char* line)
{
    // Filter by minimum severity level
    if (level < m_min_level)
    {
        return;
    }

    // Check if we need to rotate the log file
    check_rotate();

    // Calculate line length including newline
    size_t line_len = strlen(line);
    if (line_len >= MAX_LOG_LINE_LENGTH)
    {
        line_len = MAX_LOG_LINE_LENGTH - 1;
    }

    // Check if we need to flush the buffer before adding this line
    if (m_buffer_pos + line_len + 1 >= BUFFER_SIZE)
    {
        flush_buffer();
    }

    // Append the line to the buffer
    memcpy(m_buffer + m_buffer_pos, line, line_len);
    m_buffer_pos += line_len;
    m_buffer[m_buffer_pos++] = '\n';
}

void SdLogWriter::tick()
{
    flush_buffer();
    check_rotate();
}

const char* SdLogWriter::current_log_path() const
{
    return m_log_path;
}

const char* SdLogWriter::current_date() const
{
    return m_current_date;
}

// ============================================================
// Private helpers
// ============================================================

void SdLogWriter::flush_buffer()
{
    if (m_buffer_pos == 0)
    {
        return;
    }

    write_to_sd(reinterpret_cast<const uint8_t*>(m_buffer), m_buffer_pos);
    m_buffer_pos = 0;
}

bool SdLogWriter::write_to_sd(const uint8_t* data, size_t len)
{
    // Ensure the log directory exists
    if (!ensure_log_dir())
    {
        return false;
    }

    // Write the buffered data to the log file using synchronous I/O
    return m_sd_controller.write_sync(m_log_path, data, len,
                                      hal::FileOpenMode::APPEND);
}

bool SdLogWriter::ensure_log_dir()
{
    if (m_dir_created)
    {
        return true;
    }

    bool result = m_sd_controller.create_dir_sync(LOG_DIR);
    if (result)
    {
        m_dir_created = true;
    }
    return result;
}

void SdLogWriter::check_rotate()
{
    // Get the current date string
    time_t t = static_cast<time_t>(m_time.unix_now());
    struct tm utc;
    gmtime_r(&t, &utc);

    char date_str[9];
    snprintf(date_str, sizeof(date_str), "%04d%02d%02d",
             utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday);

    // Check if the day has changed
    if (strcmp(m_current_date, date_str) == 0)
    {
        return; // Same day, no rotation needed
    }

    // Day has changed: flush any pending data
    flush_buffer();

    // Update the current date and log path
    update_log_path(date_str);

    // Delete old log files based on retention policy
    delete_old_logs();
}

void SdLogWriter::delete_old_logs()
{
    // Compute the cutoff date (today - retention_days)
    time_t today = static_cast<time_t>(m_time.unix_now());
    struct tm today_utc;
    gmtime_r(&today, &today_utc);

    // Calculate cutoff date in YYYYMMDD format
    // We'll enumerate dates back from today and delete files
    // older than retention_days
    for (uint8_t day_offset = m_retention_days + 1; day_offset <= 255; ++day_offset)
    {
        // Calculate the date for this offset
        time_t cutoff = today - (static_cast<time_t>(day_offset) * 86400);
        struct tm cutoff_utc;
        gmtime_r(&cutoff, &cutoff_utc);

        char old_date[9];
        snprintf(old_date, sizeof(old_date), "%04d%02d%02d",
                 cutoff_utc.tm_year + 1900, cutoff_utc.tm_mon + 1, cutoff_utc.tm_mday);

        // Build the path for the old log file
        char old_path[32];
        snprintf(old_path, sizeof(old_path), "%s/%s.log", LOG_DIR, old_date);

        // Try to remove the old log file
        m_sd_controller.remove_sync(old_path);

        // If the date is before the cutoff, stop
        if (day_offset > 254)
        {
            break;
        }
    }
}

void SdLogWriter::update_log_path(const char* date_str)
{
    // Update the current date
    strncpy(m_current_date, date_str, sizeof(m_current_date) - 1);
    m_current_date[8] = '\0';

    // Update the log file path
    snprintf(m_log_path, sizeof(m_log_path), "%s/%s.log", LOG_DIR, m_current_date);
}