#include "camera_runner.h"
#include "hal/time_hal.h"
#include "platform_initializer.h"
#include "hal/camera_hal_impl.h"
#include "hal/serial_hal_impl.h"
#include "hal/serial_log_backend.h"
#include "hal/multi_log_backend.h"
#include "hal/sd_log_writer.h"
#include "hal/sd_card_hal_impl.h"
#include "hal/wifi_hal_impl.h"
#include "hal/web_server_hal_impl.h"
#include "hal/nvs_hal_impl.h"
#include "hal/timer_hal_impl.h"
#include "hal/restart_hal_impl.h"
#include "hal/gpio_hal.h"
#include "hal/hardware_peripheral_manager_impl.h"
#include "hal/mic_hal_impl.h"
#include "hal/mic_task_runner_esp32.h"
#include "hal/memory_allocator_esp32.h"
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
#include "file_service.h"
#include "command_handler.h"
#include "web_command_transport.h"
#include "serial_command_transport.h"
#include "system_orchestrator.h"
#include "stream_service.h"
#include "version.h"
#include <memory>
#include <strings.h>

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
    constexpr millis_t SD_LOG_FLUSH_TIME_MS = 1000;
}

/**
 * @brief Maps a configured level name to a LogLevel (unknown names
 *        default to INFO).
 */
static hal::LogLevel parse_log_level(const char* name)
{
    if (name != nullptr)
    {
        if (strcasecmp(name, "debug") == 0)
        {
            return hal::LogLevel::DEBUG;
        }
        if (strcasecmp(name, "warn") == 0 ||
            strcasecmp(name, "warning") == 0)
        {
            return hal::LogLevel::WARNING;
        }
        if (strcasecmp(name, "error") == 0)
        {
            return hal::LogLevel::ERROR;
        }
        if (strcasecmp(name, "info") == 0)
        {
            return hal::LogLevel::INFO;
        }
    }
    return hal::LogLevel::INFO;
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

        // The serial sink attaches first so every log line from here
        // on reaches the console; the SD sink attaches below once the
        // logging configuration has loaded.
        m_multi_log_backend.add_backend(&m_serial_log_backend,
                                        hal::LogLevel::DEBUG);

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

        // --- Construct the SD log sink when card logging is enabled ---
        // SdEnabledMode::TRUE attaches immediately; AFTER_SET_TIME
        // attaches from loop() once the wall clock has been set;
        // FALSE never constructs the sink.
        const ConfigData::LoggingConfig& logging_config =
            m_config_service.config().logging;
        if (logging_config.sd_enabled != SdEnabledMode::FALSE &&
            m_sd_controller.is_card_available())
        {
            m_sd_log_writer.reset(new SdLogWriter(
                m_sd_controller, m_real_time,
                parse_log_level(logging_config.sd_min_level),
                logging_config.retention_days));
            if (logging_config.sd_enabled == SdEnabledMode::TRUE)
            {
                attach_sd_log_sink();
            }
            else
            {
                m_sd_log_gated_on_time = true;
            }
        }

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

        // --- Register file browser/download/log routes and LIST_FILES ---
        // Route registration must precede the web server start (below)
        // so the full route table attaches before requests are served.
        m_file_service.set_active_clip_provider(
            [this]() -> const char*
            {
                return m_video_controller.is_recording()
                    ? m_video_controller.current_clip_path()
                    : "";
            });
        m_file_service.init(m_web_server_hal_impl,
                            m_config_service.config().web.password);

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

        // Open the after_set_time gate once the wall clock is set;
        // the opening line is flushed immediately so /logs serves it
        // without waiting for the periodic flush.
        if (m_sd_log_gated_on_time && !m_sd_log_sink_attached &&
            m_real_time.is_time_set())
        {
            m_sd_log_gated_on_time = false;
            attach_sd_log_sink();
            m_logger.info("RUNNER", "SD logging started "
                         "(sd_enabled=after_set_time)");
            m_multi_log_backend.tick();
            m_last_log_flush_time_ms = m_real_time.now();
        }

        // Periodic flush/rotation tick for the buffered SD log sink.
        if (m_sd_log_sink_attached &&
            m_real_time.elapsed_ms(m_last_log_flush_time_ms,
                                   m_real_time.now()) >=
                constants::SD_LOG_FLUSH_TIME_MS)
        {
            m_multi_log_backend.tick();
            m_last_log_flush_time_ms = m_real_time.now();
        }

        m_real_time.delay_us(constants::MAIN_LOOP_TIME_US);
    }

private:
    CameraRunnerImpl() = default;

    /**
     * @brief Attaches the SD log sink to the log fan-out (once).
     */
    void attach_sd_log_sink()
    {
        if (m_sd_log_writer == nullptr || m_sd_log_sink_attached)
        {
            return;
        }
        // The writer applies its own configured minimum level.
        m_multi_log_backend.add_backend(m_sd_log_writer.get(),
                                        hal::LogLevel::DEBUG);
        m_sd_log_sink_attached = true;
    }

    // --- Hardware Peripheral Manager (created internally) ---
    HardwarePeripheralManagerImpl m_hw_periph;

    // --- HAL implementations ---
    RealTimeService m_real_time;
    hal::SerialHalImpl m_serial_hal{115200};
    hal::SerialLogBackend m_serial_log_backend{m_serial_hal};
    MultiLogBackend m_multi_log_backend;
    LoggingService m_logger{m_multi_log_backend, m_real_time};
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
    MicTaskRunnerEsp32 m_mic_task_runner;
    MemoryAllocatorEsp32 m_mic_allocator;
    MicService m_mic_service{m_mic_hal, m_logger, m_config_service,
                              m_mic_task_runner, m_mic_allocator};
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
    FileService m_file_service{m_sd_controller, m_command_handler, m_logger};
    hal::RestartHalImpl m_restart_hal;
    SystemOrchestrator m_system_orchestrator{
        m_video_controller, m_camera_ctrl, m_command_handler,
        m_real_time, m_config_service, m_logger,
        m_stream_service, m_web_server_hal_impl,
        m_sd_controller, m_nvs_hal_impl, m_restart_hal, m_hw_periph};

    // --- Global state variables ---
    millis_t m_last_log_heartbeat_time_ms = 0;
    millis_t m_last_log_flush_time_ms = 0;

    /** SD log sink; constructed during init() once config has loaded. */
    std::unique_ptr<SdLogWriter> m_sd_log_writer;
    bool m_sd_log_sink_attached{false};
    bool m_sd_log_gated_on_time{false};
};

void CameraRunner::init()
{
    CameraRunnerImpl::instance().init();
}

void CameraRunner::loop()
{
    CameraRunnerImpl::instance().loop();
}