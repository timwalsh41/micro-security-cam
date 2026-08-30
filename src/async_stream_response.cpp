#include "async_stream_response.h"

#include <cstdio>
#include <utility>

AsyncJpegStreamResponse::AsyncJpegStreamResponse(
    std::shared_ptr<StreamFrameChannel> channel,
    const char* boundary,
    std::function<void()> on_end)
    : m_channel(std::move(channel))
    , m_on_end(std::move(on_end))
{
    _code = 200;
    _contentType = "multipart/x-mixed-replace; boundary=";
    _contentType += (boundary != nullptr ? boundary : "");
    // Never-ending stream: chunked transfer-encoding, no content length.
    _contentLength = 0;
    _sendContentLength = false;
    _chunked = true;
}

AsyncJpegStreamResponse::~AsyncJpegStreamResponse()
{
    if (m_on_end)
    {
        m_on_end();
    }
}

bool AsyncJpegStreamResponse::_sourceValid() const
{
    return true;
}

size_t AsyncJpegStreamResponse::_fillBuffer(uint8_t* buf, size_t maxLen)
{
    if (m_channel == nullptr)
    {
        return 0;
    }

    if (m_channel->has_pending_part())
    {
        const size_t n = m_channel->pull(buf, maxLen);
        // A pending part cannot vanish under the single consumer, but
        // never signal end-of-stream on an unexpected empty pull — the
        // closed check below owns that decision.
        return n > 0 ? n : RESPONSE_TRY_AGAIN;
    }

    if (m_channel->is_closed())
    {
        // Closed and fully drained: complete the response. The
        // framework finishes the chunked stream and closes the
        // connection, then destroys this response (firing on_end).
        return 0;
    }

    // No fresh frame yet: keep the stream open and retry on the next
    // framework poll/ack.
    return RESPONSE_TRY_AGAIN;
}
