#ifndef ASYNC_WEB_STREAM_WRITER_H
#define ASYNC_WEB_STREAM_WRITER_H

#include "hal/stream_writer.h"

#include <cstddef>
#include <cstdint>
#include <functional>

/**
 * @file async_web_stream_writer.h
 * @brief AsyncTCP concrete implementation of hal::StreamWriter.
 *
 * Adapts an AsyncClient* from the AsyncTCP library to the
 * hal::StreamWriter interface. Hardware-only: this header and its
 * implementation are excluded from native unit-test builds.
 */

/** Forward declaration for AsyncClient from the AsyncTCP library. */
class AsyncClient;

/**
 * @class AsyncWebStreamWriter
 * @brief StreamWriter implementation that writes to an AsyncClient.
 *
 * Writes bytes directly to the AsyncTCP client's send queue. The
 * client pointer is owned by the enclosing AsyncWebServerRequest and
 * must not outlive it; close() nulls the reference so later write()
 * calls become safe no-ops.
 *
 * A disconnect callback (registered via on_disconnect()) is invoked
 * when the AsyncClient reports a disconnect. The callback must not
 * delete this writer — the owner (StreamService) does that after
 * freeing the slot.
 */
class AsyncWebStreamWriter : public hal::StreamWriter
{
    public:
        /**
         * @brief Constructs a writer bound to an AsyncClient.
         * @param client AsyncClient to write to (may not be null).
         */
        explicit AsyncWebStreamWriter(AsyncClient* client);

        /**
         * @brief Writes bytes to the AsyncClient send queue.
         *
         * Returns 0 (and writes nothing) if the client is null or the
         * socket cannot currently send.
         */
        size_t write(const uint8_t* data, size_t len) override;

        /**
         * @brief Closes the AsyncClient and nulls the reference.
         */
        void close() override;

        /**
         * @brief Registers a callback invoked on client disconnect.
         *
         * Wired to AsyncClient::onDisconnect(). The callback must not
         * delete this writer.
         */
        void on_disconnect(std::function<void()> cb) override;

    private:
        AsyncClient* m_client;
        std::function<void()> m_disconnect_cb;
};

#endif // ASYNC_WEB_STREAM_WRITER_H
