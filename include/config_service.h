#ifndef CONFIG_SERVICE_H
#define CONFIG_SERVICE_H

#include <cstdint>
#include <cstddef>
#include "ini_parser.h"
#include "logging_service.h"
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
         * @brief Maximum size of the config file read buffer.
         *
         * Log output includes bytes_read / CONFIG_BUFFER_SIZE so reviewers
         * can see how much margin is available.
         */
        static constexpr size_t CONFIG_BUFFER_SIZE = 512;

        /**
         * @brief Default path for the config file on SD card.
         */
        static constexpr char DEFAULT_CONFIG_PATH[] = "/config.ini";

        /**
         * @brief Timeout for reading the config file from SD card (ms).
         */
        static constexpr hal::millis_t READ_TIMEOUT_MS = 5000;

    private:
        static constexpr const char* SUBSYSTEM = "CONF";
        LoggingService& m_logger;
        ConfigData m_config;
};

#endif // CONFIG_SERVICE_H
