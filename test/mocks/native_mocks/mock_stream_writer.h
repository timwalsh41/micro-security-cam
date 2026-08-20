#ifndef MOCK_STREAM_WRITER_H
#define MOCK_STREAM_WRITER_H

#include "hal/stream_writer.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

/**
 * @file mock_stream_writer.h
 * @brief Configurable mock hal::StreamWriter for native unit tests.
 *
 * Captures all written bytes into a single string and tracks
 * close/write call counts, so StreamService behavior can be verified
 * deterministically on the host machine.
 */
class MockStreamWriter : public hal::StreamWriter
{
    public:
        /** Accumulated bytes written via write(). */
        std::string captured;

        /** Number of write() calls made. */
        int write_call_count{0};

        /** Number of close() calls made. */
        int close_call_count{0};

        /**
         * Optional external counter incremented on close(). Lets tests
         * observe close() after the writer has been deleted by its owner.
         */
        int* close_counter{nullptr};

        /** Value returned by write(); set negative to drop writes. */
        bool accept_writes{true};

        /**
         * @brief Captures bytes into `captured`.
         *
         * When accept_writes is false, returns 0 without capturing.
         */
        size_t write(const uint8_t* data, size_t len) override
        {
            write_call_count++;
            if (!accept_writes || data == nullptr)
            {
                return 0;
            }
            captured.append(reinterpret_cast<const char*>(data), len);
            return len;
        }

        /**
         * @brief Records a close() call.
         */
        void close() override
        {
            close_call_count++;
            if (close_counter != nullptr)
            {
                (*close_counter)++;
            }
        }

        /**
         * @brief Stores the disconnect callback.
         */
        void on_disconnect(std::function<void()> cb) override
        {
            m_disconnect_cb = std::move(cb);
        }

        /**
         * @brief Simulates a client disconnect by invoking the callback.
         */
        void simulate_disconnect()
        {
            if (m_disconnect_cb)
            {
                m_disconnect_cb();
            }
        }

    private:
        std::function<void()> m_disconnect_cb;
};

#endif // MOCK_STREAM_WRITER_H
