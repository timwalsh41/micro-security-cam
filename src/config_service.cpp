#include "config_service.h"
#include "ini_parser.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <mutex>
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
    // Logging
    if (strcmp(key, "logging.serial_min_level") == 0) return cfg.logging.serial_min_level;
    if (strcmp(key, "logging.sd_min_level") == 0)     return cfg.logging.sd_min_level;
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
    // Logging
    else if (strcmp(key, "logging.serial_min_level") == 0) dest = cfg.logging.serial_min_level;
    else if (strcmp(key, "logging.sd_min_level") == 0)     dest = cfg.logging.sd_min_level;

    if (dest == nullptr)
    {
        return false;
    }

    size_t val_len = strnlen(value, CONFIG_FIELD_MAX_LENGTH - 1);
    memcpy(dest, value, val_len);
    dest[val_len] = '\0';
    return true;
}

/**
 * @brief Case-insensitive string equality.
 */
bool ci_equals(const char* a, const char* b)
{
    while (*a != '\0' && *b != '\0')
    {
        if (tolower(static_cast<unsigned char>(*a)) !=
            tolower(static_cast<unsigned char>(*b)))
        {
            return false;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

/**
 * @brief Parses a decimal unsigned integer, rejecting trailing garbage.
 */
bool parse_uint_value(const char* value, uint32_t& out)
{
    if (value == nullptr || value[0] == '\0')
    {
        return false;
    }
    char* end = nullptr;
    unsigned long parsed = strtoul(value, &end, 10);
    if (end == nullptr || *end != '\0')
    {
        return false;
    }
    out = static_cast<uint32_t>(parsed);
    return true;
}

/**
 * @brief Parses a hexadecimal unsigned integer, rejecting trailing garbage.
 */
bool parse_hex_value(const char* value, uint32_t& out)
{
    if (value == nullptr || value[0] == '\0')
    {
        return false;
    }
    char* end = nullptr;
    unsigned long parsed = strtoul(value, &end, 16);
    if (end == nullptr || *end != '\0')
    {
        return false;
    }
    out = static_cast<uint32_t>(parsed);
    return true;
}

/**
 * @brief Parses a boolean ("true"/"false", case-insensitive).
 */
bool parse_bool_value(const char* value, bool& out)
{
    if (ci_equals(value, "true"))
    {
        out = true;
        return true;
    }
    if (ci_equals(value, "false"))
    {
        out = false;
        return true;
    }
    return false;
}

/**
 * @brief Formats a numeric/boolean "section.key" setting into a string
 *        exactly as serialized INI text would render it. Returns false
 *        if the key is not a typed setting.
 */
bool read_typed_field(const ConfigData& cfg, const char* key,
                      char* value_out, size_t max_len)
{
    if (value_out == nullptr || max_len == 0)
    {
        return false;
    }

    if (strcmp(key, "web.port") == 0)
    {
        snprintf(value_out, max_len, "%u", cfg.web.port);
        return true;
    }
    if (strcmp(key, "web.max_streams") == 0)
    {
        snprintf(value_out, max_len, "%u", cfg.web.max_streams);
        return true;
    }
    if (strcmp(key, "camera.jpeg_quality") == 0)
    {
        snprintf(value_out, max_len, "%u", cfg.camera.jpeg_quality);
        return true;
    }
    if (strcmp(key, "camera.grab_mode") == 0)
    {
        snprintf(value_out, max_len, "%02x", cfg.camera.grab_mode);
        return true;
    }
    if (strcmp(key, "camera.rotation") == 0)
    {
        snprintf(value_out, max_len, "%u", cfg.camera.rotation);
        return true;
    }
    if (strcmp(key, "video.fps") == 0)
    {
        snprintf(value_out, max_len, "%u", cfg.video.fps);
        return true;
    }
    if (strcmp(key, "video.clip_duration_s") == 0)
    {
        snprintf(value_out, max_len, "%u", cfg.video.clip_duration_s);
        return true;
    }
    if (strcmp(key, "video.max_file_size_mb") == 0)
    {
        snprintf(value_out, max_len, "%u", cfg.video.max_file_size_mb);
        return true;
    }
    if (strcmp(key, "video.jpeg_quality") == 0)
    {
        snprintf(value_out, max_len, "%u", cfg.video.jpeg_quality);
        return true;
    }
    if (strcmp(key, "video.audio_enabled") == 0)
    {
        snprintf(value_out, max_len, "%s",
                 cfg.video.audio_enabled ? "true" : "false");
        return true;
    }
    if (strcmp(key, "video.mic_gain") == 0)
    {
        snprintf(value_out, max_len, "%u", cfg.video.mic_gain);
        return true;
    }
    if (strcmp(key, "logging.sd_enabled") == 0)
    {
        switch (cfg.logging.sd_enabled)
        {
            case SdEnabledMode::TRUE:          snprintf(value_out, max_len, "true"); break;
            case SdEnabledMode::FALSE:         snprintf(value_out, max_len, "false"); break;
            case SdEnabledMode::AFTER_SET_TIME: snprintf(value_out, max_len, "after_set_time"); break;
        }
        return true;
    }
    if (strcmp(key, "logging.serial_enabled") == 0)
    {
        snprintf(value_out, max_len, "%s", cfg.logging.serial_enabled ? "true" : "false");
        return true;
    }
    if (strcmp(key, "logging.retention_days") == 0)
    {
        snprintf(value_out, max_len, "%u", cfg.logging.retention_days);
        return true;
    }
    return false;
}

/**
 * @brief Parses a value string and writes it into the typed
 *        "section.key" setting, applying the same constraints as the
 *        INI parser. Returns false if the key is unknown or the value
 *        is rejected, leaving the config unchanged.
 */
bool write_typed_field(ConfigData& cfg, const char* key, const char* value)
{
    uint32_t parsed = 0;
    bool parsed_bool = false;

    if (strcmp(key, "web.port") == 0)
    {
        if (!parse_uint_value(value, parsed) ||
            parsed < 1 || parsed > UINT16_MAX)
        {
            return false;
        }
        cfg.web.port = static_cast<uint16_t>(parsed);
        return true;
    }
    if (strcmp(key, "web.max_streams") == 0)
    {
        if (!parse_uint_value(value, parsed) ||
            parsed < 1 || parsed > 4)
        {
            return false;
        }
        cfg.web.max_streams = static_cast<uint8_t>(parsed);
        return true;
    }
    if (strcmp(key, "camera.jpeg_quality") == 0)
    {
        if (!parse_uint_value(value, parsed) || parsed > 63)
        {
            return false;
        }
        cfg.camera.jpeg_quality = static_cast<uint8_t>(parsed);
        return true;
    }
    if (strcmp(key, "camera.grab_mode") == 0)
    {
        if (!parse_hex_value(value, parsed) || parsed > UINT8_MAX)
        {
            return false;
        }
        cfg.camera.grab_mode = static_cast<uint8_t>(parsed);
        return true;
    }
    if (strcmp(key, "camera.rotation") == 0)
    {
        // Only 0/180 are accepted; 90/270 are not supported.
        if (!parse_uint_value(value, parsed) ||
            (parsed != 0 && parsed != 180))
        {
            return false;
        }
        cfg.camera.rotation = static_cast<uint16_t>(parsed);
        return true;
    }
    if (strcmp(key, "video.fps") == 0)
    {
        if (!parse_uint_value(value, parsed) ||
            parsed < 1 || parsed > 30)
        {
            return false;
        }
        cfg.video.fps = static_cast<uint16_t>(parsed);
        return true;
    }
    if (strcmp(key, "video.clip_duration_s") == 0)
    {
        if (!parse_uint_value(value, parsed) || parsed > UINT16_MAX)
        {
            return false;
        }
        cfg.video.clip_duration_s = static_cast<uint16_t>(parsed);
        return true;
    }
    if (strcmp(key, "video.max_file_size_mb") == 0)
    {
        if (!parse_uint_value(value, parsed) || parsed > UINT16_MAX)
        {
            return false;
        }
        cfg.video.max_file_size_mb = static_cast<uint16_t>(parsed);
        return true;
    }
    if (strcmp(key, "video.jpeg_quality") == 0)
    {
        if (!parse_uint_value(value, parsed) || parsed > 63)
        {
            return false;
        }
        cfg.video.jpeg_quality = static_cast<uint8_t>(parsed);
        return true;
    }
    if (strcmp(key, "video.audio_enabled") == 0)
    {
        if (!parse_bool_value(value, parsed_bool))
        {
            return false;
        }
        cfg.video.audio_enabled = parsed_bool;
        return true;
    }
    if (strcmp(key, "video.mic_gain") == 0)
    {
        if (!parse_uint_value(value, parsed) ||
            parsed < 1 || parsed > 8)
        {
            return false;
        }
        cfg.video.mic_gain = static_cast<uint8_t>(parsed);
        return true;
    }
    if (strcmp(key, "logging.sd_enabled") == 0)
    {
        if (ci_equals(value, "true"))
        {
            cfg.logging.sd_enabled = SdEnabledMode::TRUE;
            return true;
        }
        if (ci_equals(value, "false"))
        {
            cfg.logging.sd_enabled = SdEnabledMode::FALSE;
            return true;
        }
        if (ci_equals(value, "after_set_time"))
        {
            cfg.logging.sd_enabled = SdEnabledMode::AFTER_SET_TIME;
            return true;
        }
        return false;
    }
    if (strcmp(key, "logging.serial_enabled") == 0)
    {
        if (!parse_bool_value(value, parsed_bool))
        {
            return false;
        }
        cfg.logging.serial_enabled = parsed_bool;
        return true;
    }
    if (strcmp(key, "logging.retention_days") == 0)
    {
        if (!parse_uint_value(value, parsed) ||
            parsed < 1 || parsed > 255)
        {
            return false;
        }
        cfg.logging.retention_days = static_cast<uint8_t>(parsed);
        return true;
    }
    return false;
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
    std::lock_guard<std::mutex> lock(m_mutex);
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

    std::lock_guard<std::mutex> lock(m_mutex);
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
    std::lock_guard<std::mutex> lock(m_mutex);

    if (read_typed_field(m_config, key, value_out, max_len))
    {
        return true;
    }

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
    std::lock_guard<std::mutex> lock(m_mutex);

    if (write_typed_field(m_config, key, value))
    {
        return true;
    }
    return write_field(m_config, key, value);
}

// ============================================================
// Serialization and persistence
// ============================================================

size_t ConfigService::serialize(char* buffer, size_t buffer_size) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
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
