#include "config_service.h"
#include <cstring>

// Out-of-class definition for ODR-use (C++11 constexpr string member)
constexpr char ConfigService::DEFAULT_CONFIG_PATH[];

// ============================================================
// Anonymous helpers — "section.key" to ConfigData field mapping
// ============================================================

namespace
{

/**
 * @brief Matches a "section.key" string and returns a const pointer
 *        to the char buffer, or nullptr if not found.
 */
const char* field_ptr(const ConfigData& cfg, const char* key)
{
    // WiFi
    if (strcmp(key, "wifi.ssid") == 0)          return cfg.wifi.ssid;
    if (strcmp(key, "wifi.password") == 0)      return cfg.wifi.password;
    if (strcmp(key, "wifi.static_ip") == 0)     return cfg.wifi.static_ip;
    if (strcmp(key, "wifi.gateway") == 0)       return cfg.wifi.gateway;
    if (strcmp(key, "wifi.subnet_mask") == 0)   return cfg.wifi.subnet_mask;
    // AP
    if (strcmp(key, "ap.ssid") == 0)            return cfg.ap.ssid;
    if (strcmp(key, "ap.password") == 0)        return cfg.ap.password;
    if (strcmp(key, "ap.ap_ip") == 0)           return cfg.ap.ap_ip;
    if (strcmp(key, "ap.ap_gateway") == 0)      return cfg.ap.ap_gateway;
    if (strcmp(key, "ap.ap_subnet_mask") == 0)  return cfg.ap.ap_subnet_mask;
    // Camera
    if (strcmp(key, "camera.resolution") == 0)  return cfg.camera.resolution;
    // Video
    if (strcmp(key, "video.resolution") == 0)   return cfg.video.resolution;
    // Web
    if (strcmp(key, "web.password") == 0)       return cfg.web.password;
    return nullptr;
}

/**
 * @brief Matches a "section.key" string and writes value into the
 *        corresponding mutable buffer. Returns false if unknown.
 */
bool write_field(ConfigData& cfg, const char* key, const char* value)
{
    char* dest = nullptr;

    // WiFi
    if (strcmp(key, "wifi.ssid") == 0)              dest = cfg.wifi.ssid;
    else if (strcmp(key, "wifi.password") == 0)     dest = cfg.wifi.password;
    else if (strcmp(key, "wifi.static_ip") == 0)    dest = cfg.wifi.static_ip;
    else if (strcmp(key, "wifi.gateway") == 0)      dest = cfg.wifi.gateway;
    else if (strcmp(key, "wifi.subnet_mask") == 0)  dest = cfg.wifi.subnet_mask;
    // AP
    else if (strcmp(key, "ap.ssid") == 0)           dest = cfg.ap.ssid;
    else if (strcmp(key, "ap.password") == 0)       dest = cfg.ap.password;
    else if (strcmp(key, "ap.ap_ip") == 0)          dest = cfg.ap.ap_ip;
    else if (strcmp(key, "ap.ap_gateway") == 0)     dest = cfg.ap.ap_gateway;
    else if (strcmp(key, "ap.ap_subnet_mask") == 0) dest = cfg.ap.ap_subnet_mask;
    // Camera
    else if (strcmp(key, "camera.resolution") == 0) dest = cfg.camera.resolution;
    // Video
    else if (strcmp(key, "video.resolution") == 0)  dest = cfg.video.resolution;
    // Web
    else if (strcmp(key, "web.password") == 0)      dest = cfg.web.password;

    if (dest == nullptr)
    {
        return false;
    }

    size_t val_len = strnlen(value, CONFIG_FIELD_MAX_LENGTH - 1);
    memcpy(dest, value, val_len);
    dest[val_len] = '\0';
    return true;
}

} // anonymous namespace

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

// ============================================================
// Config field access — single source of truth for field mapping
// ============================================================

bool ConfigService::get_config_value(
    const char* key, char* value_out, size_t max_len) const
{
    const char* src = field_ptr(m_config, key);
    if (src == nullptr)
    {
        return false;
    }

    size_t len = strnlen(src, CONFIG_FIELD_MAX_LENGTH);
    if (len >= max_len)
    {
        len = max_len - 1;
    }
    memcpy(value_out, src, len);
    value_out[len] = '\0';
    return true;
}

bool ConfigService::set_config_value(const char* key, const char* value)
{
    return write_field(m_config, key, value);
}
