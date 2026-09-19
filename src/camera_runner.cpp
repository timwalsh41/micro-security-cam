#include "camera_runner.h"
#include "hal/time_hal.h"
#include "platform_initializer.h"
#include "hal/camera_hal_impl.h"
#include "hal/serial_hal_impl.h"
#include "hal/serial_log_backend.h"
#include "hal/sd_card_hal_impl.h"
#include "hal/wifi_hal_impl.h"
#include "hal/web_server_hal_impl.h"
#include "hal/nvs_hal_impl.h"
#include "hal/timer_hal_impl.h"
#include "hal/gpio_hal.h"
#include "hal/hardware_peripheral_manager_impl.h"
#include "hal/mic_hal_impl.h"
#include "camera_controller.h"
#include "video_controller.h"
#include "capture_scheduler_esp32.h"
#include "time_service.h"
#include "logging_service.h"
#include "sd_controller.h"
#include "led_state_machine.h"
#include "config_service.h"
#include "mic_service.h"
#include "wifi_service.h"
#include "web_server_service.h"
#include "command_handler.h"
#include "web_command_transport.h"
#include "serial_command_transport.h"
#include "system_orchestrator.h"
#include "stream_service.h"
#include "version.h"

using hal::millis_t;

/**
 * @file camera_runner.cpp
 * @brief Singleton implementation owning all application objects.
 *
 * This singleton encapsulates the entire application initialization and
 * loop logic. All objects are private members, not in an anonymous namespace.
 * The HardwarePeripheralManager is created internally (no injection).
 */

namespace constants
{
    constexpr millis_t MAIN_LOOP_TIME_US = 250;
    constexpr millis_t LED_TICK_TIME_MS = 5;
    constexpr millis_t LOG_HEARTBEAT_TIME_MS = 30000;
}

/**
 * @class CameraRunnerImpl
 * @brief Singleton implementation owning all application objects.
 */
class CameraRunnerImpl
{
public:
    /**
     * @brief Returns the singleton instance.
     *
     * Created on first call; returned on subsequent calls.
     * The instance is a local static to ensure thread-safe
     * initialization in C++11.
     */
    static CameraRunnerImpl& instance()
    {
        static CameraRunnerImpl impl;
        return impl;
    }

    /**
     * @brief Initializes the application (equivalent to original setup()).
     */
    void init()
    {
        m_platform_init.init();

        // Add delay for PlatformIO terminal to connect
        m_real_time.delay_ms(10000);

        m_logger.info("RUNNER", "Starting initialization");
        
        char version_string[32] = "unknown";
        if (version::get_version_string(version_string, 32))
        {
            m_logger.info("RUNNER", "MicroCam %s", version_string);
        }
        else
        {
            m_logger.warn("RUNNER", "MicroCam: Unable to retrieve version");
        }

        // --- Initialize GPIO and LED ---
        m_hw_periph.setup_shared_pin();

        // Initial blink sequence using the mutex-guarded LED
        m_led_sm.set_mode(LedMode::BLINK, 250, 250);
        {
            auto now = m_real_time.now();
            while (TimeService::elapsed_ms(now, m_real_time.now()) < 3000)
            {
                m_hw_periph.update_led(m_led_sm.tick());
                m_real_time.delay_ms(constants::LED_TICK_TIME_MS);
            }
        }
        m_led_sm.set_mode(LedMode::OFF);
        m_hw_periph.update_led(m_led_sm.tick());

        // --- Initialize SD card (optional at boot) ---
        hal::SdInitResult sd_result = m_sd_controller.init_card();
        if (sd_result == hal::SdInitResult::NO_SD_CARD)
        {
            m_logger.warn("RUNNER", "No SD card detected - "
                        "entering streaming-only mode");
        }
        else if (sd_result != hal::SdInitResult::SUCCESS)
        {
            m_logger.error("RUNNER", "SD initialization failed (%u) - "
                         "entering streaming-only mode",
                         static_cast<unsigned>(sd_result));
        }

        // --- Load config: compiled-in defaults overlaid from NVS, then ---
        // --- from the SD card config file when the card is readable.  ---
        // Precedence: defaults < NVS < SD file.
        m_config_service.load_nvs(m_nvs_hal_impl);
        m_config_service.load_sd(m_sd_controller);

        // --- Initialize camera with config-driven settings ---
        CameraImageConfig cam_image_config;
        cam_image_config.resolution = m_config_service.config().camera.resolution;
        cam_image_config.jpeg_quality = m_config_service.config().camera.jpeg_quality;
        cam_image_config.fb_count = 2;
        cam_image_config.rotation = m_config_service.config().camera.rotation;
        ResolvedCameraParams cam_params = build_camera_config(cam_image_config, m_logger);
        hal::CameraInitResult cam_result = m_camera_ctrl.init(cam_params);
        if (cam_result != hal::CameraInitResult::SUCCESS)
        {
            m_logger.error("RUNNER", "Camera initialization failed");
            return;
        }
        m_logger.info("RUNNER", "Camera initialized (%ux%u)",
                    m_camera_ctrl.get_params().width,
                    m_camera_ctrl.get_params().height);

        // --- Initialize MicService if audio is enabled ---
        if (m_config_service.config().video.audio_enabled)
        {
            if (m_mic_service.init())
            {
                m_video_controller.set_mic_service(&m_mic_service);
                m_logger.info("RUNNER", "MicService initialized");
            }
            else
            {
                m_logger.warn("RUNNER", "MicService initialization failed - audio disabled");
            }
        }

        // --- Initialize video controller with validated dimensions ---
        m_video_controller.init(m_config_service.config().video,
                              m_camera_ctrl.get_params().width,
                              m_camera_ctrl.get_params().height);

        // --- Initialize stream service and wire it into the video controller ---
        m_stream_service.init(m_config_service.config().web.max_streams);
        m_video_controller.set_stream_service(&m_stream_service);

        // --- Initialize WiFi ---
        m_wifi_service.init(m_config_service.config().wifi,
                          m_config_service.config().ap,
                          30);

        // --- Initialize orchestrator (registers commands and the /stream route) ---
        m_system_orchestrator.init();

        // --- Register serial transport (after all commands are registered) ---
        m_command_handler.add_transport(m_serial_transport);

        // --- Register web command routes ---
        m_web_transport.register_routes(m_config_service.config().web.password);

        // --- Initialize web server ---
        m_web_server_service.init(m_config_service.config().web);

        m_logger.info("RUNNER", "Initialization complete");
    }

