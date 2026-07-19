#ifndef SYSTEM_ORCHESTRATOR_H
#define SYSTEM_ORCHESTRATOR_H

#include "video_controller.h"
#include "camera_controller.h"
#include "command_handler.h"
#include "time_service.h"
#include "config_service.h"
#include "logging_service.h"

/**
 * @file system_orchestrator.h
 * @brief Top-level system coordinator and command router.
 *
 * Introduced in Slice 1 as a thin command router. Grows through
 * subsequent slices into the full mode coordinator (capture-type
 * state machine, motion callbacks, scheduling).
 *
 * All external commands (web, serial, future schedule) route through
 * this component.
 */
class SystemOrchestrator
{
    public:
        /**
         * @brief Constructs the SystemOrchestrator with injected dependencies.
         *
         * @param video      Reference to VideoController for recording control.
         * @param camera     Reference to CameraController for photo capture.
         * @param cmd_handler Reference to CommandHandler for command registration.
         * @param time       Reference to TimeService for time operations.
         * @param config     Reference to ConfigService for configuration.
         * @param log        Reference to LoggingService for diagnostics.
         */
        SystemOrchestrator(VideoController& video,
                           CameraController& camera,
                           CommandHandler& cmd_handler,
                           TimeService& time,
                           ConfigService& config,
                           LoggingService& log);

        /**
         * @brief Initializes the orchestrator.
         *
         * Registers command handlers with the CommandHandler.
         * Must be called after all dependent services are initialized.
         */
        void init();

        /**
         * @brief Periodic tick for future schedule evaluation.
         *
         * Currently minimal — grows in Slice 7 to evaluate schedules,
         * manage mode transitions, and enforce min event duration.
         */
        void tick();

    private:
        VideoController& m_video;
        CameraController& m_camera;
        CommandHandler& m_cmd_handler;
        TimeService& m_time;
        ConfigService& m_config;
        LoggingService& m_log;

        void register_commands();

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
};

#endif // SYSTEM_ORCHESTRATOR_H
