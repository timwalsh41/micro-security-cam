#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include <cstdint>
#include <cstddef>
#include <functional>
#include "command_transport.h"
#include "logging_service.h"

/**
 * @file command_handler.h
 * @brief Protocol-agnostic command dispatch framework.
 *
 * Transports (web, serial) feed requests in; handlers registered
 * by the orchestrator process them. The handler is protocol-agnostic —
 * changing from text to binary requires only a new transport.
 */

/** Handler function signature. */
using CommandHandlerFn = std::function<CommandResult(
    const uint8_t* payload, size_t len,
    uint8_t* response, size_t& responseLen)>;

/**
 * @brief Protocol-agnostic command dispatch.
 *
 * Commands route through dispatch() to registered handlers.
 * Transports poll via tick() and feed requests in.
 */
class CommandHandler
{
    public:
        /**
         * @brief Maximum size of a command response payload in bytes.
         *
         * Sized to carry a full serialized configuration
         * (ConfigService::CONFIG_BUFFER_SIZE).
         */
        static constexpr size_t MAX_RESPONSE_SIZE = 1024;

        explicit CommandHandler(LoggingService& log);

        void register_handler(CommandCode code, CommandHandlerFn handler);
        void add_transport(CommandTransport& transport);
        void tick();

        CommandResult dispatch(CommandCode code,
                               const uint8_t* payload, size_t len,
                               uint8_t* response, size_t& responseLen);

    private:
        static constexpr size_t MAX_HANDLERS = 16;
        static constexpr size_t MAX_TRANSPORTS = 4;

        static constexpr const char* SUBSYSTEM = "CMDH";

        LoggingService& m_log;

        struct HandlerEntry
        {
            CommandCode code;
            CommandHandlerFn fn;
        };
        HandlerEntry m_handlers[MAX_HANDLERS]{};
        size_t m_handler_count{0};

        CommandTransport* m_transports[MAX_TRANSPORTS]{};
        size_t m_transport_count{0};
};

#endif // COMMAND_HANDLER_H
