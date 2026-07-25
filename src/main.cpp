#include "hal/time_hal.h"
#include "platform_initializer.h"
#include "hal/camera_hal_impl.h"
#include "hal/serial_log_backend.h"
#include "hal/sd_card_hal_impl.h"
#include "hal/wifi_hal_impl.h"
#include "hal/web_server_hal_impl.h"
#include "camera_controller.h"
#include "video_controller.h"
#include "time_service.h"
#include "logging_service.h"
#include "peripheral_manager.h"
#include "config_service.h"
#include "wifi_service.h"
#include "web_server_service.h"
#include "command_handler.h"
#include "web_command_transport.h"
#include "system_orchestrator.h"

using hal::millis_t;

namespace constants
{
    constexpr millis_t LED_ON_TIME_MS = 1000;
    constexpr millis_t DEFAULT_CAMERA_CAPTURE_INTERVAL_MS = 20000;
    constexpr millis_t PERIPHERAL_MANAGER_TICK_TIME_MS = 5;
}

// --- Core services ---
RealTimeService real_time;
hal::SerialLogBackend log_backend;
LoggingService logger{log_backend, real_time};
ConfigService config_service{logger};
hal::SdCardHalImpl sd_hal{SD_CS};
PeripheralManager peripheral_manager{real_time, sd_hal};
hal::CameraHalImpl camera_hal;
CameraController camera_ctrl{camera_hal};
VideoController video_controller{camera_ctrl, peripheral_manager, real_time, logger};
PlatformInitializer platform_init;
hal::WifiHalImpl wifi_hal_impl;
hal::WebServerHalImpl web_server_hal_impl;
WifiService wifi_service{wifi_hal_impl, real_time, logger};
CommandHandler command_handler{logger};
WebCommandTransport web_transport{web_server_hal_impl, command_handler};
WebServerService web_server_service{web_server_hal_impl, logger};
SystemOrchestrator system_orchestrator{
    video_controller, camera_ctrl, command_handler,
    real_time, config_service, logger};

millis_t last_peripheral_tick_time_ms = 0;
millis_t last_capture_time_ms = 0;
uint32_t image_count = 1;
millis_t camera_capture_interval_ms = constants::DEFAULT_CAMERA_CAPTURE_INTERVAL_MS;

void take_photo(uint32_t image_count)
{
    CameraCaptureResult result = camera_ctrl.capture_frame();
    if (result != CameraCaptureResult::SUCCESS)
    {
        logger.error("CAMERA", "Failed to capture frame");
        return;
    }

    hal::CameraFrame frame = camera_ctrl.get_frame();

    char file_name[32];
    snprintf(file_name, sizeof(file_name), "/image%u.jpg", image_count);
    peripheral_manager.write_file(file_name, frame.data, frame.len, 1000u);
    camera_ctrl.release_frame();

    logger.info("CAMERA", "Saved picture: %s", file_name);
}

void setup()
{
    platform_init.init();

    // Add delay for PlatformIO terminal to connect
    real_time.delay_ms(5000);

    logger.info("MAIN", "Starting initialization");

    peripheral_manager.init();
    peripheral_manager.blink(250, 250);
    {
        auto now = real_time.now();

        while (real_time.now() < (now + 3000))
        {
            peripheral_manager.tick();
            real_time.delay_ms(constants::PERIPHERAL_MANAGER_TICK_TIME_MS);
        }
    }
    peripheral_manager.turn_led_off();

    // --- Initialize SD card ---
    if (peripheral_manager.init_sd_card() != hal::SdInitResult::SUCCESS)
    {
        logger.error("MAIN", "SD initialization failed");
        return;
    }
    logger.info("MAIN", "SD initialized");

    // --- Load config FIRST (needs SD card) ---
    uint8_t config_buf[ConfigService::CONFIG_BUFFER_SIZE];
    size_t bytes_read = 0;
    peripheral_manager.read_file(ConfigService::DEFAULT_CONFIG_PATH,
                                 config_buf, sizeof(config_buf),
                                 &bytes_read, ConfigService::READ_TIMEOUT_MS);
    logger.info("MAIN", "Config read: %zu / %zu bytes",
                bytes_read, ConfigService::CONFIG_BUFFER_SIZE);
    config_service.load(config_buf, bytes_read);

    // --- Initialize camera with config-driven settings ---
    CameraImageConfig cam_image_config;
    cam_image_config.resolution = config_service.config().camera.resolution;
    cam_image_config.jpeg_quality = config_service.config().camera.jpeg_quality;
    ResolvedCameraParams cam_params = build_camera_config(cam_image_config, logger);
    hal::CameraInitResult cam_result = camera_ctrl.init(cam_params);
    if (cam_result != hal::CameraInitResult::SUCCESS)
    {
        logger.error("MAIN", "Camera initialization failed");
        return;
    }
    logger.info("MAIN", "Camera initialized (%ux%u)",
                camera_ctrl.get_params().width,
                camera_ctrl.get_params().height);

    // --- Initialize video controller with validated dimensions ---
    video_controller.init(config_service.config().video,
                          camera_ctrl.get_params().width,
                          camera_ctrl.get_params().height);

    // --- Apply capture interval from config ---
    if (config_service.config().camera.capture_interval_ms > 0)
    {
        camera_capture_interval_ms = config_service.config().camera.capture_interval_ms;
        logger.info("MAIN", "Capture interval: %u ms", camera_capture_interval_ms);
    }

    // --- Initialize WiFi ---
    wifi_service.init(config_service.config().wifi,
                      config_service.config().ap,
                      30);

    // --- Initialize web server ---
    web_server_service.init(config_service.config().web);

    // --- Initialize orchestrator (registers commands) ---
    system_orchestrator.init();

    // --- Register web command routes (after all commands are registered) ---
    web_transport.register_routes(config_service.config().web.password);

    logger.info("MAIN", "Initialization complete");
}

void loop()
{
    if (camera_ctrl.is_initialized() && peripheral_manager.is_sd_card_initialized())
    {
        auto now = real_time.now();

        if (TimeService::elapsed_ms(last_peripheral_tick_time_ms, now) >= constants::PERIPHERAL_MANAGER_TICK_TIME_MS)
        {
            peripheral_manager.tick();
            last_peripheral_tick_time_ms = now;
        }

        // Advance video recording state machine
        video_controller.tick();

        // Poll command transports (web, serial)
        command_handler.tick();

        // Process HTTP requests
        web_server_service.handle_client();

        // Orchestrator tick (schedule evaluation, mode transitions)
        system_orchestrator.tick();

        // LED feedback: video state takes precedence over photo capture
        switch (video_controller.state())
        {
            case RecordingState::RECORDING:
                peripheral_manager.blink(200, 200);
                break;
            case RecordingState::STOPPING:
                peripheral_manager.pulse_led(500);
                break;
            case RecordingState::IDLE:
                // LED controlled by other logic below
                break;
        }

        if (TimeService::elapsed_ms(last_capture_time_ms, now) >= camera_capture_interval_ms)
        {
            if (!video_controller.is_recording())
            {
                peripheral_manager.pulse_led(constants::LED_ON_TIME_MS);
            }
            last_capture_time_ms = now;
            logger.info("MAIN", "Capturing image...");
            take_photo(image_count);
            image_count++;
        }
        else if (now % 5000 == 0)
        {
            logger.info("MAIN", "Looping...");
            real_time.delay_ms(2);
        }
    }
}
