#ifndef HAL_WEB_SERVER_HAL_H
#define HAL_WEB_SERVER_HAL_H

#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>

#include "stream_frame_channel.h"

/**
 * @file web_server_hal.h
 * @brief Abstract interface for HTTP server hardware operations.
 *
 * Provides a mockable abstraction over a third-party web server library.
 * Concrete ESP32 implementation and native test mock derive from
 * this interface.
 *
 * Route handlers receive a RequestRef object through which they
 * inspect the request and send the response. No singleton request
 * accessors are needed — each handler invocation gets its own
 * RequestRef tied to the specific client connection.
 */

namespace hal
{

/** HTTP method identifiers */
enum class HttpMethod : uint8_t
{
    GET = 0,
    POST = 1,
};

/**
 * @brief Callback invoked when a streaming response has ended.
 *
 * Fired exactly once when the transport destroys the response object —
 * on client disconnect or after the response completes — so the owner
 * can reclaim the viewer slot. Runs in the transport's task context.
 */
using StreamEndCallback = std::function<void()>;

/**
 * @class RequestRef
 * @brief Per-request abstraction
 *
 * Created by the WebServerHal implementation in the route callback
 * and passed directly to the handler. Response methods send data
 * through the underlying client connection.
 */
class RequestRef
{
    public:
        virtual ~RequestRef() = default;

        /** @brief Returns the URI of the current request. */
        virtual const char* uri() const = 0;

        /** @brief Returns the HTTP method of the current request. */
        virtual HttpMethod method() const = 0;

        /**
         * @brief Returns the value of a query parameter.
         * @param name Parameter name.
         * @return Parameter value, or empty string if not found.
         */
        virtual const char* arg(const char* name) const = 0;

        /**
         * @brief Returns the value of a request header.
         * @param name Header name.
         * @return Header value, or empty string if not found.
         */
        virtual const char* header(const char* name) const = 0;

        /**
         * @brief Returns the HTTP Basic Auth header value.
         *
         * The returned string includes the "Basic " prefix.
         * Returns empty string if no Authorization header present.
         */
        virtual const char* auth_header() const = 0;

        /**
         * @brief Sends a complete HTTP response.
         * @param code         HTTP status code (200, 401, etc.).
         * @param content_type MIME type string.
         * @param content      Response body.
         */
        virtual void send(int code, const char* content_type,
                          const char* content) = 0;

        /**
         * @brief Sends a complete HTTP response with binary body.
         * @param code         HTTP status code.
         * @param content_type MIME type string.
         * @param data         Response body bytes.
         * @param len          Length of data in bytes.
         */
        virtual void send(int code, const char* content_type,
                          const uint8_t* data, size_t len) = 0;

        /**
         * @brief Attaches a live multipart streaming response to this
         *        request.
         *
         * The response sends the HTTP multipart headers itself, then
         * pulls pre-formatted multipart parts from @p channel as the
         * client acknowledges data (framework flow control), so frame
         * bytes are never truncated by a send-buffer limit. The stream
         * continues until the channel is closed and drained or the
         * client disconnects.
         *
         * Ownership of @p channel is shared: the response holds a
         * reference until it is destroyed. When the transport destroys
         * the response (disconnect or completion), @p on_end fires so
         * the owner can reclaim the viewer slot; the request/response
         * lifecycle itself stays entirely inside the transport, which
         * keeps per-viewer transport objects leak-free.
         *
         * @param channel  Per-viewer frame channel.
         * @param boundary Multipart boundary token (without dashes).
         * @param on_end   Callback fired when the response ends.
         * @return true if the response was attached.
         */
        virtual bool send_stream_response(
            std::shared_ptr<StreamFrameChannel> channel,
            const char* boundary,
            StreamEndCallback on_end) = 0;
};

/**
 * @class WebServerHal
 * @brief Abstract interface for async HTTP server operations.
 *
 * Provides route registration and lifecycle management. Routes
 * are registered with std::function-based handlers that receive
 * a RequestRef for per-request inspection and response.
 */
class WebServerHal
{
    public:
        virtual ~WebServerHal() = default;

        /**
         * @brief Starts the HTTP server on the given port.
         * @param port TCP port number.
         * @return true on success.
         */
        virtual bool init(uint16_t port) = 0;

        /**
         * @brief Registers a route handler.
         *
         * The handler receives a RequestRef tied to the client
         * connection. Lambdas with captures are supported natively
         * — no static trampolines needed.
         *
         * @param uri     URI pattern (e.g. "/status").
         * @param method  HTTP method.
         * @param handler Callback invoked when the route matches.
         */
        virtual void on(const char* uri, HttpMethod method,
                        std::function<void(RequestRef&)> handler) = 0;

        /**
         * @brief Stops the HTTP server and releases resources.
         */
        virtual void stop() = 0;
};

} // namespace hal

#endif // HAL_WEB_SERVER_HAL_H
