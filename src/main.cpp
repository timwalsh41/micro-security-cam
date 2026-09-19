#include "hal/time_hal.h"
#include "platform_initializer.h"
#include "hal/camera_hal_impl.h"
#include "hal/serial_hal_impl.h"
#include "hal/serial_log_backend.h"
#include "hal/sd_card_hal_impl.h"
#include "hal/sd_log_writer.h"
#include "hal/multi_log_backend.h"
#include "hal/wifi_hal_impl.h"
#include "hal/web_server_hal_impl.h"
#include "hal/nvs_hal_impl.h"
#include "hal/timer_hal_impl.h"
#include "hal/gpio_hal.h"
#include "camera_controller.h"
#include "video_controller.h"
#include "capture_scheduler_esp32.h"
#include "time_service.h"
#include "logging_service.h"
#include "sd_controller.h"
#include "led_state_machine.h"
#include "config_service.h"
#include "wifi_service.h"
#include "web_server_service.h"
#include "command_handler.h"
#include "web_command_transport.h"
#include "serial_command_transport.h"
#include "system_orchestrator.h"
#include "stream_service.h"
#include "version.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

using hal::millis_t;

void cs_signal(peripherals::SdSignal signal);

namespace constants
{
    constexpr millis_t MAIN_LOOP_TIME_US = 250;
    constexpr millis_t LED_TICK_TIME_MS = 5;
    constexpr millis_t LOG_HEARTBEAT_TIME_MS = 30000;
}

// --- HAL implementations ---
RealTimeService real_time;
hal::SerialHalImpl serial_hal{115200};
hal::SerialLogBackend serial_log_backend{serial_hal};
hal::SdCardHalImpl sd_hal{hal::SD_CS_PIN};
hal::CameraHalImpl camera_hal;
hal::TimerHalImpl timer_hal;
hal::NvsHalImpl nvs_hal_impl;

// --- FreeRTOS mutex for shared GPIO 21 (LED / SD CS) ---
SemaphoreHandle_t sd_active_mutex = nullptr;

// --- Service-layer objects ---
SdController sd_controller{real_time, sd_hal, cs_signal};
// --- SD logging (must be after sd_controller) ---
SdLogWriter sd_log_writer{sd_controller, real_time, hal::LogLevel::INFO, 7};
MultiLogBackend multi_log_backend;
LoggingService logger{multi_log_backend, real_time};
LedStateMachine led_sm{real_time};
CameraController camera_ctrl{camera_hal};
CaptureSchedulerEsp32 capture_scheduler{timer_hal};
VideoController video_controller{camera_ctrl, sd_controller, real_time, logger, capture_scheduler};
PlatformInitializer platform_init;
hal::WifiHalImpl wifi_hal_impl;
hal::WebServerHalImpl web_server_hal_impl;
WifiService wifi_service{wifi_hal_impl, real_time, logger};
CommandHandler command_handler{logger};
WebCommandTransport web_transport{web_server_hal_impl, command_handler};
SerialCommandTransport serial_transport{serial_hal, real_time};
WebServerService web_server_service{web_server_hal_impl, logger};
ConfigService config_service{logger};
StreamService stream_service{logger};
SystemOrchestrator system_orchestrator{
    video_controller, camera_ctrl, command_handler,
    real_time, config_service, logger,
    stream_service, web_server_hal_impl,
    sd_controller, nvs_hal_impl};


// --- Global state variables ---
millis_t last_log_heartbeat_time_ms = 0;
bool sd_logging_pending = false;

// --- Free helper functions ---
/**
 * @brief CS signal callback for SdController sync I/O.
 *
 * Acquires the shared GPIO mutex on CS_ACTIVE and releases on
 * CS_INACTIVE, ensuring exclusive access to the shared pin
 * during SD transactions.
 */
void cs_signal(peripherals::SdSignal signal)
{
    if (!sd_active_mutex)
    {
        return;
    }

    if (signal == peripherals::SdSignal::CS_ACTIVE)
    {
        xSemaphoreTake(sd_active_mutex, portMAX_DELAY);
        hal::digital_write(hal::LED_PIN, hal::PIN_LOW);   // CS active = LOW
    }
    else
    {
        hal::digital_write(hal::LED_PIN, hal::PIN_HIGH);  // CS inactive = HIGH
        xSemaphoreGive(sd_active_mutex);
    }
}

/**
 * @brief Updates the LED pin based on LedStateMachine output.
 *
 * Non-blocking: skips the update if the SD mutex is held (SD
 * transaction in progress). SD takes priority over LED.
 */
void update_led()
{
    if (!sd_active_mutex)
    {
        return;
    }

    // Try to acquire without blocking — skip if SD owns the pin
    if (xSemaphoreTake(sd_active_mutex, 0) != pdTRUE)
    {
        return;
    }

    bool led_on = led_sm.tick();
    hal::digital_write(hal::LED_PIN, led_on ? hal::PIN_LOW : hal::PIN_HIGH);
    xSemaphoreGive(sd_active_mutex);
}

