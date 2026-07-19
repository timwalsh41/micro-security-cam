#ifndef HAL_WEB_SERVER_HAL_H
#define HAL_WEB_SERVER_HAL_H

#include <cstdint>
#include <cstddef>

/**
 * @file web_server_hal.h
 * @brief Abstract interface for HTTP server hardware operations.
 *
 * Provides a mockable abstraction over the Arduino WebServer library.
 * Concrete ESP32 implementation and native test mock derive from
 * this interface.
 *
 * HTTP method constants match Arduino WebServer values.
 */

namespace hal
{

/** HTTP method identifiers matching Arduino WebServer constants. */
enum class HttpMethod : uint8_t
{
    GET = 0,
    POST = 1,
};

/**
 * @class WebServerHal
 * @brief Abstract interface for HTTP server operations.
 *
 * Provides route registration, request handling, and response
 * sending. Route handlers execute synchronously within the
 * handle_client() call.
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
        virtual bool begin(uint16_t port) = 0;

        /**
         * @brief Registers a route handler.
         * @param uri     URI pattern (e.g. "/status").
         * @param method  HTTP method.
         * @param handler Callback invoked when the route matches.
         */
        virtual void on(const char* uri, HttpMethod method,
                        void (*handler)()) = 0;

        /**
         * @brief Processes pending HTTP requests.
         *
         * Must be called periodically from the main loop.
         */
        virtual void handle_client() = 0;

        /**
         * @brief Stops the HTTP server.
         */
        virtual void stop() = 0;

        // --- Request accessors (valid within handler context) ---

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

        // --- Response senders (valid within handler context) ---

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
         * @brief Sends a response header with content length.
         *
         * Used for streaming responses where the body is sent
         * incrementally via send_content().
         *
         * @param code           HTTP status code.
         * @param content_type   MIME type string.
         * @param content_length Total body length, or 0 for chunked.
         */
        virtual void send_header(int code, const char* content_type,
                                 size_t content_length) = 0;

        /**
         * @brief Sends a chunk of response body.
         * @param data Body bytes.
         * @param len  Length of data.
         */
        virtual void send_content(const uint8_t* data, size_t len) = 0;
};

} // namespace hal

#endif // HAL_WEB_SERVER_HAL_H
