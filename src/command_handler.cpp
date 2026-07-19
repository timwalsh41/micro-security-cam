#include "command_handler.h"
#include <cstring>

CommandHandler::CommandHandler(LoggingService& log)
    : m_log(log)
{
}

void CommandHandler::register_handler(CommandCode code,
                                      CommandHandlerFn handler)
{
    if (m_handler_count >= MAX_HANDLERS)
    {
        m_log.warn(SUBSYSTEM, "Handler table full, cannot register 0x%02X",
                   static_cast<uint8_t>(code));
        return;
    }

    m_handlers[m_handler_count].code = code;
    m_handlers[m_handler_count].fn = handler;
    m_handler_count++;

    m_log.debug(SUBSYSTEM, "Registered handler for 0x%02X",
                static_cast<uint8_t>(code));
}

void CommandHandler::add_transport(CommandTransport& transport)
{
    if (m_transport_count >= MAX_TRANSPORTS)
    {
        m_log.warn(SUBSYSTEM, "Transport table full, cannot add transport");
        return;
    }

    m_transports[m_transport_count] = &transport;
    m_transport_count++;

    m_log.debug(SUBSYSTEM, "Added transport (%zu total)", m_transport_count);
}

void CommandHandler::tick()
{
    for (size_t i = 0; i < m_transport_count; i++)
    {
        if (!m_transports[i]->has_request())
        {
            continue;
        }

        CommandCode code = CommandCode::GET_STATUS;
        const uint8_t* payload = nullptr;
        size_t payload_len = 0;

        m_transports[i]->read_request(code, payload, payload_len);

        uint8_t response[MAX_RESPONSE_SIZE]{};
        size_t response_len = 0;

        CommandResult result = dispatch(code, payload, payload_len,
                                        response, response_len);

        m_transports[i]->send_response(result, response, response_len);
    }
}

CommandResult CommandHandler::dispatch(CommandCode code,
                                       const uint8_t* payload, size_t len,
                                       uint8_t* response,
                                       size_t& responseLen)
{
    m_log.debug(SUBSYSTEM, "Dispatching command 0x%02X",
                static_cast<uint8_t>(code));

    for (size_t i = 0; i < m_handler_count; i++)
    {
        if (m_handlers[i].code == code)
        {
            CommandResult result = m_handlers[i].fn(payload, len,
                                                    response, responseLen);
            m_log.debug(SUBSYSTEM, "Command 0x%02X result: %u",
                        static_cast<uint8_t>(code),
                        static_cast<uint8_t>(result));
            return result;
        }
    }

    m_log.warn(SUBSYSTEM, "No handler for command 0x%02X",
               static_cast<uint8_t>(code));
    return CommandResult::UNRECOGNIZED;
}
