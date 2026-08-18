#include "async_web_stream_writer.h"

#include <AsyncTCP.h>

AsyncWebStreamWriter::AsyncWebStreamWriter(AsyncClient* client)
    : m_client(client)
{
}

size_t AsyncWebStreamWriter::write(const uint8_t* data, size_t len)
{
    if (m_client == nullptr || data == nullptr || len == 0)
    {
        return 0;
    }

    // canSend() is false when the socket has no pending ack capacity or
    // is no longer connected — treat that as "not writable" rather than
    // writing into a closed connection.
    if (!m_client->canSend())
    {
        return 0;
    }

    return m_client->write(reinterpret_cast<const char*>(data), len);
}

void AsyncWebStreamWriter::close()
{
    if (m_client != nullptr)
    {
        m_client->close();
        m_client = nullptr;
    }
}

void AsyncWebStreamWriter::on_disconnect(std::function<void()> cb)
{
    m_disconnect_cb = std::move(cb);

    if (m_client != nullptr && m_disconnect_cb)
    {
        // AsyncClient::onDisconnect takes a callback + opaque arg. The
        // callback must not delete this writer (the owner does that).
        m_client->onDisconnect(
            [](void* arg, AsyncClient* /*client*/)
            {
                auto* self = static_cast<AsyncWebStreamWriter*>(arg);
                if (self->m_disconnect_cb)
                {
                    self->m_disconnect_cb();
                }
            },
            this);
    }
}
