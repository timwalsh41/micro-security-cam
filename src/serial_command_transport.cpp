#include "serial_command_transport.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

SerialCommandTransport::SerialCommandTransport(hal::SerialHal& serial)
: m_serial(serial)
, m_line_pos(0)
, m_request_ready(false)
{
}

bool SerialCommandTransport::has_request()
{
    while (m_serial.available() > 0)
    {
        int c = m_serial.read();
        if (c < 0)
        {
            break;
        }

        char ch = static_cast<char>(c);

        if (ch == '\n' || ch == '\r')
        {
            if (m_line_pos > 0)
            {
                m_line_buffer[m_line_pos] = '\0';
                m_request_ready = true;
                m_line_pos = 0;
                return true;
            }
            // Skip blank lines / trailing \r.
            continue;
        }

        if (m_line_pos < LINE_BUFFER_SIZE - 1)
        {
            m_line_buffer[m_line_pos++] = ch;
        }
        else
        {
            // Line too long — discard and reset.
            m_line_pos = 0;
        }
    }

    return false;
}

void SerialCommandTransport::read_request(
    CommandCode& code,
    const uint8_t*& payload,
    size_t& payload_len)
{
    payload = nullptr;
    payload_len = 0;

    if (!m_request_ready)
    {
        code = static_cast<CommandCode>(0);
        return;
    }
    m_request_ready = false;

    // Split line into command and optional args.
    const char* line = m_line_buffer;

    // Skip leading spaces.
    while (*line == ' ')
    {
        line++;
    }

    if (*line == '\0')
    {
        // Empty line — treat as unrecognised.
        code = static_cast<CommandCode>(0);
        return;
    }

    // Find the end of the command word.
    const char* cmd_end = line;
    while (*cmd_end != '\0' && *cmd_end != ' ')
    {
        cmd_end++;
    }

    // Extract command name into a temporary buffer.
    size_t cmd_len = static_cast<size_t>(cmd_end - line);
    char cmd_buf[LINE_BUFFER_SIZE];
    if (cmd_len >= LINE_BUFFER_SIZE)
    {
        cmd_len = LINE_BUFFER_SIZE - 1;
    }
    memcpy(cmd_buf, line, cmd_len);
    cmd_buf[cmd_len] = '\0';

    // Args start after the space (skip whitespace).
    const char* args = cmd_end;
    while (*args == ' ')
    {
        args++;
    }
    if (*args == '\0')
    {
        args = nullptr;
    }

    if (!parse_command(cmd_buf, args, code, payload, payload_len))
    {
        code = static_cast<CommandCode>(0);
    }
}

void SerialCommandTransport::send_response(
    CommandResult code,
    const uint8_t* data,
    size_t len)
{
    char buf[LINE_BUFFER_SIZE + 16];

    if (code == CommandResult::SUCCESS)
    {
        if (data != nullptr && len > 0)
        {
            // Truncate data to fit in the response line.
            size_t copy_len = len;
            if (copy_len > sizeof(buf) - 6)
            {
                copy_len = sizeof(buf) - 6;
            }
            memcpy(buf, "OK ", 3);
            memcpy(buf + 3, data, copy_len);
            buf[3 + copy_len] = '\n';
            size_t total = 3 + copy_len + 1;
            m_serial.write(reinterpret_cast<const uint8_t*>(buf), total);
        }
        else
        {
            static const char ok[] = "OK\n";
            m_serial.write(reinterpret_cast<const uint8_t*>(ok), 3);
        }
    }
    else
    {
        // Format: ERR <code> [message]\n
        int n = snprintf(buf, sizeof(buf), "ERR %02X\n",
                         static_cast<unsigned int>(code));
        if (n > 0)
        {
            m_serial.write(reinterpret_cast<const uint8_t*>(buf),
                           static_cast<size_t>(n));
        }
    }
}

bool SerialCommandTransport::parse_command(
    const char* cmd, const char* args,
    CommandCode& code,
    const uint8_t*& payload,
    size_t& payload_len)
{
    // Holds payload data for the current request.
    static uint8_t payload_buffer[LINE_BUFFER_SIZE];

    // Compare command names (case-sensitive).
    if (strcmp(cmd, "STATUS") == 0)
    {
        code = CommandCode::GET_STATUS;
        payload = nullptr;
        payload_len = 0;
        return true;
    }
    else if (strcmp(cmd, "PHOTO") == 0)
    {
        code = CommandCode::TAKE_PHOTO;
        payload = nullptr;
        payload_len = 0;
        return true;
    }
    else if (strcmp(cmd, "RECORD_START") == 0)
    {
        code = CommandCode::START_RECORDING;
        payload = nullptr;
        payload_len = 0;
        return true;
    }
    else if (strcmp(cmd, "RECORD_STOP") == 0)
    {
        code = CommandCode::STOP_RECORDING;
        payload = nullptr;
        payload_len = 0;
        return true;
    }
    else if (strcmp(cmd, "TIME") == 0)
    {
        if (args == nullptr)
        {
            return false;
        }
        // Parse decimal Unix timestamp into raw 4-byte uint32.
        unsigned long ts = strtoul(args, nullptr, 10);
        payload_buffer[0] = static_cast<uint8_t>(ts & 0xFF);
        payload_buffer[1] = static_cast<uint8_t>((ts >> 8) & 0xFF);
        payload_buffer[2] = static_cast<uint8_t>((ts >> 16) & 0xFF);
        payload_buffer[3] = static_cast<uint8_t>((ts >> 24) & 0xFF);
        code = CommandCode::SET_TIME;
        payload = payload_buffer;
        payload_len = 4;
        return true;
    }
    else if (strcmp(cmd, "GET") == 0)
    {
        if (args == nullptr)
        {
            return false;
        }
        // Payload is the "section.key" string.
        size_t arg_len = strnlen(args, LINE_BUFFER_SIZE - 1);
        memcpy(payload_buffer, args, arg_len);
        payload_buffer[arg_len] = '\0';
        code = CommandCode::GET_CONFIG_VALUE;
        payload = payload_buffer;
        payload_len = arg_len;
        return true;
    }
    else if (strcmp(cmd, "SET") == 0)
    {
        if (args == nullptr)
        {
            return false;
        }
        // Payload is the "section.key=value" string.
        size_t arg_len = strnlen(args, LINE_BUFFER_SIZE - 1);
        memcpy(payload_buffer, args, arg_len);
        payload_buffer[arg_len] = '\0';
        code = CommandCode::SET_CONFIG_VALUE;
        payload = payload_buffer;
        payload_len = arg_len;
        return true;
    }

    return false;
}
