#include "config_service.h"
#include <cstring>

// Out-of-class definition for ODR-use (C++11 constexpr string member)
constexpr char ConfigService::DEFAULT_CONFIG_PATH[];

// ============================================================
// Construction
// ============================================================

ConfigService::ConfigService(LoggingService& logger)
    : m_logger(logger)
{
    m_config = IniParser::defaults();
}

// ============================================================
// Public interface
// ============================================================

bool ConfigService::load(const uint8_t* data, size_t length)
{
    m_logger.info(SUBSYSTEM, "Parsing config: %zu bytes", length);

    if (!data || length == 0)
    {
        m_logger.warn(SUBSYSTEM, "No config data, using defaults");
        m_config = IniParser::defaults();
        return false;
    }

    m_config = IniParser::parse(reinterpret_cast<const char*>(data), length);

    // Log parsed settings at INFO level
    if (m_config.wifi.ssid[0] != '\0')
    {
        m_logger.info(SUBSYSTEM, "WiFi SSID: %s", m_config.wifi.ssid);
    }
    m_logger.info(SUBSYSTEM, "AP SSID: %s",
                  m_config.ap.ssid[0] != '\0' ? m_config.ap.ssid : "(not set)");
    m_logger.info(SUBSYSTEM, "Web port: %u", m_config.web.port);
    m_logger.info(SUBSYSTEM, "Capture interval: %u ms",
                  m_config.camera.capture_interval_ms);
    m_logger.info(SUBSYSTEM, "Video: %ufps %s, clip %us, quality %u",
                  m_config.video.fps, m_config.video.resolution,
                  m_config.video.clip_duration_s, m_config.video.jpeg_quality);

    return true;
}

const ConfigData& ConfigService::config() const
{
    return m_config;
}

ConfigData& ConfigService::mutable_config()
{
    return m_config;
}