    /**
     * @brief Runs one iteration of the application loop.
     */
    void loop()
    {
        // Poll command transports (web, serial)
        m_command_handler.tick();

        // Orchestrator tick (schedule evaluation, mode transitions)
        m_system_orchestrator.tick();

        // LED state machine tick (non-blocking, skips if SD owns pin)
        bool led_on = m_led_sm.tick();
        m_hw_periph.update_led(led_on);

        // Periodic heartbeat log
        hal::millis_t now = m_real_time.now();
        if (m_real_time.elapsed_ms(m_last_log_heartbeat_time_ms, m_real_time.now()) >= constants::LOG_HEARTBEAT_TIME_MS)
        {
            m_logger.info("RUNNER", "Core temperature %.2f", m_hw_periph.read_temperature());
            m_last_log_heartbeat_time_ms = m_real_time.now();
            m_last_log_heartbeat_time_ms -= m_last_log_heartbeat_time_ms % constants::LOG_HEARTBEAT_TIME_MS;
        }

        m_real_time.delay_us(constants::MAIN_LOOP_TIME_US);
    }

private:
    CameraRunnerImpl() = default;

    // --- Hardware Peripheral Manager (created internally) ---
    HardwarePeripheralManagerImpl m_hw_periph;

    // --- HAL implementations ---
    RealTimeService m_real_time;
    hal::SerialHalImpl m_serial_hal{115200};
    hal::SerialLogBackend m_log_backend{m_serial_hal};
    LoggingService m_logger{m_log_backend, m_real_time};
    hal::SdCardHalImpl m_sd_hal{hal::SD_CS_PIN};
    hal::CameraHalImpl m_camera_hal;
    hal::TimerHalImpl m_timer_hal;
    hal::NvsHalImpl m_nvs_hal_impl;

    // --- Service-layer objects ---
    SdController m_sd_controller{m_real_time, m_sd_hal,
        [this](peripherals::SdSignal signal) {
            m_hw_periph.cs_signal(signal);
        }};
    LedStateMachine m_led_sm{m_real_time};
    CameraController m_camera_ctrl{m_camera_hal};
    CaptureSchedulerEsp32 m_capture_scheduler{m_timer_hal};
    ConfigService m_config_service{m_logger};
    hal::MicHalImpl m_mic_hal;
    MicService m_mic_service{m_mic_hal, m_logger, m_config_service};
    VideoController m_video_controller{m_camera_ctrl, m_sd_controller, m_real_time, m_logger, m_capture_scheduler, &m_mic_service};
    PlatformInitializer m_platform_init;
    hal::WifiHalImpl m_wifi_hal_impl;
    hal::WebServerHalImpl m_web_server_hal_impl;
    WifiService m_wifi_service{m_wifi_hal_impl, m_real_time, m_logger};
    CommandHandler m_command_handler{m_logger};
    WebCommandTransport m_web_transport{m_web_server_hal_impl, m_command_handler};
    SerialCommandTransport m_serial_transport{m_serial_hal, m_real_time};
    WebServerService m_web_server_service{m_web_server_hal_impl, m_logger};
    StreamService m_stream_service{m_logger};
    SystemOrchestrator m_system_orchestrator{
        m_video_controller, m_camera_ctrl, m_command_handler,
        m_real_time, m_config_service, m_logger,
        m_stream_service, m_web_server_hal_impl,
        m_sd_controller, m_nvs_hal_impl};

    // --- Global state variables ---
    millis_t m_last_log_heartbeat_time_ms = 0;
};

void CameraRunner::init()
{
    CameraRunnerImpl::instance().init();
}

void CameraRunner::loop()
{
    CameraRunnerImpl::instance().loop();
}