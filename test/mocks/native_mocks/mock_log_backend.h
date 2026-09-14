#ifndef MOCK_LOG_BACKEND_H
#define MOCK_LOG_BACKEND_H

#include "hal/log_backend.h"
#include <mutex>
#include <string>
#include <vector>

/**
 * @file mock_log_backend.h
 * @brief Configurable mock LogBackend for native unit tests.
 *
 * Captures formatted log lines for assertion, following the same
 * pattern as MockCameraHal and MockSdCardHal.
 */
class MockLogBackend : public hal::LogBackend
{
    public:
        // ============================================================
        // Call tracking (inspect after calling the method)
        // ============================================================

        /** Number of times write() has been called. */
        int write_call_count{0};

        /** The most recent log line written. */
        std::string last_message;

        /** All messages written, in order. */
        std::vector<std::string> messages;

        /** The severity level of the most recent write call. */
        hal::LogLevel last_level{hal::LogLevel::DEBUG};

        /** All severity levels written, in order. */
        std::vector<hal::LogLevel> levels;

        // ============================================================
        // Interface implementation
        // ============================================================

        void write(hal::LogLevel level, const char* line) override
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            write_call_count++;
            last_level = level;
            last_message = std::string(line);
            messages.push_back(last_message);
            levels.push_back(level);
        }

        /** Clears all captured state so the mock can be reused between tests. */
        void reset()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            write_call_count = 0;
            last_message.clear();
            messages.clear();
            levels.clear();
            last_level = hal::LogLevel::DEBUG;
        }

    private:
        /**
         * @brief Serializes capture when logs are written from multiple
         *        tasks.
         *
         * Mirrors the production boundary: SerialHalImpl::write()
         * serializes each complete log line under a FreeRTOS mutex, so
         * SerialLogBackend itself holds no lock; this mutex protects the
         * mock's own capture state when native concurrency tests drive
         * logging from more than one thread.
         */
        std::mutex m_mutex;
};

#endif // MOCK_LOG_BACKEND_H
