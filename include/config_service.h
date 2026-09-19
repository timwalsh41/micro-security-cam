#ifndef CONFIG_SERVICE_H
#define CONFIG_SERVICE_H

#include <cstdint>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include "ini_parser.h"
#include "logging_service.h"
#include "sd_controller.h"
#include "hal/nvs_hal.h"
#include "hal/time_hal.h"

/**
 * @file config_service.h
 * @brief Configuration service that parses INI config data and provides typed access.
 *
 * Reads a raw buffer (provided by the caller) and parses it into a
 * ConfigData struct. All I/O is handled externally — this service
 * owns only parsing, logging, and default-fallback behavior.
 *
 */

/**
 * @class ConfigService
 * @brief Parses INI configuration buffers and provides typed access to config values.
 *
 * Owns the parse + logging + defaults lifecycle. Does not perform any
 * file I/O — the caller provides the raw buffer.
 * 
 */
class ConfigService
{
    public:
        /**
         * @brief Construct a ConfigService with the given logger.
         *
         * @param logger LoggingService for status and diagnostic output.
         */
        explicit ConfigService(LoggingService& logger);

        /**
         * @brief Parses a raw buffer into the internal config.
         *
         * On success, logs key settings at INFO level.
         * On failure (empty/null/parse error), falls back to defaults and logs a warning.
         *
         * @param data   Pointer to the raw INI file content.
         * @param length Number of bytes in the buffer.
         * @return true if parsing succeeded, false if defaults were used.
         */
        bool load(const uint8_t* data, size_t length);

        /**
         * @brief Overlays a raw INI buffer onto the current config.
         *
         * Applies each setting recognized in the buffer on top of the
         * current config, setting-by-setting; settings absent from the
         * buffer retain their current values. Unknown keys and malformed
         * lines are ignored.
         *
         * @param data   Pointer to the raw INI-format content.
         * @param length Number of bytes in the buffer.
         * @return true if content was applied, false if the buffer was
         *         null or empty (config left unchanged).
         */
        bool overlay(const uint8_t* data, size_t length);

        /**
         * @brief Returns the parsed configuration.
         *
         * Before any call to load(), returns default config values.
         *
         * @return Const reference to the parsed ConfigData.
         */
        const ConfigData& config() const;

        /**
         * @brief Returns a mutable reference to the internal config.
         *
         * Allows callers to modify config fields in place. Call 
         * IniParser::serialize() afterward if the modified config
         * should be persisted to storage.
         *
         * @return Mutable reference to the internal ConfigData.
         */
        ConfigData& mutable_config();

        /**
         * @brief Reads a configuration value by "section.key" path.
         *
         * String settings are returned verbatim. Numeric and boolean
         * settings are formatted as they appear in serialized INI text
         * (decimal, with `camera.grab_mode` in two-digit hex and
         * `video.audio_enabled` as "true"/"false").
         *
         * @param key       Dot-separated section and key name (e.g. "wifi.ssid").
         * @param value_out Buffer to receive the value string.
         * @param max_len   Size of the output buffer.
         * @return true if the key was found and value copied.
         */
        bool get_config_value(const char* key,
                              char* value_out, size_t max_len) const;

        /**
         * @brief Writes a configuration value by "section.key" path.
         *
         * String settings accept any value that fits the field buffer.
         * Numeric and boolean settings are parsed from the string and
         * must satisfy the same constraints as the INI parser (for
         * example `web.port` 1–65535, `web.max_streams` 1–4,
         * `video.fps` 1–30, `video.mic_gain` 1–8, jpeg quality ≤ 63,
         * `camera.grab_mode` as hexadecimal ≤ 0xFF); out-of-range or
         * unparseable values are rejected and leave the config
         * unchanged. The change applies to the live in-RAM config only
         * and is not persisted.
         *
         * @param key   Dot-separated section and key name (e.g. "wifi.ssid").
         * @param value New value string.
         * @return true if the key was recognised and value was written.
         */
        bool set_config_value(const char* key, const char* value);

