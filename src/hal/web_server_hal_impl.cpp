#include "hal/web_server_hal_impl.h"
#include <cstring>

#include <WebServer.h>
#include <functional>

namespace hal
{

// ============================================================
// Constructor / Destructor
// ============================================================

WebServerHalImpl::WebServerHalImpl()
    : m_server(nullptr)
{

}

WebServerHalImpl::~WebServerHalImpl()
{
    if (m_server != nullptr)
    {
        delete m_server;
    }

    m_server = nullptr;
}

// ============================================================
// Lifecycle
// ============================================================

bool WebServerHalImpl::begin(uint16_t port)
{
    if (m_server != nullptr)
    {
        delete m_server;
    }

    m_server = new WebServer(port);
    m_server->begin();

    return true;
}

void WebServerHalImpl::on(const char* uri, HttpMethod method,
                          void (*handler)())
{
    if (m_server == nullptr)
    {
        return;
    }

    // Map our HttpMethod enum to Arduino HTTP method constants
    HTTPMethod arduino_method = (method == HttpMethod::POST)
        ? HTTP_POST
        : HTTP_GET;

    // Store the handler pointer and create a wrapper lambda
    // that Arduino WebServer can call. We use a static array
    // to keep handler function pointers alive for the Arduino
    // WebServer lambdas, which capture them by value.
    //
    // The static array persists for the program lifetime, so
    // the pointers remain valid for all WebServer callbacks.
    // If MAX_ROUTES is exceeded, the route is silently not
    // registered — see WebServerHalImpl::MAX_ROUTES.
    static void (*s_handlers[WebServerHalImpl::MAX_ROUTES])();
    static size_t s_count = 0;

    if (s_count >= WebServerHalImpl::MAX_ROUTES)
    {
        return;
    }

    s_handlers[s_count] = handler;
    void (*captured)() = s_handlers[s_count];
    s_count++;

    m_server->on(uri, arduino_method,
                 [captured]() { captured(); });
}

void WebServerHalImpl::handle_client()
{
    if (m_server != nullptr)
    {
        m_server->handleClient();
    }
}

void WebServerHalImpl::stop()
{
    if (m_server != nullptr)
    {
        m_server->stop();
    }
}

// ============================================================
// Request accessors
// ============================================================

const char* WebServerHalImpl::uri() const
{
    if (m_server != nullptr)
    {
        String u = m_server->uri();
        strncpy(m_uri_buf, u.c_str(), URI_BUF_SIZE - 1);
        m_uri_buf[URI_BUF_SIZE - 1] = '\0';
        return m_uri_buf;
    }
    return "";
}

HttpMethod WebServerHalImpl::method() const
{
    if (m_server != nullptr)
    {
        HTTPMethod m = m_server->method();
        if (m == HTTP_POST)
        {
            return HttpMethod::POST;
        }
    }
    return HttpMethod::GET;
}

const char* WebServerHalImpl::arg(const char* name) const
{
    if (m_server != nullptr && name != nullptr)
    {
        String val = m_server->arg(String(name));
        strncpy(m_arg_value_buf, val.c_str(), ARG_BUF_SIZE - 1);
        m_arg_value_buf[ARG_BUF_SIZE - 1] = '\0';
        return m_arg_value_buf;
    }
    return "";
}

const char* WebServerHalImpl::header(const char* name) const
{
    if (m_server != nullptr && name != nullptr)
    {
        String val = m_server->header(String(name));
        strncpy(m_header_value_buf, val.c_str(), HEADER_BUF_SIZE - 1);
        m_header_value_buf[HEADER_BUF_SIZE - 1] = '\0';
        return m_header_value_buf;
    }
    return "";
}

const char* WebServerHalImpl::auth_header() const
{
    if (m_server != nullptr)
    {
        String auth = m_server->header("Authorization");
        strncpy(m_auth_buf, auth.c_str(), HEADER_BUF_SIZE - 1);
        m_auth_buf[HEADER_BUF_SIZE - 1] = '\0';
        return m_auth_buf;
    }
    return "";
}

// ============================================================
// Response senders
// ============================================================

void WebServerHalImpl::send(int code, const char* content_type,
                            const char* content)
{
    if (m_server != nullptr)
    {
        m_server->send(code, content_type, content);
    }
}

void WebServerHalImpl::send(int code, const char* content_type,
                            const uint8_t* data, size_t len)
{
    if (m_server != nullptr)
    {
        m_server->send_P(code, content_type,
                         reinterpret_cast<const char*>(data), len);
    }
}

void WebServerHalImpl::send_header(int code, const char* content_type,
                                   size_t content_length)
{
    if (m_server != nullptr)
    {
        m_server->setContentLength(content_length);
        m_server->sendHeader("Access-Control-Allow-Origin", "*");
        
        // Store code and type for the subsequent send_content call.
        // Arduino WebServer uses beginResponse for streaming.
        (void)code;
        (void)content_type;
    }
}

void WebServerHalImpl::send_content(const uint8_t* data, size_t len)
{
    if (m_server != nullptr)
    {
        // For simple implementations, send the full response at once.
        // Chunked streaming will be refined when MJPEG streaming
        // is implemented.
        (void)data;
        (void)len;
    }
}

} // namespace hal
