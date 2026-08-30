#ifndef ASYNC_STREAM_RESPONSE_H
#define ASYNC_STREAM_RESPONSE_H

#include "stream_frame_channel.h"

#include <ESPAsyncWebServer.h>

#include <functional>
#include <memory>

/**
 * @file async_stream_response.h
 * @brief ESPAsyncWebServer live multipart streaming response.
 *
 * Hardware-only: this header and its implementation are excluded from
 * native unit-test builds.
 */

/**
 * @class AsyncJpegStreamResponse
 * @brief Chunked multipart/x-mixed-replace response fed by a
 *        StreamFrameChannel.
 *
 * The response is attached to the request with send(): the transport
 * then drives it entirely — it writes the HTTP response headers, pulls
 * pre-formatted multipart parts from the channel as the client
 * acknowledges data (framework flow control; no raw client writes and
 * no per-write send-buffer truncation), and finishes the response when
 * the channel is closed and drained or the client disconnects. The
 * transport's own request/client lifecycle objects are never touched,
 * so nothing leaks per viewer.
 *
 * When the transport destroys this response — on disconnect or after
 * completion — the on_end callback fires exactly once so the owner can
 * reclaim the viewer slot.
 */
class AsyncJpegStreamResponse : public AsyncAbstractResponse
{
    public:
        /**
         * @brief Constructs the streaming response.
         *
         * @param channel  Per-viewer frame channel (shared with the
         *                 stream service slot).
         * @param boundary Multipart boundary token (without dashes).
         * @param on_end   Callback fired on response destruction.
         */
        AsyncJpegStreamResponse(
            std::shared_ptr<StreamFrameChannel> channel,
            const char* boundary,
            std::function<void()> on_end);

        /**
         * @brief Destroys the response and fires the end callback.
         */
        ~AsyncJpegStreamResponse() override;

        bool _sourceValid() const override;

        /**
         * @brief Fills the framework's buffer from the channel.
         *
         * Returns RESPONSE_TRY_AGAIN while no part is pending (the
         * framework keeps the connection open and retries on the next
         * poll/ack) and 0 once the channel is closed and drained, which
         * completes the response and closes the connection.
         */
        size_t _fillBuffer(uint8_t* buf, size_t maxLen) override;

    private:
        std::shared_ptr<StreamFrameChannel> m_channel;
        std::function<void()> m_on_end;
};

#endif // ASYNC_STREAM_RESPONSE_H
