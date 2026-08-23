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

        // ============================================================
        // Interface implementation
        // ============================================================

        void write(const char* line) override
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            write_call_count++;
            last_message = std::string(line);
            messages.push_back(last_message);
        }

        /** Clears all captured state so the mock can be reused between tests. */
        void reset()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            write_call_count = 0;
            last_message.clear();
            messages.clear();
        }

    private:
        /** Serializes capture when logs are written from multiple tasks. */
        std::mutex m_mutex;
};

#endif // MOCK_LOG_BACKEND_H
