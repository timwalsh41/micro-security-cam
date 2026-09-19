#include "ini_parser.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cctype>

// ============================================================
// Static methods
// ============================================================

ConfigData IniParser::defaults()
{
    return ConfigData{};
}

// ============================================================
// Helper methods for key/value parsing
// ============================================================

bool IniParser::key_match(const char* key_start, size_t key_len, const char* name)
{
    size_t name_len = strlen(name);
    return key_len == name_len && strncasecmp(key_start, name, name_len) == 0;
}

void IniParser::value_copy(const char* val_start, size_t val_len, char* buf, size_t buf_size)
{
    size_t copy_len = val_len < buf_size - 1 ? val_len : buf_size - 1;
    memcpy(buf, val_start, copy_len);
    buf[copy_len] = '\0';
}

void IniParser::get_string_val(const char* val_start, size_t val_len, char* dest, size_t dest_size)
{
    size_t copy_len = val_len < dest_size - 1 ? val_len : dest_size - 1;
    memcpy(dest, val_start, copy_len);
    dest[copy_len] = '\0';
}

uint32_t IniParser::get_uint_val(const char* val_start, size_t val_len, bool* valid, int base)
{
    char buf[16];
    value_copy(val_start, val_len, buf, sizeof(buf));
    char* endptr = nullptr;
    uint32_t result = strtoul(buf, &endptr, base);
    if (valid)
    {
        *valid = (*endptr == '\0');
    }
    return result;
}

bool IniParser::get_bool_val(const char* val_start, size_t val_len)
{
    if (val_len == 4 && strncasecmp(val_start, "true", 4) == 0)
    {
        return true;
    }
    if (val_len == 5 && strncasecmp(val_start, "false", 5) == 0)
    {
        return false;
    }
    return false;
}

ConfigData IniParser::parse(const char* content, size_t length)
{
    return parse_into(ConfigData{}, content, length);
}

ConfigData IniParser::overlay(const ConfigData& base,
                              const char* content, size_t length)
{
    return parse_into(base, content, length);
}

