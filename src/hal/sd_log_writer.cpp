#include "hal/sd_log_writer.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

// Out-of-class definition for ODR-use (C++11 constexpr static member)
constexpr char SdLogWriter::LOG_DIR[];

// ============================================================
// Construction
// ============================================================

SdLogWriter::SdLogWriter(SdController& sd_controller,
                         TimeService& time,
                         hal::LogLevel min_level,
                         uint8_t sd_retention_days)
    : m_sd_controller(sd_controller)
    , m_time(time)
    , m_min_level(min_level)
    , m_retention_days(sd_retention_days)
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
    std::lock_guard<std::mutex> lock(m_buffer_mutex);

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
    std::lock_guard<std::mutex> lock(m_buffer_mutex);
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
    if (!result)
    {
        // A failed create may mean the directory already exists; a
        // present directory satisfies the invariant either way.
        result = m_sd_controller.exists_sync(LOG_DIR);
    }
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
    // Compute the cutoff date: daily log files strictly older than
    // (today - sd_retention_days) are expired.
    time_t today = static_cast<time_t>(m_time.unix_now());
    time_t cutoff = today - (static_cast<time_t>(m_retention_days) * 86400);
    struct tm cutoff_utc;
    gmtime_r(&cutoff, &cutoff_utc);

    char cutoff_date[9];
    snprintf(cutoff_date, sizeof(cutoff_date), "%04d%02d%02d",
             cutoff_utc.tm_year + 1900, cutoff_utc.tm_mon + 1, cutoff_utc.tm_mday);

    // Collect expired log files in a single directory scan, then
    // remove them after the scan completes so the directory is not
    // modified while it is being iterated.
    std::vector<std::string> expired;
    m_sd_controller.list_dir_sync(LOG_DIR,
        [this, &cutoff_date, &expired](const char* entry, size_t size, bool is_dir)
        {
            (void)size;
            (void)is_dir;
            // Daily log files are named YYYYMMDD.log; files dated in
            // 1970 derive from an unset wall clock and are exempt
            // from retention so a later set_time cannot expire them.
            if (strlen(entry) == 12 &&
                strncmp(entry + 8, ".log", 4) == 0 &&
                strncmp(entry, "1970", 4) != 0 &&
                strncmp(entry, cutoff_date, 8) < 0)
            {
                char old_path[32];
                snprintf(old_path, sizeof(old_path), "%s/%s",
                         LOG_DIR, entry);
                expired.push_back(old_path);
            }
            return true;
        });

    for (const std::string& path : expired)
    {
        m_sd_controller.remove_sync(path.c_str());
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