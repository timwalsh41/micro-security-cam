#include "hal/web_server_hal_impl.h"
#include "async_stream_response.h"
#include <cstring>

#include <ESPAsyncWebServer.h>
#include <functional>

namespace hal
{

// ============================================================
// AsyncRequestRef
// ============================================================

AsyncRequestRef::AsyncRequestRef(AsyncWebServerRequest* request)
    : m_request(request)
{
}

const char* AsyncRequestRef::uri() const
{
    if (m_request != nullptr)
    {
        strncpy(m_uri_buf, m_request->url().c_str(), URI_BUF_SIZE - 1);
        m_uri_buf[URI_BUF_SIZE - 1] = '\0';
        return m_uri_buf;
    }
    return "";
}

HttpMethod AsyncRequestRef::method() const
{
    // GET/POST-only mapping limitation is documented in the header
    if (m_request != nullptr)
    {
        auto m = m_request->method();
        if (m == HTTP_POST)
        {
            return HttpMethod::POST;
        }
    }
    return HttpMethod::GET;
}

const char* AsyncRequestRef::arg(const char* name) const
{
    if (m_request != nullptr && name != nullptr)
    {
        String val = m_request->arg(name);
        strncpy(m_arg_buf, val.c_str(), ARG_BUF_SIZE - 1);
        m_arg_buf[ARG_BUF_SIZE - 1] = '\0';
        return m_arg_buf;
    }
    return "";
}

const char* AsyncRequestRef::header(const char* name) const
{
    if (m_request != nullptr && name != nullptr)
    {
        String val = m_request->header(name);
        strncpy(m_header_buf, val.c_str(), HEADER_BUF_SIZE - 1);
        m_header_buf[HEADER_BUF_SIZE - 1] = '\0';
        return m_header_buf;
    }
    return "";
}

const char* AsyncRequestRef::auth_header() const
{
    if (m_request != nullptr)
    {
        String val = m_request->header("Authorization");
        strncpy(m_auth_buf, val.c_str(), HEADER_BUF_SIZE - 1);
        m_auth_buf[HEADER_BUF_SIZE - 1] = '\0';
        return m_auth_buf;
    }
    return "";
}

void AsyncRequestRef::send(int code, const char* content_type,
                           const char* content)
{
    if (m_request != nullptr)
    {
        m_request->send(code, content_type, content);
    }
}

void AsyncRequestRef::send(int code, const char* content_type,
                           const uint8_t* data, size_t len)
{
    if (m_request != nullptr)
    {
        m_request->send(code, content_type, data, len);
    }
}

bool AsyncRequestRef::send_stream_response(
    std::shared_ptr<StreamFrameChannel> channel,
    const char* boundary,
    StreamEndCallback on_end)
{
    if (m_request == nullptr || !channel || boundary == nullptr)
    {
        return false;
    }

    // The response is owned by the request from here on: the transport
    // sends it, pulls parts from the channel under framework flow
    // control, and destroys it on disconnect or completion (firing
    // on_end so the viewer slot can be reclaimed).
    AsyncJpegStreamResponse* response =
        new AsyncJpegStreamResponse(std::move(channel), boundary,
                                    std::move(on_end));
    m_request->send(response);
    return true;
}

// ============================================================
// WebServerHalImpl
// ============================================================

WebServerHalImpl::WebServerHalImpl()
    : m_server(nullptr)
{
}

WebServerHalImpl::~WebServerHalImpl()
{
    stop();
    delete m_server;
    m_server = nullptr;
}

bool WebServerHalImpl::init(uint16_t port)
{
    stop();
    delete m_server;

    m_server = new AsyncWebServer(port);
    m_server->begin();

    return true;
}

void WebServerHalImpl::on(const char* uri, HttpMethod method,
                          std::function<void(RequestRef&)> handler)
{
    if (m_server == nullptr)
    {
        return;
    }

    WebRequestMethodComposite arduino_method = (method == HttpMethod::POST)
        ? HTTP_POST
        : HTTP_GET;

    m_server->on(uri, arduino_method,
                 [handler](AsyncWebServerRequest* request)
                 {
                     AsyncRequestRef ref(request);
                     handler(ref);
                 });
}

void WebServerHalImpl::stop()
{
    if (m_server != nullptr)
    {
        m_server->end();
    }
}

} // namespace hal