ConfigData IniParser::parse_into(ConfigData config,
                                 const char* content, size_t length)
{
    if (!content || length == 0)
    {
        return config;
    }

    // Track which section we're in
    enum class Section : uint8_t
    {
        NONE,
        WIFI,
        AP,
        WEB,
        CAMERA,
        VIDEO,
        LOGGING
    };

    Section current_section = Section::NONE;

    const char* pos = content;
    const char* end = content + length;

    while (pos < end)
    {
        // Find end of current line
        const char* line_end = pos;
        while (line_end < end && *line_end != '\n')
        {
            line_end++;
        }

        size_t line_len = static_cast<size_t>(line_end - pos);

        // Copy line to a working buffer (null-terminated)
        char line_buf[MAX_LINE_LENGTH];
        if (line_len >= sizeof(line_buf))
        {
            line_len = sizeof(line_buf) - 1;
        }
        memcpy(line_buf, pos, line_len);
        line_buf[line_len] = '\0';

        // Trim leading whitespace
        const char* p = line_buf;
        while (*p && isspace(static_cast<unsigned char>(*p)))
        {
            p++;
        }

        // Skip empty lines
        if (*p == '\0')
        {
            pos = line_end + 1;
            continue;
        }

        // Skip comment lines
        if (*p == '#' || *p == ';')
        {
            pos = line_end + 1;
            continue;
        }

        // Check for section header
        if (*p == '[')
        {
            p++;
            const char* section_end = strchr(p, ']');
            if (section_end)
            {
                size_t section_len = static_cast<size_t>(section_end - p);

                // Trim trailing whitespace from section name
                while (section_len > 0 &&
                       isspace(static_cast<unsigned char>(p[section_len - 1])))
                {
                    section_len--;
                }

                if (section_len == 4 && strncasecmp(p, "wifi", 4) == 0)
                {
                    current_section = Section::WIFI;
                }
                else if (section_len == 2 && strncasecmp(p, "ap", 2) == 0)
                {
                    current_section = Section::AP;
                }
                else if (section_len == 3 && strncasecmp(p, "web", 3) == 0)
                {
                    current_section = Section::WEB;
                }
                else if (section_len == 6 && strncasecmp(p, "camera", 6) == 0)
                {
                    current_section = Section::CAMERA;
                }
                else if (section_len == 5 && strncasecmp(p, "video", 5) == 0)
                {
                    current_section = Section::VIDEO;
                }
                else if (section_len == 7 && strncasecmp(p, "logging", 7) == 0)
                {
                    current_section = Section::LOGGING;
                }
                else
                {
                    current_section = Section::NONE;
                }
            }
            pos = line_end + 1;
            continue;
        }

        // Parse key=value pair
        const char* eq = strchr(p, '=');
        if (eq)
        {
            // Extract key (trim trailing whitespace)
            const char* key_start = p;
            size_t key_len = static_cast<size_t>(eq - key_start);
            while (key_len > 0 &&
                   isspace(static_cast<unsigned char>(key_start[key_len - 1])))
            {
                key_len--;
            }

            // Extract value (trim leading whitespace)
            const char* val_start = eq + 1;
            while (*val_start && isspace(static_cast<unsigned char>(*val_start)))
            {
                val_start++;
            }
            size_t val_len = strlen(val_start);
            // Trim trailing whitespace from value
            while (val_len > 0 &&
                   isspace(static_cast<unsigned char>(val_start[val_len - 1])))
            {
                val_len--;
            }

            // Assign based on section and key
            switch (current_section)
            {
                case Section::WIFI:
                {
                    if (key_match(key_start, key_len, "ssid"))
                    {
                        get_string_val(val_start, val_len, config.wifi.ssid, CONFIG_FIELD_MAX_LENGTH);
                    }
                    else if (key_match(key_start, key_len, "password"))
                    {
                        get_string_val(val_start, val_len, config.wifi.password, CONFIG_FIELD_MAX_LENGTH);
                    }
                    else if (key_match(key_start, key_len, "static_ip"))
                    {
                        get_string_val(val_start, val_len, config.wifi.static_ip, CONFIG_FIELD_MAX_LENGTH);
                    }
                    else if (key_match(key_start, key_len, "gateway"))
                    {
                        get_string_val(val_start, val_len, config.wifi.gateway, CONFIG_FIELD_MAX_LENGTH);
                    }
                    else if (key_match(key_start, key_len, "subnet_mask"))
                    {
                        get_string_val(val_start, val_len, config.wifi.subnet_mask, CONFIG_FIELD_MAX_LENGTH);
                    }
                    break;
                }
                case Section::AP:
                {
                    if (key_match(key_start, key_len, "ssid"))
                    {
                        get_string_val(val_start, val_len, config.ap.ssid, CONFIG_FIELD_MAX_LENGTH);
                    }
                    else if (key_match(key_start, key_len, "password"))
                    {
                        get_string_val(val_start, val_len, config.ap.password, CONFIG_FIELD_MAX_LENGTH);
                    }
                    else if (key_match(key_start, key_len, "ap_ip"))
                    {
                        get_string_val(val_start, val_len, config.ap.ap_ip, CONFIG_FIELD_MAX_LENGTH);
                    }
                    else if (key_match(key_start, key_len, "ap_gateway"))
                    {
                        get_string_val(val_start, val_len, config.ap.ap_gateway, CONFIG_FIELD_MAX_LENGTH);
                    }
                    else if (key_match(key_start, key_len, "ap_subnet_mask"))
                    {
                        get_string_val(val_start, val_len, config.ap.ap_subnet_mask, CONFIG_FIELD_MAX_LENGTH);
                    }
                    break;
                }
                case Section::WEB:
                {
                    if (key_match(key_start, key_len, "port"))
                    {
                        bool valid = false;
                        uint32_t parsed = get_uint_val(val_start, val_len, &valid);
                        if (valid && parsed >= 1 && parsed <= UINT16_MAX)
                        {
                            config.web.port = static_cast<uint16_t>(parsed);
                        }
                    }
                    else if (key_match(key_start, key_len, "password"))
                    {
                        get_string_val(val_start, val_len, config.web.password, CONFIG_FIELD_MAX_LENGTH);
                    }
                    else if (key_match(key_start, key_len, "max_streams"))
                    {
                        uint32_t parsed = get_uint_val(val_start, val_len);
                        if (parsed >= 1 && parsed <= 4)
                        {
                            config.web.max_streams = static_cast<uint8_t>(parsed);
                        }
                    }
                    break;
                }
                case Section::CAMERA:
                {
                    if (key_match(key_start, key_len, "resolution"))
                    {
                        get_string_val(val_start, val_len, config.camera.resolution, CONFIG_FIELD_MAX_LENGTH);
                    }
                    else if (key_match(key_start, key_len, "jpeg_quality"))
                    {
                        uint32_t parsed = get_uint_val(val_start, val_len);
                        if (parsed <= 63)
                        {
                            config.camera.jpeg_quality = static_cast<uint8_t>(parsed);
                        }
                    }
                    else if (key_match(key_start, key_len, "grab_mode"))
                    {
                        config.camera.grab_mode = static_cast<uint8_t>(get_uint_val(val_start, val_len, nullptr, 16));
                    }
                    else if (key_match(key_start, key_len, "rotation"))
                    {
                        // Only 0/180 are accepted; 90/270 are not supported.
                        uint32_t parsed = get_uint_val(val_start, val_len);
                        if (parsed == 0 || parsed == 180)
                        {
                            config.camera.rotation = static_cast<uint16_t>(parsed);
                        }
                    }
                    break;
                }
                case Section::VIDEO:
                {
                    if (key_match(key_start, key_len, "fps"))
                    {
                        uint32_t parsed = get_uint_val(val_start, val_len);
                        if (parsed >= 1 && parsed <= 30)
                        {
                            config.video.fps = static_cast<uint16_t>(parsed);
                        }
                    }
                    else if (key_match(key_start, key_len, "resolution"))
                    {
                        get_string_val(val_start, val_len, config.video.resolution, CONFIG_FIELD_MAX_LENGTH);
                    }
                    else if (key_match(key_start, key_len, "clip_duration_s"))
                    {
                        config.video.clip_duration_s = static_cast<uint16_t>(get_uint_val(val_start, val_len));
                    }
                    else if (key_match(key_start, key_len, "max_file_size_mb"))
                    {
                        config.video.max_file_size_mb = static_cast<uint16_t>(get_uint_val(val_start, val_len));
                    }
                    else if (key_match(key_start, key_len, "jpeg_quality"))
                    {
                        uint32_t parsed = get_uint_val(val_start, val_len);
                        if (parsed <= 63)
                        {
                            config.video.jpeg_quality = static_cast<uint8_t>(parsed);
                        }
                    }
                    else if (key_match(key_start, key_len, "audio_enabled"))
                    {
                        config.video.audio_enabled = get_bool_val(val_start, val_len);
                    }
                    else if (key_match(key_start, key_len, "mic_gain"))
                    {
                        uint32_t parsed = get_uint_val(val_start, val_len);
                        if (parsed >= 1 && parsed <= 8)
                        {
                            config.video.mic_gain = static_cast<uint8_t>(parsed);
                        }
                    }
                    break;
                }
                case Section::LOGGING:
                {
                    if (key_match(key_start, key_len, "sd_enabled"))
                    {
                        config.logging.sd_enabled = get_bool_val(val_start, val_len);
                    }
                    else if (key_match(key_start, key_len, "serial_enabled"))
                    {
                        config.logging.serial_enabled = get_bool_val(val_start, val_len);
                    }
                    else if (key_match(key_start, key_len, "serial_min_level"))
                    {
                        get_string_val(val_start, val_len, config.logging.serial_min_level, CONFIG_FIELD_MAX_LENGTH);
                    }
                    else if (key_match(key_start, key_len, "sd_min_level"))
                    {
                        get_string_val(val_start, val_len, config.logging.sd_min_level, CONFIG_FIELD_MAX_LENGTH);
                    }
                    else if (key_match(key_start, key_len, "retention_days"))
                    {
                        uint32_t parsed = get_uint_val(val_start, val_len);
                        if (parsed >= 1 && parsed <= 255)
                        {
                            config.logging.retention_days = static_cast<uint8_t>(parsed);
                        }
                    }
                    break;
                }
                case Section::NONE:
                    break;
            }
        }

        pos = line_end + 1;
    }

    return config;
}

