#ifndef SD_LOG_WRITER_H
#define SD_LOG_WRITER_H

#include "hal/log_backend.h"
#include "sd_controller.h"
#include "time_service.h"
#include <cstddef>
#include <cstdint>
#include <mutex>

/**
 * @file sd_log_writer.h
 * @brief LogBackend implementation that writes log lines to an SD card file.
 *
 * Buffers log lines in internal DRAM and flushes them to the SD card
 * on each tick(). Supports daily log file rotation at UTC midnight
 * and configurable retention of old log files.
 *
 * Log files are stored at `/logs/YYYYMMDD.log`.
 */

/**
 * @class SdLogWriter
 * @brief Buffered log writer for SD card storage.
 *
 * Implements hal::LogBackend to receive formatted log lines,
 * buffers them in internal DRAM, and flushes to SD card on tick().
 * Supports daily log file rotation and configurable retention.
 *
 * Thread safety: write() may be called from several task contexts
 * while tick() flushes from the main loop; an internal mutex
 * serializes the buffer and rotation state and is held across the
 * buffered flush I/O.
 */
class SdLogWriter : public hal::LogBackend
{
    public:
        /**
         * @brief Maximum length of a formatted log line (including null terminator).
         */
        static constexpr size_t MAX_LOG_LINE_LENGTH = 256;

        /**
         * @brief Size of the internal write buffer in bytes.
         */
        static constexpr size_t BUFFER_SIZE = 4096;

        /**
         * @brief Log directory path on the SD card.
         */
        static constexpr char LOG_DIR[] = "/logs";

        /**
         * @brief Constructs an SdLogWriter with the given dependencies.
         *
         * @param sd_controller  Reference to SdController for SD I/O.
         * @param time           Reference to TimeService for timestamps.
         * @param min_level      Minimum log severity level to write.
         * @param retention_days Number of days to retain old log files (default: 7).
         */
        explicit SdLogWriter(SdController& sd_controller,
                             TimeService& time,
                             hal::LogLevel min_level = hal::LogLevel::INFO,
                             uint8_t retention_days = 7);

        /**
         * @brief Write a formatted log line to the internal buffer.
         *
         * If the log level is below the minimum level, the line is discarded.
         * If the buffer is full, it is flushed to SD.
         *
         * @param level The severity level of the log message.
         * @param line  The formatted log line to output. Null-terminated.
         */
        void write(hal::LogLevel level, const char* line) override;

        /**
         * @brief Periodic tick to flush buffered data to SD and handle rotation.
         *
         * Called from the main loop. Flushes the internal buffer to SD
         * and checks for day rollover to start a new log file and
         * delete old log files based on retention policy.
         */
        void tick() override;

        /**
         * @brief Returns the current log file path (YYYYMMDD.log).
         *
         * @return Pointer to the current log file path buffer.
         */
        const char* current_log_path() const;

        /**
         * @brief Returns the current date string (YYYYMMDD).
         *
         * @return Pointer to the current date string buffer.
         */
        const char* current_date() const;

    private:
        /**
         * @brief Current log file path buffer (e.g., "/logs/YYYYMMDD.log").
         */
        char m_log_path[32];

        /**
         * @brief Current date string (YYYYMMDD).
         */
        char m_current_date[9];

        SdController& m_sd_controller;
        TimeService& m_time;
        hal::LogLevel m_min_level;
        uint8_t m_retention_days;

        char m_buffer[BUFFER_SIZE];
        size_t m_buffer_pos;

        /**
         * @brief Serializes write() against the flushing tick().
         */
        mutable std::mutex m_buffer_mutex;

        /**
         * @brief Flag indicating whether the log directory has been created.
         */
        bool m_dir_created{false};

        /**
         * @brief Flag indicating whether the log file has been opened/initialized.
         */
        bool m_file_initialized{false};

        /**
         * @brief Flush the internal buffer to the SD card.
         *
         * Writes the buffered data to the current log file and resets
         * the buffer position.
         */
        void flush_buffer();

        /**
         * @brief Write data to the SD card log file.
         *
         * @param data Pointer to the data buffer to write.
         * @param len  Number of bytes to write.
         * @return true if write succeeded, false otherwise.
         */
        bool write_to_sd(const uint8_t* data, size_t len);

        /**
         * @brief Ensure the log directory exists on the SD card.
         *
         * @return true if the directory exists or was created successfully.
         */
        bool ensure_log_dir();

        /**
         * @brief Check if the day has changed and rotate the log file.
         *
         * Compares the current date with the stored date. If different,
         * flushes the buffer, starts a new log file, and deletes old
         * log files based on retention policy.
         */
        void check_rotate();

        /**
         * @brief Delete old log files based on retention policy.
         *
         * Scans the log directory once and removes daily log files
         * strictly older than (today - retention_days). Files dated
         * in 1970 derive from an unset wall clock and are never
         * expired.
         */
        void delete_old_logs();

        /**
         * @brief Update the log file path based on the current date.
         *
         * @param date_str The YYYYMMDD date string.
         */
        void update_log_path(const char* date_str);
};

#endif // SD_LOG_WRITER_H