#ifndef SYSTEM_ORCHESTRATOR_H
#define SYSTEM_ORCHESTRATOR_H

#include "video_controller.h"
#include "camera_controller.h"
#include "command_handler.h"
#include "time_service.h"
#include "config_service.h"
#include "ini_parser.h"
#include "logging_service.h"
#include "sd_controller.h"
#include "hal/nvs_hal.h"
#include "hal/restart_hal.h"
#include "hal/time_hal.h"
#include "hal/web_server_hal.h"

/** Forward declarations. */
class StreamService;

/**
 * @file system_orchestrator.h
 * @brief Top-level system coordinator and command router.
 *
 * All external commands (web, serial, future schedule) route through
 * this component.
 */
class SystemOrchestrator
{
    public:
        /** Delay between accepting a REBOOT command and the actual
         *  restart: long enough for the HTTP response (or serial
         *  reply) to flush to the client first. */
        static constexpr hal::millis_t REBOOT_DELAY_MS = 500;

        /**
         * @brief Constructs the SystemOrchestrator with injected dependencies.
         *
         * @param video      Reference to VideoController for recording control.
         * @param camera     Reference to CameraController for photo capture.
         * @param cmd_handler Reference to CommandHandler for command registration.
         * @param time       Reference to TimeService for time operations.
         * @param config     Reference to ConfigService for configuration.
         * @param log        Reference to LoggingService for logging.
         * @param streams    Reference to StreamService for MJPEG stream control.
         * @param web        Reference to WebServerHal for /stream route registration.
         * @param sd         Reference to SdController for SD availability and
         *                   config persistence to the SD card.
         * @param nvs        Reference to NvsHal for config persistence to NVS.
         * @param restart    Reference to RestartHal for deferred device restart.
         */
        SystemOrchestrator(VideoController& video,
                           CameraController& camera,
                           CommandHandler& cmd_handler,
                           TimeService& time,
                           ConfigService& config,
                           LoggingService& log,
                           StreamService& streams,
                           hal::WebServerHal& web,
                           SdController& sd,
                           hal::NvsHal& nvs,
                           hal::RestartHal& restart);

        /**
         * @brief Initializes the orchestrator.
         *
         * Registers command handlers with the CommandHandler.
         * Must be called after all dependent services are initialized.
         */
        void init();

        /**
         * @brief Returns whether the SD card was available at initialization.
         *
         * The availability is latched from the SD controller at init()
         * time: both a missing card and a failed card initialization
         * latch the system into streaming-only mode (photo capture and
         * recording gated with SD_UNAVAILABLE). Recovery is to insert
         * a card (if missing) and power-cycle.
         *
         * @return true if the SD card was usable when init() ran.
         */
        bool is_sd_available() const;

        /**
         * @brief Periodic tick for future schedule evaluation.
         *
         * Manages schedules and mode transitions.
         */
        void tick();

    private:
        static constexpr const char* SUBSYSTEM = "ORCH";

        VideoController& m_video;
        CameraController& m_camera;
        CommandHandler& m_cmd_handler;
        TimeService& m_time;
        ConfigService& m_config;
        LoggingService& m_log;
        StreamService& m_streams;
        hal::WebServerHal& m_web;
        SdController& m_sd;
        hal::NvsHal& m_nvs;
        hal::RestartHal& m_restart;

        /** Latched SD availability snapshot from init(). */
        bool m_sd_available{false};

        /** TTL-cached SD storage stats for GET_STATUS (refreshed at
         *  most every 60 s instead of scanning on every poll). */
        struct SdStatsCache
        {
            bool valid{false};
            hal::millis_t refreshed_at{0};
            uint64_t free_bytes{0};
            unsigned int photos{0};
            unsigned int videos{0};
        };
        SdStatsCache m_sd_stats;

        /** Register-time snapshot of the web password for /stream auth. */
        char m_stream_password[CONFIG_FIELD_MAX_LENGTH]{};

        /** Register-time snapshot of the web password for /update auth. */
        char m_update_password[CONFIG_FIELD_MAX_LENGTH]{};

        void register_commands();

        /**
         * @brief Registers the /stream MJPEG route.
         */
        void register_stream_route();

        /**
         * @brief Registers the POST /update bulk-config route.
         */
        void register_update_route();

        // Command handler implementations
        CommandResult handle_get_status(const uint8_t* payload, size_t len,
                                        uint8_t* response, size_t& responseLen);
        CommandResult handle_start_recording(const uint8_t* payload, size_t len,
                                             uint8_t* response, size_t& responseLen);
        CommandResult handle_stop_recording(const uint8_t* payload, size_t len,
                                            uint8_t* response, size_t& responseLen);
        CommandResult handle_take_photo(const uint8_t* payload, size_t len,
                                        uint8_t* response, size_t& responseLen);
        CommandResult handle_set_time(const uint8_t* payload, size_t len,
                                      uint8_t* response, size_t& responseLen);
        CommandResult handle_set_config(const uint8_t* payload, size_t len,
                                        uint8_t* response, size_t& responseLen);
        CommandResult handle_get_config(const uint8_t* payload, size_t len,
                                        uint8_t* response, size_t& responseLen);
        CommandResult handle_get_config_value(const uint8_t* payload, size_t len,
                                              uint8_t* response, size_t& responseLen);
        CommandResult handle_set_config_value(const uint8_t* payload, size_t len,
                                              uint8_t* response, size_t& responseLen);
        CommandResult handle_start_stream(const uint8_t* payload, size_t len,
                                          uint8_t* response, size_t& responseLen);
        CommandResult handle_stop_stream(const uint8_t* payload, size_t len,
                                         uint8_t* response, size_t& responseLen);
        CommandResult handle_list_streams(const uint8_t* payload, size_t len,
                                          uint8_t* response, size_t& responseLen);
        CommandResult handle_get_version(const uint8_t* payload, size_t len,
                                         uint8_t* response, size_t& responseLen);
        CommandResult handle_reboot(const uint8_t* payload, size_t len,
                                    uint8_t* response, size_t& responseLen);

        /** @brief Handler for GET /stream — registers a viewer slot. */
        void handle_stream_request(hal::RequestRef& request);

        /** @brief Handler for POST /update — bulk config overlay + save. */
        void handle_update_request(hal::RequestRef& request);
};

#endif // SYSTEM_ORCHESTRATOR_H
