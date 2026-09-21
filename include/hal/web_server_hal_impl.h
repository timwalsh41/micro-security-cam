#ifndef HAL_WEB_SERVER_HAL_IMPL_H
#define HAL_WEB_SERVER_HAL_IMPL_H

#include "hal/web_server_hal.h"

#include <functional>
#include <string>
#include <vector>

/**
 * @file web_server_hal_impl.h
 * @brief ESP32 concrete implementation of WebServerHal using AsyncWebServer.
 *
 * Wraps the ESPAsyncWebServer library behind the abstract WebServerHal
 * interface. Route handlers use std::function with captures.
 */

/** Forward declaration for AsyncWebServer and AsyncWebServerRequest */
class AsyncWebServer;
class AsyncWebServerRequest;

namespace hal
{

/**
 * @class AsyncRequestRef
 * @brief RequestRef implementation wrapping AsyncWebServerRequest.
 *
 * Created on the stack in the route callback and passed directly
 * to the handler. All string accessors copy to internal buffers
 * since AsyncWebServerRequest may return String temporaries.
 */
class AsyncRequestRef : public RequestRef
{
    public:
        explicit AsyncRequestRef(AsyncWebServerRequest* request);

        const char* uri() const override;

        /**
         * @brief Returns the HTTP method of the request.
         *
         * Only GET and POST are mapped. Any other method (PUT, DELETE,
         * HEAD, etc.) is reported as GET. This is acceptable because only
         * GET and POST routes are registered, but callers should not rely
         * on method() to distinguish non-GET/POST methods.
         */
        HttpMethod method() const override;
        const char* arg(const char* name) const override;
        const char* form_value(const char* name) const override;
        const char* header(const char* name) const override;
        const char* auth_header() const override;

        void send(int code, const char* content_type,
                  const char* content) override;
        void send(int code, const char* content_type,
                  const uint8_t* data, size_t len) override;

        void send_unauthorized() override;

        bool send_stream_response(
            std::shared_ptr<StreamFrameChannel> channel,
            const char* boundary,
            StreamEndCallback on_end) override;

        bool send_data_response(const char* content_type,
                                size_t total_len,
                                DataFiller filler) override;

    private:
        AsyncWebServerRequest* m_request;

        static constexpr size_t URI_BUF_SIZE = 128;
        static constexpr size_t ARG_BUF_SIZE = 128;
        static constexpr size_t HEADER_BUF_SIZE = 128;

        /** Room for a full serialized configuration (form payload cap). */
        static constexpr size_t FORM_BUF_SIZE = 1280;

        mutable char m_uri_buf[URI_BUF_SIZE]{};
        mutable char m_arg_buf[ARG_BUF_SIZE]{};
        mutable char m_header_buf[HEADER_BUF_SIZE]{};
        mutable char m_auth_buf[HEADER_BUF_SIZE]{};
        mutable char m_form_buf[FORM_BUF_SIZE]{};
};

/**
 * @class WebServerHalImpl
 * @brief Concrete WebServerHal wrapping ESPAsyncWebServer.
 *
 * Each route registers directly with the AsyncWebServer using
 * its native std::function-based API. Registrations made before
 * init() are buffered and attached to the route table before the
 * server begins listening, so requests are never served with a
 * partial route table.
 */
class WebServerHalImpl : public WebServerHal
{
    public:
        WebServerHalImpl();
        ~WebServerHalImpl();

        bool init(uint16_t port) override;
        void on(const char* uri, HttpMethod method,
                std::function<void(RequestRef&)> handler) override;
        void stop() override;

    private:
        /** Route registration buffered while the server is absent. */
        struct PendingRoute
        {
            std::string uri;
            HttpMethod method;
            std::function<void(RequestRef&)> handler;
        };

        AsyncWebServer* m_server{nullptr};
        std::vector<PendingRoute> m_pending_routes;

        /**
         * @brief Attaches a route handler to the given server.
         *
         * @param server  Server to register the route with.
         * @param uri     URI pattern.
         * @param method  HTTP method.
         * @param handler Handler to invoke on a matching request.
         */
        static void register_route(AsyncWebServer& server, const char* uri,
                                   HttpMethod method,
                                   std::function<void(RequestRef&)> handler);
};

} // namespace hal

#endif // HAL_WEB_SERVER_HAL_IMPL_H
