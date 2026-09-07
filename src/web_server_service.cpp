#include "web_server_service.h"
#include "web_auth_util.h"
#include <cstring>

WebServerService::WebServerService(hal::WebServerHal& hal, LoggingService& log)
    : m_hal(hal)
    , m_log(log)
{
}

void WebServerService::init(const ConfigData::WebConfig& config)
{
    // Store password for authentication
    strncpy(m_password, config.password, CONFIG_FIELD_MAX_LENGTH - 1);
    m_password[CONFIG_FIELD_MAX_LENGTH - 1] = '\0';

    // Register the landing page route before the HAL starts the HTTP
    // server, so the route table is complete before requests are served.
    m_hal.on("/", hal::HttpMethod::GET,
             [this](hal::RequestRef& request)
             {
                 this->handle_index(request);
             });

    // Start the HTTP server
    if (!m_hal.init(config.port))
    {
        m_log.error(SUBSYSTEM, "Failed to start web server on port %u",
                    config.port);
        return;
    }

    // Command routes (/status, /control) are registered by WebCommandTransport

    m_log.info(SUBSYSTEM, "Web server started on port %u", config.port);
}

// ============================================================
// Route handlers
// ============================================================

void WebServerService::handle_index(hal::RequestRef& request)
{
    if (!check_auth(request))
    {
        return;
    }

    static const char* INDEX_HTML =
        "<!DOCTYPE html>"
        "<html><head><title>ESP32 Camera</title></head>"
        "<body><h1>ESP32 Security Camera</h1>"
        "<p>System online.</p></body></html>";

    request.send(200, "text/html", INDEX_HTML);
}

bool WebServerService::check_auth(hal::RequestRef& request)
{
    const char* auth = request.auth_header();

    if (!check_basic_auth(auth, m_password))
    {
        request.send(401, "application/json",
                     "{\"error\":\"authentication required\"}");
        m_log.warn(SUBSYSTEM, "Auth rejected: invalid or missing credentials");
        return false;
    }

    return true;
}
