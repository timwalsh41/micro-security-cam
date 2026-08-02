#ifndef HAL_WEB_SERVER_HAL_IMPL_H
#define HAL_WEB_SERVER_HAL_IMPL_H

#include "hal/web_server_hal.h"

/**
 * @file web_server_hal_impl.h
 * @brief ESP32 concrete implementation of WebServerHal using AsyncWebServer.
 *
 * Implements WebServerHal interface.
 */

/** Forward declaration for AsyncWebServer and AsyncWebServerRequest */
class AsyncWebServer;
class AsyncWebServerRequest;

namespace hal
{

/** Forward declaration for stream writers */
class StreamWriter;

/**
 * @file web_server_hal_impl.h
 * @brief ESP32 concrete implementation of WebServerHal using AsyncWebServer.
 *
 * Wraps the ESPAsyncWebServer library behind the abstract WebServerHal
 * interface. Route handlers use std::function with captures.
 */

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
        HttpMethod method() const override;
        const char* arg(const char* name) const override;
        const char* header(const char* name) const override;
        const char* auth_header() const override;

        void send(int code, const char* content_type,
                  const char* content) override;
        void send(int code, const char* content_type,
                  const uint8_t* data, size_t len) override;

        StreamWriter* create_stream_writer() override;

    private:
        AsyncWebServerRequest* m_request;

        static constexpr size_t URI_BUF_SIZE = 128;
        static constexpr size_t ARG_BUF_SIZE = 128;
        static constexpr size_t HEADER_BUF_SIZE = 128;

        mutable char m_uri_buf[URI_BUF_SIZE]{};
        mutable char m_arg_buf[ARG_BUF_SIZE]{};
        mutable char m_header_buf[HEADER_BUF_SIZE]{};
        mutable char m_auth_buf[HEADER_BUF_SIZE]{};
};

/**
 * @class WebServerHalImpl
 * @brief Concrete WebServerHal wrapping ESPAsyncWebServer.
 *
 * Each route registers directly with the AsyncWebServer using
 * its native std::function-based API.
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
        AsyncWebServer* m_server{nullptr};
};

} // namespace hal

#endif // HAL_WEB_SERVER_HAL_IMPL_H