size_t IniParser::serialize(const ConfigData& data,
                            char* buffer,
                            size_t buffer_size)
{
    size_t written = 0;

    // Helper: write a string to buffer, tracking offset.
    auto write = [&](const char* str)
    {
        size_t len = strlen(str);
        if (buffer && written + len < buffer_size)
        {
            memcpy(buffer + written, str, len);
        }
        written += len;
    };

    // [wifi]
    write("[wifi]\n");
    write("ssid=");
    write(data.wifi.ssid);
    write("\n");
    write("password=");
    write(data.wifi.password);
    write("\n");
    write("static_ip=");
    write(data.wifi.static_ip);
    write("\n");
    write("gateway=");
    write(data.wifi.gateway);
    write("\n");
    write("subnet_mask=");
    write(data.wifi.subnet_mask);
    write("\n");

    // [ap]
    write("[ap]\n");
    write("ssid=");
    write(data.ap.ssid);
    write("\n");
    write("password=");
    write(data.ap.password);
    write("\n");
    write("ap_ip=");
    write(data.ap.ap_ip);
    write("\n");
    write("ap_gateway=");
    write(data.ap.ap_gateway);
    write("\n");
    write("ap_subnet_mask=");
    write(data.ap.ap_subnet_mask);
    write("\n");

    // [web]
    write("[web]\n");
    char num_buf[16];
    snprintf(num_buf, sizeof(num_buf), "%u", data.web.port);
    write("port=");
    write(num_buf);
    write("\n");
    write("password=");
    write(data.web.password);
    write("\n");
    snprintf(num_buf, sizeof(num_buf), "%u", data.web.max_streams);
    write("max_streams=");
    write(num_buf);
    write("\n");

    // [camera]
    write("[camera]\n");
    write("resolution=");
    write(data.camera.resolution);
    write("\n");
    snprintf(num_buf, sizeof(num_buf), "%u", data.camera.jpeg_quality);
    write("jpeg_quality=");
    write(num_buf);
    write("\n");
    snprintf(num_buf, sizeof(num_buf), "%02x", data.camera.grab_mode);
    write("grab_mode=");
    write(num_buf);
    write("\n");
    snprintf(num_buf, sizeof(num_buf), "%u", data.camera.rotation);
    write("rotation=");
    write(num_buf);
    write("\n");

    // [video]
    write("[video]\n");
    snprintf(num_buf, sizeof(num_buf), "%u", data.video.fps);
    write("fps=");
    write(num_buf);
    write("\n");
    write("resolution=");
    write(data.video.resolution);
    write("\n");
    snprintf(num_buf, sizeof(num_buf), "%u", data.video.clip_duration_s);
    write("clip_duration_s=");
    write(num_buf);
    write("\n");
    snprintf(num_buf, sizeof(num_buf), "%u", data.video.max_file_size_mb);
    write("max_file_size_mb=");
    write(num_buf);
    write("\n");
    snprintf(num_buf, sizeof(num_buf), "%u", data.video.jpeg_quality);
    write("jpeg_quality=");
    write(num_buf);
    write("\n");
    write("audio_enabled=");
    write(data.video.audio_enabled ? "true" : "false");
    write("\n");
    snprintf(num_buf, sizeof(num_buf), "%u", data.video.mic_gain);
    write("mic_gain=");
    write(num_buf);
    write("\n");

    // [logging]
    write("[logging]\n");
    write("sd_enabled=");
    write(data.logging.sd_enabled ? "true" : "false");
    write("\n");
    write("serial_enabled=");
    write(data.logging.serial_enabled ? "true" : "false");
    write("\n");
    write("serial_min_level=");
    write(data.logging.serial_min_level);
    write("\n");
    write("sd_min_level=");
    write(data.logging.sd_min_level);
    write("\n");
    snprintf(num_buf, sizeof(num_buf), "%u", data.logging.retention_days);
    write("retention_days=");
    write(num_buf);
    write("\n");

    // Null-terminate if buffer has room
    if (buffer && written < buffer_size)
    {
        buffer[written] = '\0';
    }

    return written;
}
