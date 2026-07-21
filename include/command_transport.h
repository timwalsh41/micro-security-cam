#ifndef COMMAND_TRANSPORT_H
#define COMMAND_TRANSPORT_H

#include <cstdint>
#include <cstddef>

/**
 * @file command_transport.h
 * @brief Abstract transport interface for the command dispatch framework.
 *
 * Encapsulates request/response mechanics. Concrete transports
 * (web, serial) implement this interface and are registered with
 * CommandHandler for polling-driven dispatch.
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
 *
 * Transports that are callback-driven (e.g. web) may leave the
 * polling methods as no-ops and call CommandHandler::dispatch()
 * directly.
 */
class CommandTransport
{
    public:
        virtual ~CommandTransport() = default;

        /**
         * @brief Check if a complete request is available.
         * @return true if a request is ready to read.
         */
        virtual bool has_request() = 0;

        /**
         * @brief Read the pending request.
         *
         * Only valid when has_request() returned true.
         * After calling, has_request() should return false
         * until the next request arrives.
         *
         * @param[out] code       The command code.
         * @param[out] payload    Pointer to payload data (may be nullptr).
         * @param[out] payload_len Length of payload in bytes.
         */
        virtual void read_request(CommandCode& code,
                                  const uint8_t*& payload,
                                  size_t& payload_len) = 0;

        /**
         * @brief Send a response to the request.
         *
         * @param code  The result code.
         * @param data  Pointer to response data (may be nullptr if len is 0).
         * @param len   Length of response data in bytes.
         */
        virtual void send_response(CommandResult code,
                                   const uint8_t* data,
                                   size_t len) = 0;
};

#endif // COMMAND_TRANSPORT_H
