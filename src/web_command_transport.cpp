#include "web_command_transport.h"
#include "web_auth_util.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

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
    if (std::strcmp(cmd, "set_time") == 0)
    {
        return CommandCode::SET_TIME;
    }

    return CommandCode::GET_STATUS;
}

void WebCommandTransport::dispatch_and_respond(CommandCode code)
{
    dispatch_and_respond(code, nullptr, 0);
}

void WebCommandTransport::dispatch_and_respond(
    CommandCode code,
    const uint8_t* payload,
    size_t payload_len)
{
    uint8_t response_buf[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;

    CommandResult result = m_handler.dispatch(code, payload, payload_len,
                                              response_buf, response_len);

    // If the handler wrote response data, forward it directly.
    if (response_len > 0)
    {
        if (response_len < CommandHandler::MAX_RESPONSE_SIZE)
        {
            response_buf[response_len] = '\0';
        }
        m_web.send(200, "application/json",
                   reinterpret_cast<const char*>(response_buf));
        return;
    }

    // Fall back to basic result for commands with no response payload.
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
    const char* auth = m_web.auth_header();

    if (!check_basic_auth(auth, m_password))
    {
        m_web.send(401, "application/json",
                   "{\"error\":\"authentication required\"}");
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

    // /status always returns GET_STATUS
    dispatch_and_respond(CommandCode::GET_STATUS);
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

    // SET_TIME needs a Unix timestamp from the "ts" query parameter.
    if (code == CommandCode::SET_TIME)
    {
        const char* ts_str = m_web.arg("ts");
        if (ts_str == nullptr || ts_str[0] == '\0')
        {
            m_web.send(400, "application/json",
                       "{\"error\":\"missing 'ts' parameter\"}");
            return;
        }

        // Parse and pack as little-endian uint32.
        unsigned long ts = strtoul(ts_str, nullptr, 10);
        uint8_t payload[4];
        payload[0] = static_cast<uint8_t>(ts & 0xFF);
        payload[1] = static_cast<uint8_t>((ts >> 8) & 0xFF);
        payload[2] = static_cast<uint8_t>((ts >> 16) & 0xFF);
        payload[3] = static_cast<uint8_t>((ts >> 24) & 0xFF);

        dispatch_and_respond(code, payload, sizeof(payload));
        return;
    }

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
