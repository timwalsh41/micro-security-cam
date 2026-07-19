#include "web_command_transport.h"
#include <cstring>
#include <cstdio>

// Static instance pointer for trampolines.
WebCommandTransport* WebCommandTransport::s_instance = nullptr;

WebCommandTransport::WebCommandTransport(hal::WebServerHal& web,
                                         CommandHandler& handler)
    : m_web(web)
    , m_handler(handler)
{
    m_password[0] = '\0';
}

void WebCommandTransport::register_routes(const char* password)
{
    s_instance = this;

    // Store password for auth checks
    if (password != nullptr)
    {
        strncpy(m_password, password, sizeof(m_password) - 1);
        m_password[sizeof(m_password) - 1] = '\0';
    }
    else
    {
        m_password[0] = '\0';
    }

    m_web.on("/status", hal::HttpMethod::GET,
             WebCommandTransport::handle_status_trampoline);
    m_web.on("/control", hal::HttpMethod::GET,
             WebCommandTransport::handle_control_trampoline);
}

void WebCommandTransport::handle_status_trampoline()
{
    if (s_instance != nullptr)
    {
        s_instance->handle_status();
    }
}

void WebCommandTransport::handle_control_trampoline()
{
    if (s_instance != nullptr)
    {
        s_instance->handle_control();
    }
}

CommandCode WebCommandTransport::map_cmd_string(const char* cmd) const
{
    if (cmd == nullptr || cmd[0] == '\0')
    {
        return CommandCode::GET_STATUS;
    }
    if (std::strcmp(cmd, "start") == 0)
    {
        return CommandCode::START_RECORDING;
    }
    if (std::strcmp(cmd, "stop") == 0)
    {
        return CommandCode::STOP_RECORDING;
    }
    if (std::strcmp(cmd, "photo") == 0)
    {
        return CommandCode::TAKE_PHOTO;
    }
    if (std::strcmp(cmd, "config") == 0)
    {
        return CommandCode::GET_CONFIG;
    }

    return CommandCode::GET_STATUS;
}

void WebCommandTransport::dispatch_and_respond(CommandCode code)
{
    uint8_t response_buf[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;

    CommandResult result = m_handler.dispatch(code, nullptr, 0,
                                              response_buf, response_len);

    // Build JSON response
    char json[128];
    if (result == CommandResult::SUCCESS)
    {
        snprintf(json, sizeof(json), "{\"result\":\"ok\"}");
    }
    else
    {
        snprintf(json, sizeof(json), "{\"result\":\"error\",\"code\":%u}",
                 static_cast<uint8_t>(result));
    }

    m_web.send(200, "application/json", json);
}

bool WebCommandTransport::check_auth()
{
    // No password configured — allow all requests
    if (m_password[0] == '\0')
    {
        return true;
    }

    // Check if Authorization header contains the password
    const char* auth = m_web.auth_header();
    if (auth == nullptr || auth[0] == '\0')
    {
        m_web.send(401, "application/json",
                   "{\"error\":\"authentication required\"}");
        return false;
    }

    if (strstr(auth, m_password) == nullptr)
    {
        m_web.send(401, "application/json",
                   "{\"error\":\"invalid credentials\"}");
        return false;
    }

    return true;
}

void WebCommandTransport::handle_status()
{
    if (!check_auth())
    {
        return;
    }

    const char* cmd = m_web.arg("cmd");
    CommandCode code = map_cmd_string(cmd);
    dispatch_and_respond(code);
}

void WebCommandTransport::handle_control()
{
    if (!check_auth())
    {
        return;
    }

    const char* cmd = m_web.arg("cmd");
    if (cmd == nullptr || cmd[0] == '\0')
    {
        m_web.send(400, "application/json",
                   "{\"error\":\"missing 'cmd' parameter\"}");
        return;
    }

    CommandCode code = map_cmd_string(cmd);
    dispatch_and_respond(code);
}

// ============================================================
// CommandTransport interface (no-ops — web is push-based)
// ============================================================

bool WebCommandTransport::has_request()
{
    return false;
}

void WebCommandTransport::read_request(CommandCode& code,
                                       const uint8_t*& payload,
                                       size_t& payload_len)
{
    (void)code;
    (void)payload;
    (void)payload_len;
}

void WebCommandTransport::send_response(CommandResult code,
                                        const uint8_t* data,
                                        size_t len)
{
    (void)code;
    (void)data;
    (void)len;
}
