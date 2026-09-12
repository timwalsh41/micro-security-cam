#include "config_service.h"
#include "ini_parser.h"
#include <cstring>

// Out-of-class definitions for ODR-use (C++11 constexpr string members)
constexpr char ConfigService::DEFAULT_CONFIG_PATH[];
constexpr char ConfigService::NVS_NAMESPACE[];
constexpr char ConfigService::NVS_KEY[];
constexpr char ConfigService::NVS_SCHEMA_KEY[];

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

bool ConfigService::overlay(const uint8_t* data, size_t length)
{
    if (!data || length == 0)
    {
        m_logger.warn(SUBSYSTEM, "Overlay: no config content, config unchanged");
        return false;
    }

    m_config = IniParser::overlay(
        m_config, reinterpret_cast<const char*>(data), length);

    m_logger.info(SUBSYSTEM, "Config overlay applied: %zu bytes", length);
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

// ============================================================
// Serialization and persistence
// ============================================================

size_t ConfigService::serialize(char* buffer, size_t buffer_size) const
{
    return IniParser::serialize(m_config, buffer, buffer_size);
}

bool ConfigService::load_nvs(hal::NvsHal& nvs)
{
    if (!nvs.begin(NVS_NAMESPACE))
    {
        m_logger.warn(SUBSYSTEM, "NVS not available, config overlay skipped");
        return false;
    }

    uint8_t buf[CONFIG_BUFFER_SIZE];
    size_t bytes_read = nvs.read_blob(NVS_KEY, buf, sizeof(buf));
    if (bytes_read == 0)
    {
        m_logger.info(SUBSYSTEM, "No saved config in NVS");
        return false;
    }

    // Schema gate: a saved config without a readable schema version or
    // with a version other than the firmware's is incompatible. It is
    // erased (no migration) and the current config - the compiled-in
    // defaults at boot - is kept.
    uint32_t saved_version = 0;
    size_t version_len = nvs.read_blob(
        NVS_SCHEMA_KEY,
        reinterpret_cast<uint8_t*>(&saved_version),
        sizeof(saved_version));
    if (version_len != sizeof(saved_version))
    {
        m_logger.warn(SUBSYSTEM,
                      "Discarded NVS config: no schema version "
                      "(expected %u)",
                      static_cast<unsigned>(NVS_SCHEMA_VERSION));
        nvs.erase_key(NVS_KEY);
        nvs.erase_key(NVS_SCHEMA_KEY);
        return false;
    }
    if (saved_version != NVS_SCHEMA_VERSION)
    {
        m_logger.warn(SUBSYSTEM,
                      "Discarded NVS config with schema version %u "
                      "(expected %u)",
                      static_cast<unsigned>(saved_version),
                      static_cast<unsigned>(NVS_SCHEMA_VERSION));
        nvs.erase_key(NVS_KEY);
        nvs.erase_key(NVS_SCHEMA_KEY);
        return false;
    }

    m_logger.info(SUBSYSTEM, "NVS config read: %zu bytes", bytes_read);
    return overlay(buf, bytes_read);
}

bool ConfigService::save_nvs(hal::NvsHal& nvs)
{
    // Serialize and validate first: commit only a config that fits the
    // storage buffer. Serialization reads the live config without
    // modifying it, so a failure here leaves the RAM config intact.
    uint8_t buf[CONFIG_BUFFER_SIZE];
    size_t serialized_len = serialize(reinterpret_cast<char*>(buf), sizeof(buf));
    if (serialized_len >= sizeof(buf))
    {
        m_logger.error(SUBSYSTEM,
                       "Serialized config too large for NVS (%zu bytes)",
                       serialized_len);
        return false;
    }

    if (!nvs.begin(NVS_NAMESPACE))
    {
        m_logger.error(SUBSYSTEM, "NVS not available, config save failed");
        return false;
    }

    if (!nvs.write_blob(NVS_KEY, buf, serialized_len))
    {
        m_logger.error(SUBSYSTEM, "NVS config write failed");
        return false;
    }

    uint32_t schema_version = NVS_SCHEMA_VERSION;
    if (!nvs.write_blob(NVS_SCHEMA_KEY,
                        reinterpret_cast<uint8_t*>(&schema_version),
                        sizeof(schema_version)))
    {
        m_logger.error(SUBSYSTEM, "NVS schema version write failed");
        return false;
    }

    // Verify the committed blob by read-back comparison.
    uint8_t verify_buf[CONFIG_BUFFER_SIZE];
    size_t verify_len = nvs.read_blob(NVS_KEY, verify_buf, sizeof(verify_buf));
    if (verify_len != serialized_len ||
        memcmp(verify_buf, buf, serialized_len) != 0)
    {
        m_logger.error(SUBSYSTEM, "NVS config read-back verification failed");
        return false;
    }

    m_logger.info(SUBSYSTEM, "Config saved to NVS (%zu bytes)", serialized_len);
    return true;
}

bool ConfigService::load_sd(SdController& sd)
{
    uint8_t buf[CONFIG_BUFFER_SIZE];
    size_t bytes_read = sd.read_sync(DEFAULT_CONFIG_PATH, buf, sizeof(buf));
    if (bytes_read == 0)
    {
        m_logger.warn(SUBSYSTEM, "No config file read from SD");
        return false;
    }

    m_logger.info(SUBSYSTEM, "SD config read: %zu / %zu bytes",
                  bytes_read, CONFIG_BUFFER_SIZE);
    return overlay(buf, bytes_read);
}

bool ConfigService::save_sd(SdController& sd)
{
    // Serialize and validate first: commit only a config that fits the
    // storage buffer. Serialization reads the live config without
    // modifying it, so a failure here leaves the RAM config intact.
    uint8_t buf[CONFIG_BUFFER_SIZE];
    size_t serialized_len = serialize(reinterpret_cast<char*>(buf), sizeof(buf));
    if (serialized_len >= sizeof(buf))
    {
        m_logger.error(SUBSYSTEM,
                       "Serialized config too large for SD (%zu bytes)",
                       serialized_len);
        return false;
    }

    // WRITE mode truncates any existing config file.
    if (!sd.write_sync(DEFAULT_CONFIG_PATH, buf, serialized_len,
                       hal::FileOpenMode::WRITE))
    {
        m_logger.error(SUBSYSTEM, "SD config file write failed");
        return false;
    }

    // Verify the committed file by read-back comparison.
    uint8_t verify_buf[CONFIG_BUFFER_SIZE];
    size_t verify_len = sd.read_sync(DEFAULT_CONFIG_PATH, verify_buf,
                                     sizeof(verify_buf));
    if (verify_len != serialized_len ||
        memcmp(verify_buf, buf, serialized_len) != 0)
    {
        m_logger.error(SUBSYSTEM, "SD config read-back verification failed");
        return false;
    }

    m_logger.info(SUBSYSTEM, "Config saved to SD: %s (%zu bytes)",
                  DEFAULT_CONFIG_PATH, serialized_len);
    return true;
}
