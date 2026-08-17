#ifndef HAL_WEB_SERVER_HAL_H
#define HAL_WEB_SERVER_HAL_H

#include <cstdint>
#include <cstddef>
#include <functional>

#include "hal/stream_writer.h"

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
         * @brief Creates a stream writer for this connection.
         *
         * Returns nullptr if streaming is not supported.
         *
         * @return Pointer to a StreamWriter, or nullptr.
         */
        virtual StreamWriter* create_stream_writer() = 0;
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
