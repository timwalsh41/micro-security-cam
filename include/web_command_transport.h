#ifndef WEB_COMMAND_TRANSPORT_H
#define WEB_COMMAND_TRANSPORT_H

#include "hal/web_server_hal.h"
#include "command_handler.h"
#include "command_transport.h"
#include "ini_parser.h"

/**
 * @file web_command_transport.h
 * @brief HTTP → CommandHandler bridge.
 *
 * Routes HTTP requests to CommandHandler::dispatch(). Each
 * registered command becomes a thin HTTP endpoint.
 *
 * Implements CommandTransport to satisfy the interface contract,
 * but web requests are push-based — the actual dispatch happens
 * inline in the route callbacks, not through the polling model.
 */
class WebCommandTransport : public CommandTransport
{
    public:
        WebCommandTransport(hal::WebServerHal& web, CommandHandler& handler);

        /**
         * @brief Registers HTTP routes for /status and /control.
         *
         * Must be called after the web server is initialized and
         * all commands are registered with the CommandHandler.
         *
         * @param password  Optional web password for Basic auth.
         *                  If null or empty, auth is skipped.
         */
        void register_routes(const char* password = nullptr);

        // -- CommandTransport interface --
        // Web requests are push-based (handled in route callbacks),
        // so these are not used for polling.
        bool has_request() override;
        void read_request(CommandCode& code,
                          const uint8_t*& payload,
                          size_t& payload_len) override;
        void send_response(CommandResult code,
                           const uint8_t* data, size_t len) override;

    private:
        static constexpr const char* SUBSYSTEM = "WEBCMD";

        hal::WebServerHal& m_web;
        CommandHandler& m_handler;
        char m_password[CONFIG_FIELD_MAX_LENGTH]{};

        /**
         * @brief Checks HTTP Basic auth against the configured password.
         * @param request The incoming request (provides auth header).
         * @return true if auth passes or no password is configured.
         */
        bool check_auth(hal::RequestRef& request);

        /**
         * @brief Maps a query parameter string to a CommandCode.
         * @param cmd  The command string from the "cmd" query param.
         * @return The matching CommandCode, or GET_STATUS as default.
         */
        CommandCode map_cmd_string(const char* cmd) const;

        /**
         * @brief Dispatches a command and sends a JSON response.
         * @param request The request to send the response through.
         * @param code    The command to dispatch.
         */
        void dispatch_and_respond(hal::RequestRef& request,
                                  CommandCode code);

        /**
         * @brief Dispatches a command with a binary payload and sends a JSON response.
         * @param request     The request to send the response through.
         * @param code        The command to dispatch.
         * @param payload     Pointer to payload data (may be nullptr).
         * @param payload_len Length of payload in bytes.
         */
        void dispatch_and_respond(hal::RequestRef& request,
                                  CommandCode code,
                                  const uint8_t* payload,
                                  size_t payload_len);

        /** @brief Handler for GET /status — always dispatches GET_STATUS. */
        void handle_status(hal::RequestRef& request);

        /** @brief Handler for GET /control — dispatches the command from the "cmd" query param. */
        void handle_control(hal::RequestRef& request);

        /**
         * @brief Sends the standard JSON result envelope for a result code.
         *
         * Used for commands that produce no response payload:
         * `{"result":"ok"}` on success, `{"result":"error","code":N}`
         * otherwise.
         */
        void send_result(hal::RequestRef& request, CommandResult result);

        /**
         * @brief Returns true if the command manages the configuration.
         *
         * Config commands are refused on the web interface while no web
         * password is configured; the serial interface is unrestricted.
         */
        static bool is_config_command(CommandCode code);
};

#endif // WEB_COMMAND_TRANSPORT_H
