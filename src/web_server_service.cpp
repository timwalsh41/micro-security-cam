#include "web_server_service.h"
#include "web_auth_util.h"
#include "web_ui_content.h"
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

    // Embedded single-page UI (see web_ui_content.cpp). A future
    // SD-card /data/ override may shadow this document per-URL; the
    // serving order and reserved config hook are documented in
    // docs/SYSTEM_DESIGN.md (Web UI Hosting). The override is not
    // implemented yet, so the embedded page is always served.
    request.send(200, "text/html", web_ui_html());
}

bool WebServerService::check_auth(hal::RequestRef& request)
{
    const char* auth = request.auth_header();

    if (!check_basic_auth(auth, m_password))
    {
        request.send_unauthorized();
        m_log.warn(SUBSYSTEM, "Auth rejected: invalid or missing credentials");
        return false;
    }

    return true;
}
