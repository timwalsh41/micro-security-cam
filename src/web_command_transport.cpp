#include "web_command_transport.h"
#include "web_auth_util.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

WebCommandTransport::WebCommandTransport(hal::WebServerHal& web,
                                         CommandHandler& handler)
    : m_web(web)
    , m_handler(handler)
{
    m_password[0] = '\0';
}

void WebCommandTransport::register_routes(const char* password)
{
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
             [this](hal::RequestRef& request)
             {
                 this->handle_status(request);
             });

    m_web.on("/control", hal::HttpMethod::GET,
             [this](hal::RequestRef& request)
             {
                 this->handle_control(request);
             });
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
    if (std::strcmp(cmd, "stream_start") == 0)
    {
        return CommandCode::START_STREAM;
    }
    if (std::strcmp(cmd, "stream_stop") == 0)
    {
        return CommandCode::STOP_STREAM;
    }
    if (std::strcmp(cmd, "stream_list") == 0)
    {
        return CommandCode::LIST_STREAMS;
    }
    if (std::strcmp(cmd, "config") == 0)
    {
        return CommandCode::GET_CONFIG;
    }
    if (std::strcmp(cmd, "config_save") == 0)
    {
        return CommandCode::SET_CONFIG;
    }
    if (std::strcmp(cmd, "set_time") == 0)
    {
        return CommandCode::SET_TIME;
    }

    return CommandCode::GET_STATUS;
}

void WebCommandTransport::dispatch_and_respond(
    hal::RequestRef& request,
    CommandCode code)
{
    dispatch_and_respond(request, code, nullptr, 0);
}

void WebCommandTransport::dispatch_and_respond(
    hal::RequestRef& request,
    CommandCode code,
    const uint8_t* payload,
    size_t payload_len)
{
    uint8_t response_buf[CommandHandler::MAX_RESPONSE_SIZE]{};
    size_t response_len = 0;

    CommandResult result = m_handler.dispatch(code, payload, payload_len,
                                              response_buf, response_len);

    // If the handler wrote response data, forward it directly. The
    // full serialized config (GET_CONFIG) is INI text, not JSON.
    if (response_len > 0)
    {
        if (response_len < CommandHandler::MAX_RESPONSE_SIZE)
        {
            response_buf[response_len] = '\0';
        }
        const char* content_type =
            code == CommandCode::GET_CONFIG ? "text/plain" : "application/json";
        request.send(200, content_type,
                     reinterpret_cast<const char*>(response_buf));
        return;
    }

    // Fall back to basic result for commands with no response payload.
    send_result(request, result);
}

void WebCommandTransport::send_result(
    hal::RequestRef& request,
    CommandResult result)
{
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

    request.send(200, "application/json", json);
}

bool WebCommandTransport::is_config_command(CommandCode code)
{
    return code == CommandCode::GET_CONFIG ||
           code == CommandCode::SET_CONFIG;
}

bool WebCommandTransport::check_auth(hal::RequestRef& request)
{
    const char* auth = request.auth_header();

    if (!check_basic_auth(auth, m_password))
    {
        request.send_unauthorized();
        return false;
    }

    return true;
}

void WebCommandTransport::handle_status(hal::RequestRef& request)
{
    if (!check_auth(request))
    {
        return;
    }

    // /status always returns GET_STATUS
    dispatch_and_respond(request, CommandCode::GET_STATUS);
}

void WebCommandTransport::handle_control(hal::RequestRef& request)
{
    if (!check_auth(request))
    {
        return;
    }

    const char* cmd = request.arg("cmd");
    if (cmd == nullptr || cmd[0] == '\0')
    {
        request.send(400, "application/json",
                     "{\"error\":\"missing 'cmd' parameter\"}");
        return;
    }

    CommandCode code = map_cmd_string(cmd);

    // Config commands carry credentials and the full config body, so
    // they are only served over the web when a password is configured;
    // the serial interface is unrestricted.
    if (m_password[0] == '\0' && is_config_command(code))
    {
        send_result(request, CommandResult::UNAUTHORIZED);
        return;
    }

    // SET_CONFIG needs a destination from the "dest" query parameter.
    if (code == CommandCode::SET_CONFIG)
    {
        const char* dest = request.arg("dest");
        if (dest == nullptr || dest[0] == '\0')
        {
            request.send(400, "application/json",
                         "{\"error\":\"missing 'dest' parameter\"}");
            return;
        }

        // Payload is the destination string: "nvs" or "sd".
        dispatch_and_respond(request, code,
                             reinterpret_cast<const uint8_t*>(dest),
                             strlen(dest));
        return;
    }

    // SET_TIME needs a Unix timestamp from the "ts" query parameter.
    if (code == CommandCode::SET_TIME)
    {
        const char* ts_str = request.arg("ts");
        if (ts_str == nullptr || ts_str[0] == '\0')
        {
            request.send(400, "application/json",
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

        dispatch_and_respond(request, code, payload, sizeof(payload));
        return;
    }

    dispatch_and_respond(request, code);
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
