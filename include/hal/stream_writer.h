#ifndef HAL_STREAM_WRITER_H
#define HAL_STREAM_WRITER_H

#include <cstddef>
#include <cstdint>
#include <functional>

/**
 * @file stream_writer.h
 * @brief Abstract write sink for streaming data to a client connection.
 *
 * Provides a minimal, mockable abstraction over a network connection
 * write sink. The ESP32 concrete implementation adapts `AsyncClient*`
 * from the AsyncTCP library; native tests use a mock that captures
 * written bytes.
 */

namespace hal
{

/**
 * @class StreamWriter
 * @brief Abstract sink for streaming bytes to a client.
 *
 * Implementations write bytes to an underlying transport (TCP client,
 * in-memory buffer for tests, etc.) and close the connection when done.
 * Callers should not assume write() is thread-safe; ownership of the
 * concrete object is held by the creator.
 */
class StreamWriter
{
    public:
        virtual ~StreamWriter() = default;

        /**
         * @brief Writes bytes to the underlying sink.
         *
         * @param data Pointer to the bytes to write.
         * @param len  Number of bytes to write.
         * @return Number of bytes accepted by the sink (may be less than
         *         @p len, or 0 if the sink is unavailable/closed).
         */
        virtual size_t write(const uint8_t* data, size_t len) = 0;

        /**
         * @brief Closes the underlying connection.
         *
         * Safe to call more than once. After close(), subsequent write()
         * calls should return 0.
         */
        virtual void close() = 0;

        /**
         * @brief Registers a callback invoked when the connection drops.
         *
         * The callback fires when the underlying transport disconnects
         * (e.g. the client closed the socket). The owner uses it to free
         * the slot and delete the writer. May be called from an async
         * context; implementations must not delete the writer from within
         * the callback.
         *
         * @param cb Callback to invoke on disconnect (may be empty).
         */
        virtual void on_disconnect(std::function<void()> cb) = 0;
};

} // namespace hal

#endif // HAL_STREAM_WRITER_H
