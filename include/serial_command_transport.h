#ifndef SERIAL_COMMAND_TRANSPORT_H
#define SERIAL_COMMAND_TRANSPORT_H

#include "command_transport.h"
#include "hal/serial_hal.h"

/**
 * @file serial_command_transport.h
 * @brief Serial text-protocol transport for the command dispatch framework.
 *
 * Implements a simple line-based text protocol over UART. Each line is a
 * single command; responses are returned as OK/ERR text lines. Line
 * accumulation happens inside has_request(), so no external tick() is
 * needed.
 *
 * Protocol:
 *   TX: COMMAND [args]\n
 *   RX: OK [data]\n
 *   RX: ERR <code> [message]\n
 *
 * Commands:
 *   STATUS          -> GET_STATUS (no args)
 *   PHOTO           -> TAKE_PHOTO (no args)
 *   RECORD_START    -> START_RECORDING (no args)
 *   RECORD_STOP     -> STOP_RECORDING (no args)
 *   TIME <unix_ts>  -> SET_TIME (4-byte payload)
 *   GET <key>       -> GET_CONFIG_VALUE (payload = "section.key")
 *   SET <key>=<val> -> SET_CONFIG_VALUE (payload = "section.key=value")
 */

class SerialCommandTransport : public CommandTransport
{
    public:
        /**
         * @brief Maximum length of a single command line (including newline).
         */
        static constexpr size_t LINE_BUFFER_SIZE = 128;

        /**
         * @brief Constructs the serial transport.
         *
         * @param serial  Reference to a SerialHal for I/O.
         */
        explicit SerialCommandTransport(hal::SerialHal& serial);

        // ---- CommandTransport overrides ----

        bool has_request() override;
        void read_request(CommandCode& code,
                          const uint8_t*& payload,
                          size_t& payload_len) override;
        void send_response(CommandResult code,
                           const uint8_t* data,
                           size_t len) override;

    private:
        hal::SerialHal& m_serial;

        char m_line_buffer[LINE_BUFFER_SIZE];
        size_t m_line_pos{0};
        bool m_request_ready{false};

        /**
         * @brief Maps a text command to a CommandCode.
         * @param cmd  Null-terminated command name (e.g. "STATUS").
         * @param args Pointer into m_line_buffer past the command + space,
         *             or nullptr if no args.
         * @param[out] code       The resolved CommandCode.
         * @param[out] payload    Pointer to the payload data.
         * @param[out] payload_len Length in bytes.
         * @return true if the command was recognised.
         */
        bool parse_command(const char* cmd, const char* args,
                           CommandCode& code,
                           const uint8_t*& payload,
                           size_t& payload_len);
};

#endif // SERIAL_COMMAND_TRANSPORT_H