void setup()
{
    platform_init.init();

    // Add delay for PlatformIO terminal to connect
    real_time.delay_ms(10000);

    // --- Add serial backend to multi_log_backend early so log messages ---
    // --- before SD init are captured by serial output.               ---
    multi_log_backend.add_backend(&serial_log_backend, hal::LogLevel::INFO);

    logger.info("MAIN", "Starting initialization");
    
    char version_string[32] = "unknown";
    if (version::get_version_string(version_string, 32))
    {
        logger.info("MAIN", "MicroCam %s", version_string);
    }
    else
    {
        logger.warn("MAIN", "MicroCam: Unable to retrieve version");
    }

    // --- Create FreeRTOS mutex for shared GPIO pin ---
    sd_active_mutex = xSemaphoreCreateMutex();

    // --- Initialize GPIO and LED ---
    hal::set_pin_as_output(hal::LED_PIN);

    // Initial blink sequence using the mutex-guarded LED
    led_sm.set_mode(LedMode::BLINK, 250, 250);
    {
        auto now = real_time.now();
        while (TimeService::elapsed_ms(now, real_time.now()) < 3000)
        {
            update_led();
            real_time.delay_ms(constants::LED_TICK_TIME_MS);
        }
    }
    led_sm.set_mode(LedMode::OFF);
    update_led();

    // --- Initialize SD card (optional at boot) ---
    // A missing card or a failed card initialization latches the system
    // into streaming-only mode: camera, WiFi, web server, streaming and
    // command dispatch all come up, but recording and photo capture are
    // unavailable. Recovery is insert-card-then-power-cycle.
    hal::SdInitResult sd_result = sd_controller.init_card();
    if (sd_result == hal::SdInitResult::NO_SD_CARD)
    {
        logger.warn("MAIN", "No SD card detected - "
                    "entering streaming-only mode");
    }
    else if (sd_result != hal::SdInitResult::SUCCESS)
    {
        logger.error("MAIN", "SD initialization failed (%u) - "
                     "entering streaming-only mode",
                     static_cast<unsigned>(sd_result));
    }

    // --- Load config: compiled-in defaults overlaid from NVS, then ---
    // --- from the SD card config file when the card is readable.  ---
    // Precedence: defaults < NVS < SD file.
    config_service.load_nvs(nvs_hal_impl);
    config_service.load_sd(sd_controller);

    // --- Initialize camera with config-driven settings ---
    CameraImageConfig cam_image_config;
    cam_image_config.resolution = config_service.config().camera.resolution;
    cam_image_config.jpeg_quality = config_service.config().camera.jpeg_quality;
    cam_image_config.fb_count = 2;
    cam_image_config.rotation = config_service.config().camera.rotation;
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

    // --- Initialize stream service and wire it into the video controller ---
    stream_service.init(config_service.config().web.max_streams);
    video_controller.set_stream_service(&stream_service);

    // --- Initialize WiFi ---
    wifi_service.init(config_service.config().wifi,
                      config_service.config().ap,
                      30);

    // --- Initialize orchestrator (registers commands and the /stream route) ---
    system_orchestrator.init();

    // --- Register serial transport (after all commands are registered) ---
    command_handler.add_transport(serial_transport);

    // --- Register web command routes ---
    // All HTTP routes register before the server starts listening; the
    // HAL buffers registrations made before the server exists and
    // attaches them to the route table before begin().
    web_transport.register_routes(config_service.config().web.password);

    // --- Initialize web server ---
    web_server_service.init(config_service.config().web);

    // --- Initialize SD logging (after SD card is ready) ---
    if (sd_controller.is_card_available())
    {
        switch (config_service.config().logging.sd_enabled)
        {
            case SdEnabledMode::TRUE:
                multi_log_backend.add_backend(&sd_log_writer, hal::LogLevel::INFO);
                logger.info("MAIN", "SD logging ready");
                break;
            case SdEnabledMode::FALSE:
                logger.info("MAIN", "SD logging disabled: config");
                break;
            case SdEnabledMode::AFTER_SET_TIME:
                sd_logging_pending = true;
                logger.info("MAIN", "SD logging deferred: waiting for set_time");
                break;
        }
    }
    else
    {
        logger.info("MAIN", "SD logging disabled: no card");
    }

    logger.info("MAIN", "Initialization complete");
}

void loop()
{
    // Poll command transports (web, serial)
    command_handler.tick();

    // Orchestrator tick (schedule evaluation, mode transitions)
    system_orchestrator.tick();

    // LED state machine tick (non-blocking, skips if SD owns pin)
    update_led();

    // Periodic heartbeat log
    hal::millis_t now = real_time.now();
    if (real_time.elapsed_ms(last_log_heartbeat_time_ms, real_time.now()) >= constants::LOG_HEARTBEAT_TIME_MS)
    {
        logger.info("MAIN", "Core temperature %.2f", temperatureRead());
        last_log_heartbeat_time_ms = real_time.now();
        last_log_heartbeat_time_ms -= last_log_heartbeat_time_ms % constants::LOG_HEARTBEAT_TIME_MS;
    }

    // --- Flush SD log buffer ---
    multi_log_backend.tick();

    // --- Enable SD logging if deferred and time is now set ---
    if (sd_logging_pending && real_time.is_time_set())
    {
        if (sd_controller.is_card_available())
        {
            multi_log_backend.add_backend(&sd_log_writer, hal::LogLevel::INFO);
            logger.info("MAIN", "SD logging enabled: time set");
        }
        sd_logging_pending = false;
    }

    real_time.delay_us(constants::MAIN_LOOP_TIME_US);
}
