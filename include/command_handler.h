#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include <cstdint>
#include <cstddef>
#include <functional>
#include "logging_service.h"

/**
 * @file command_handler.h
 * @brief Protocol-agnostic command dispatch framework.
 *
 * Transports (web, serial) feed requests in; handlers registered
 * by the orchestrator process them. The handler is protocol-agnostic —
 * changing from text to binary requires only a new transport.
 */

/** Command codes — extensible as features land. */
enum class CommandCode : uint8_t
{
    GET_STATUS = 0x01,
    START_RECORDING = 0x10,
    STOP_RECORDING = 0x11,
    TAKE_PHOTO = 0x12,
    SET_CONFIG = 0x20,
    GET_CONFIG = 0x21,
    SET_TIME = 0x30,
};

/** Result codes returned by command handlers. */
enum class CommandResult : uint8_t
{
    SUCCESS = 0x00,
    UNRECOGNIZED = 0x01,
    INVALID_ARGS = 0x02,
    INVALID_STATE = 0x03,
    INTERNAL_ERROR = 0xFF,
};

/**
 * @brief Abstract transport interface.
 *
 * Encapsulates request/response mechanics. Concrete transports
 * (web, serial) implement this interface.
 */
class CommandTransport
{
    public:
        virtual ~CommandTransport() = default;
        virtual bool has_request() = 0;
        virtual void read_request(CommandCode& code,
                                  const uint8_t*& payload,
                                  size_t& payload_len) = 0;
        virtual void send_response(CommandResult code,
                                   const uint8_t* data, size_t len) = 0;
};

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
        static constexpr size_t MAX_RESPONSE_SIZE = 256;

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

        static constexpr const char* SUBSYSTEM = "CmdH";

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
