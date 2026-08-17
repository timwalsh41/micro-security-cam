#include "hal/time_hal.h"
#include "platform_initializer.h"
#include "hal/camera_hal_impl.h"
#include "hal/serial_hal_impl.h"
#include "hal/serial_log_backend.h"
#include "hal/sd_card_hal_impl.h"
#include "hal/wifi_hal_impl.h"
#include "hal/web_server_hal_impl.h"
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

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

using hal::millis_t;

namespace constants
{
    constexpr millis_t LED_TICK_TIME_MS = 5;
}

// --- HAL implementations ---
RealTimeService real_time;
hal::SerialHalImpl serial_hal{115200};
hal::SerialLogBackend log_backend{serial_hal};
LoggingService logger{log_backend, real_time};
hal::SdCardHalImpl sd_hal{hal::SD_CS_PIN};
hal::CameraHalImpl camera_hal;
hal::TimerHalImpl timer_hal;

// --- FreeRTOS mutex for shared GPIO 21 (LED / SD CS) ---
SemaphoreHandle_t sd_active_mutex = nullptr;

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

// --- Service-layer objects ---
SdController sd_controller{real_time, sd_hal, cs_signal};
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
    stream_service, web_server_hal_impl};

millis_t last_led_tick_time_ms = 0;

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
    real_time.delay_ms(5000);

    logger.info("MAIN", "Starting initialization");

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

    // --- Initialize SD card ---
    if (sd_controller.init_card() != hal::SdInitResult::SUCCESS)
    {
        logger.error("MAIN", "SD initialization failed");
        return;
    }
    logger.info("MAIN", "SD initialized");

    // --- Load config (needs SD card) ---
    uint8_t config_buf[ConfigService::CONFIG_BUFFER_SIZE];
    size_t bytes_read = sd_controller.read_sync(
        ConfigService::DEFAULT_CONFIG_PATH,
        config_buf, sizeof(config_buf));
    logger.info("MAIN", "Config read: %zu / %zu bytes",
                bytes_read, ConfigService::CONFIG_BUFFER_SIZE);
    config_service.load(config_buf, bytes_read);

    // --- Initialize camera with config-driven settings ---
    CameraImageConfig cam_image_config;
    cam_image_config.resolution = config_service.config().camera.resolution;
    cam_image_config.jpeg_quality = config_service.config().camera.jpeg_quality;
    cam_image_config.fb_count = 2;
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

    // --- Initialize web server ---
    web_server_service.init(config_service.config().web);

    // --- Initialize orchestrator (registers commands) ---
    system_orchestrator.init();

    // --- Register serial transport (after all commands are registered) ---
    command_handler.add_transport(serial_transport);

    // --- Register web command routes (after all commands are registered) ---
    web_transport.register_routes(config_service.config().web.password);

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
    if (now % 10000 < constants::LED_TICK_TIME_MS)
    {
        logger.info("MAIN", "Core temperature %.2f", temperatureRead());
        real_time.delay_ms(2);
    }
}
