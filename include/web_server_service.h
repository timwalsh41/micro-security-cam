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
 * the main handle_client() loop. Routes are registered during init().
 * The landing page (GET /) is served here; command routes (/status,
 * /control) are registered by WebCommandTransport.
 */
class WebServerService
{
    public:
        WebServerService(hal::WebServerHal& hal, LoggingService& log);

        /**
         * @brief Initializes the web server with the given config.
         *
         * @param config Web server configuration from ConfigService.
         */
        void init(const ConfigData::WebConfig& config);

        /**
         * @brief Processes pending HTTP requests.
         * Must be called periodically from the main loop.
         */
        void handle_client();

        /**
         * @brief Route handler for GET /.
         * Public for use by static trampolines.
         */
        void handle_index();

    private:
        hal::WebServerHal& m_hal;
        LoggingService& m_log;
        char m_password[CONFIG_FIELD_MAX_LENGTH]{};

        static constexpr const char* SUBSYSTEM = "WEB";

        /** @brief Singleton instance for static trampolines. */
        static WebServerService* s_instance;

        /** @brief Static trampoline for GET / route. */
        static void trampoline_index();

        bool check_auth();
};

#endif // WEB_SERVER_SERVICE_H