        /**
         * @brief Serializes the current config to INI-format text.
         *
         * All fields are always written (including empty strings), so a
         * round trip through serialize()/overlay() preserves every setting.
         *
         * @param buffer      Destination buffer for the INI text.
         * @param buffer_size Size of the destination buffer in bytes.
         * @return Number of bytes written (excluding null terminator).
         *         If the buffer is too small, returns the number of bytes
         *         that would have been written.
         */
        size_t serialize(char* buffer, size_t buffer_size) const;

        /**
         * @brief Loads config settings from NVS onto the current config.
         *
         * Reads the config blob from the NVS namespace and overlays it
         * setting-by-setting onto the current config. When no config is
         * saved in NVS the current config is left unchanged.
         *
         * A saved config whose schema version does not match the
         * firmware's (or that carries no readable schema version) is
         * erased and discarded without migration; the current config
         * retains its values (compiled-in defaults at boot).
         *
         * @param nvs NvsHal used for storage access.
         * @return true if a saved config was read and applied.
         */
        bool load_nvs(hal::NvsHal& nvs);

        /**
         * @brief Persists the current config to NVS.
         *
         * Serializes the live config first and validates that it fits
         * the config buffer before committing; the write is verified by
         * a read-back comparison. The in-RAM config is never modified
         * by persistence, so a failed save leaves it intact.
         *
         * @param nvs NvsHal used for storage access.
         * @return true if the config was committed and verified.
         */
        bool save_nvs(hal::NvsHal& nvs);

        /**
         * @brief Loads config settings from the SD card config file.
         *
         * Reads the config file and overlays it setting-by-setting onto
         * the current config. When the card is unavailable or the file
         * is missing/unreadable the current config is left unchanged.
         *
         * @param sd SdController used for storage access.
         * @return true if config content was read and applied.
         */
        bool load_sd(SdController& sd);

        /**
         * @brief Persists the current config to the SD card config file.
         *
         * Serializes the live config and validates that it fits the
         * config buffer before committing; the write replaces the file
         * and is verified by a read-back comparison. The in-RAM config
         * is never modified by persistence, so a failed write leaves it
         * intact.
         *
         * @param sd SdController used for storage access.
         * @return true if the config file was written and verified.
         */
        bool save_sd(SdController& sd);

        /**
         * @brief Maximum size of the config file read buffer.
         *
         * Log output includes bytes_read / CONFIG_BUFFER_SIZE so reviewers
         * can see how much margin is available.
         */
        static constexpr size_t CONFIG_BUFFER_SIZE = 1280;

        /**
         * @brief Default path for the config file on SD card.
         */
        static constexpr char DEFAULT_CONFIG_PATH[] = "/config.ini";

        /**
         * @brief Timeout for reading the config file from SD card (ms).
         */
        static constexpr hal::millis_t READ_TIMEOUT_MS = 5000;

        /**
         * @brief NVS namespace holding the persisted config blob.
         */
        static constexpr char NVS_NAMESPACE[] = "config";

        /**
         * @brief NVS key holding the persisted config blob.
         */
        static constexpr char NVS_KEY[] = "config";

        /**
         * @brief NVS key holding the config schema version.
         */
        static constexpr char NVS_SCHEMA_KEY[] = "schema_version";

        /**
         * @brief Current config schema version.
         *
         * Increment when the persisted config layout changes in a way
         * that cannot be overlaid; a saved config with a different
         * version is discarded (no migration) in favor of the
         * compiled-in defaults.
         */
        static constexpr uint32_t NVS_SCHEMA_VERSION = 2U;

    private:
        static constexpr const char* SUBSYSTEM = "CONF";
        LoggingService& m_logger;
        ConfigData m_config;

        /**
         * @brief Guards m_config against cross-task concurrent access.
         *
         * Command handlers run on different tasks (serial commands on
         * the main loop task, web command handlers on the async server
         * task), so every accessor that reads or writes m_config locks
         * this mutex.
         */
        mutable std::mutex m_mutex;
};

#endif // CONFIG_SERVICE_H
