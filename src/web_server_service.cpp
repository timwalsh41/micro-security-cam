#include "web_server_service.h"
#include <cstring>

static constexpr const char* SUBSYSTEM = "Web";

/**
 * @brief Static trampolines for route callbacks.
 *
 * The WebServerHal::on() interface takes a raw function pointer void(*)(),
 * which cannot capture state. A single global pointer tracks the active
 * WebServerService instance and delegates to the appropriate method.
 */
static WebServerService* s_instance = nullptr;

static void trampoline_index()
{
    if (s_instance != nullptr)
    {
        s_instance->handle_index();
    }
}

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

    // Set static instance for trampoline callbacks
    s_instance = this;

    // Start the HTTP server
    if (!m_hal.begin(config.port))
    {
        m_log.error(SUBSYSTEM, "Failed to start web server on port %u",
                    config.port);
        return;
    }

    // Register landing page route
    m_hal.on("/", hal::HttpMethod::GET, trampoline_index);
    // Command routes (/status, /control) are registered by WebCommandTransport

    m_log.info(SUBSYSTEM, "Web server started on port %u", config.port);
}

void WebServerService::handle_client()
{
    m_hal.handle_client();
}

// ============================================================
// Route handlers
// ============================================================

void WebServerService::handle_index()
{
    if (!check_auth())
    {
        return;
    }

    static const char* INDEX_HTML =
        "<!DOCTYPE html>"
        "<html><head><title>ESP32 Camera</title></head>"
        "<body><h1>ESP32 Security Camera</h1>"
        "<p>System online.</p></body></html>";

    m_hal.send(200, "text/html", INDEX_HTML);
}

bool WebServerService::check_auth()
{
    // No password configured — allow all requests
    if (m_password[0] == '\0')
    {
        return true;
    }

    // Check if Authorization header contains the password
    const char* auth = m_hal.auth_header();
    if (auth == nullptr || auth[0] == '\0')
    {
        m_hal.send(401, "application/json",
                   "{\"error\":\"authentication required\"}");
        m_log.warn(SUBSYSTEM, "Auth rejected: no credentials provided");
        return false;
    }

    if (strstr(auth, m_password) == nullptr)
    {
        m_hal.send(401, "application/json",
                   "{\"error\":\"invalid credentials\"}");
        m_log.warn(SUBSYSTEM, "Auth rejected: invalid credentials");
        return false;
    }

    return true;
}
