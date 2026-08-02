#ifndef WEB_SERVER_SERVICE_H
#define WEB_SERVER_SERVICE_H

#include "hal/web_server_hal.h"
#include "logging_service.h"
#include "ini_parser.h"

/**
 * @file web_server_service.h
 * @brief HTTP web server service with route registration and auth.
 *
 * Manages web server lifecycle, HTTP Basic authentication, and
 * the landing page (GET /). Routes are registered during init().
 *
 * Command routes (/status, /control) are registered by
 * WebCommandTransport.
 */
class WebServerService
{
    public:
        WebServerService(hal::WebServerHal& hal, LoggingService& log);

        /**
         * @brief Initializes the web server with the given config.
         *
         * Registers routes via HAL::on() with lambda handlers.
         *
         * @param config Web server configuration from ConfigService.
         */
        void init(const ConfigData::WebConfig& config);

    private:
        hal::WebServerHal& m_hal;
        LoggingService& m_log;
        char m_password[CONFIG_FIELD_MAX_LENGTH]{};

        static constexpr const char* SUBSYSTEM = "WEB";

        /** @brief Route handler for GET /. */
        void handle_index(hal::RequestRef& request);

        /**
         * @brief Checks HTTP Basic auth against the configured password.
         * @param request The incoming request (provides auth header).
         * @return true if auth passes or no password is configured.
         */
        bool check_auth(hal::RequestRef& request);
};

#endif // WEB_SERVER_SERVICE_H
