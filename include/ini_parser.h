#ifndef INI_PARSER_H
#define INI_PARSER_H

#include <cstdint>
#include <cstddef>

/**
 * @file ini_parser.h
 * @brief INI-style configuration file parser and data structures.
 *
 * Provides a stateless, dependency-free parser for INI-format configuration
 * files. Supports sections, key-value pairs, comments, and whitespace
 * trimming.
 *
 * Example config file:
 * @code
 *   [wifi]
 *   ssid=MyNetwork
 *   password=MyPassword
 *
 *   [camera]
 *   capture_interval_ms=20000
 * @endcode
 *
 * Unknown keys and sections are silently ignored. Missing keys retain
 * their default values. Malformed lines are skipped gracefully.
 * Parsing never fails fatally — at worst, defaults are returned.
 */

/**
 * @brief Maximum length of a string field (including null terminator).
 */
static constexpr size_t CONFIG_FIELD_MAX_LENGTH = 64;

/**
 * @brief Parsed configuration data with typed sub-structs for each section.
 *
 * All string fields are fixed-size char arrays with null terminators.
 * Default values are safe for production use without any config file.
 */
struct ConfigData
{
    /**
     * @brief WiFi station mode configuration.
     */
    struct WifiConfig
    {
        char ssid[CONFIG_FIELD_MAX_LENGTH]{};
        char password[CONFIG_FIELD_MAX_LENGTH]{};
    };

    /**
     * @brief WiFi access point fallback configuration.
     */
    struct ApConfig
    {
        char ssid[CONFIG_FIELD_MAX_LENGTH]{};
        char password[CONFIG_FIELD_MAX_LENGTH]{};
    };

    /**
     * @brief Web server configuration.
     */
    struct WebConfig
    {
        uint16_t port{80};
        char password[CONFIG_FIELD_MAX_LENGTH]{};
    };

    /**
     * @brief Camera capture configuration.
     */
    struct CameraConfig
    {
        uint32_t capture_interval_ms{20000};
    };

    /**
     * @brief Video recording configuration.
     *
     * Resolution is stored as a string (e.g. "VGA", "HD") to keep
     * ConfigData platform-independent. The VideoController maps the
     * string to the platform-specific framesize_t enum at runtime.
     */
    struct VideoConfig
    {
        uint16_t fps{10};
        char resolution[CONFIG_FIELD_MAX_LENGTH]{"VGA"};
        uint16_t clip_duration_s{60};
        uint16_t max_file_size_mb{0};
        uint8_t jpeg_quality{10};
    };

    WifiConfig wifi;
    ApConfig ap;
    WebConfig web;
    CameraConfig camera;
    VideoConfig video;
};

/**
 * @class IniParser
 * @brief Stateless utility for parsing INI-format configuration data.
 *
 * All methods are static — no instance state is needed. The parser
 * operates on a raw buffer (no file I/O) and returns a populated
 * ConfigData struct with defaults for any missing values.
 */
class IniParser
{
    public:
        /**
         * @brief Returns a ConfigData pre-populated with safe defaults.
         *
         * @return ConfigData with all default values (empty strings, port 80,
         *         capture interval 20000 ms).
         */
        static ConfigData defaults();

        /**
         * @brief Parses an INI-format buffer into a ConfigData struct.
         *
         * Missing keys retain their defaults. Unknown sections and keys
         * are silently skipped. Malformed lines (no '=') are ignored.
         *
         * @param content Pointer to the INI file content.
         * @param length  Number of bytes in the content buffer.
         * @return Populated ConfigData with parsed values and defaults.
         */
        static ConfigData parse(const char* content, size_t length);

        /**
         * @brief Serializes a ConfigData struct into INI-format text.
         *
         * Writes a complete INI file representation to the provided buffer.
         * The output is round-trip compatible with parse() — serializing
         * a ConfigData and re-parsing it yields identical values.
         *
         * All fields are always written, including empty strings.
         * The output is null-terminated.
         *
         * @param data        The ConfigData to serialize.
         * @param buffer      Destination buffer for the INI text.
         * @param buffer_size Size of the destination buffer in bytes.
         * @return Number of bytes written (excluding null terminator).
         *         If buffer is null or too small, returns the number of
         *         bytes that would have been written.
         */
        static size_t serialize(const ConfigData& data,
                                char* buffer,
                                size_t buffer_size);

    private:
        // Maximum line length supported by the parser (excluding null terminator)
        static constexpr size_t MAX_LINE_LENGTH = 128;
};

#endif // INI_PARSER_H
