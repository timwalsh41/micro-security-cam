#ifndef HAL_WEB_SERVER_HAL_IMPL_H
#define HAL_WEB_SERVER_HAL_IMPL_H

#include "hal/web_server_hal.h"

// Forward-declare Arduino WebServer to avoid including it in the header
class WebServer;

namespace hal
{

/**
 * @file web_server_hal_impl.h
 * @brief ESP32 concrete implementation of WebServerHal.
 *
 * Wraps the Arduino WebServer library behind the abstract
 * WebServerHal interface. All ESP32-specific code is isolated
 * here; the service layer depends only on the abstract interface.
 */
class WebServerHalImpl : public WebServerHal
{
    public:
        WebServerHalImpl();
        ~WebServerHalImpl();

        bool begin(uint16_t port) override;
        void on(const char* uri, HttpMethod method, void (*handler)()) override;
        void handle_client() override;
        void stop() override;

        const char* uri() const override;
        HttpMethod method() const override;
        const char* arg(const char* name) const override;
        const char* header(const char* name) const override;
        const char* auth_header() const override;

        void send(int code, const char* content_type,
                  const char* content) override;
        void send(int code, const char* content_type,
                  const uint8_t* data, size_t len) override;
        void send_header(int code, const char* content_type,
                         size_t content_length) override;
        void send_content(const uint8_t* data, size_t len) override;

    private:
        WebServer* m_server{nullptr};

        /** Internal buffers for string results that may be invalidated. */
        static constexpr size_t URI_BUF_SIZE = 128;
        static constexpr size_t ARG_BUF_SIZE = 128;
        static constexpr size_t HEADER_BUF_SIZE = 128;

        mutable char m_uri_buf[URI_BUF_SIZE]{};
        mutable char m_arg_value_buf[ARG_BUF_SIZE]{};
        mutable char m_arg_name_buf[64]{};
        mutable char m_header_value_buf[HEADER_BUF_SIZE]{};
        mutable char m_header_name_buf[64]{};
        mutable char m_auth_buf[HEADER_BUF_SIZE]{};
};

} // namespace hal

#endif // HAL_WEB_SERVER_HAL_IMPL_H
