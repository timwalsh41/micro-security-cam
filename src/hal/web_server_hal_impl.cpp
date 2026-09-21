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

const char* AsyncRequestRef::form_value(const char* name) const
{
    if (m_request != nullptr && name != nullptr)
    {
        String val = m_request->arg(name);
        strncpy(m_form_buf, val.c_str(), FORM_BUF_SIZE - 1);
        m_form_buf[FORM_BUF_SIZE - 1] = '\0';
        return m_form_buf;
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

void AsyncRequestRef::send_unauthorized()
{
    if (m_request != nullptr)
    {
        // Browsers require a WWW-Authenticate challenge header to
        // trigger a credential prompt on a 401; beginResponse +
        // addHeader attaches it alongside the JSON error body that
        // scripted clients already expect.
        AsyncWebServerResponse* response =
            m_request->beginResponse(401, "application/json",
                                     "{\"error\":\"authentication required\"}");
        response->addHeader("WWW-Authenticate", AUTH_CHALLENGE);
        m_request->send(response);
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

bool AsyncRequestRef::send_data_response(const char* content_type,
                                         size_t total_len,
                                         DataFiller filler)
{
    if (m_request == nullptr || content_type == nullptr || !filler)
    {
        return false;
    }

    // The library's AwsResponseFiller matches DataFiller's signature:
    // (buf, maxLen, index) where index is the byte offset of the chunk
    // within the response body.
    m_request->send(content_type, total_len,
                    [filler](uint8_t* buf, size_t max_len, size_t index) -> size_t
                    {
                        return filler(buf, max_len, index);
                    });
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

    // Complete the route table before the server starts listening:
    // handlers registered while the server was absent are attached
    // here, so begin() never serves a request with a partial route
    // table.
    for (PendingRoute& route : m_pending_routes)
    {
        register_route(*m_server, route.uri.c_str(), route.method,
                       std::move(route.handler));
    }
    m_pending_routes.clear();

    m_server->begin();

    return true;
}

void WebServerHalImpl::on(const char* uri, HttpMethod method,
                          std::function<void(RequestRef&)> handler)
{
    if (m_server == nullptr)
    {
        // Buffer the registration; init() replays it before begin().
        PendingRoute route;
        route.uri = (uri != nullptr) ? uri : "";
        route.method = method;
        route.handler = std::move(handler);
        m_pending_routes.push_back(std::move(route));
        return;
    }

    register_route(*m_server, uri, method, std::move(handler));
}

void WebServerHalImpl::register_route(AsyncWebServer& server,
                                      const char* uri, HttpMethod method,
                                      std::function<void(RequestRef&)> handler)
{
    WebRequestMethodComposite arduino_method = (method == HttpMethod::POST)
        ? HTTP_POST
        : HTTP_GET;

    server.on(uri, arduino_method,
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
