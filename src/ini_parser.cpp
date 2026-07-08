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

ConfigData IniParser::parse(const char* content, size_t length)
{
    ConfigData config{};

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
        VIDEO
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
                    if (key_len == 4 && strncasecmp(key_start, "ssid", 4) == 0)
                    {
                        memcpy(config.wifi.ssid, val_start,
                               val_len < CONFIG_FIELD_MAX_LENGTH - 1
                                   ? val_len
                                   : CONFIG_FIELD_MAX_LENGTH - 1);
                    }
                    else if (key_len == 8 && strncasecmp(key_start, "password", 8) == 0)
                    {
                        memcpy(config.wifi.password, val_start,
                               val_len < CONFIG_FIELD_MAX_LENGTH - 1
                                   ? val_len
                                   : CONFIG_FIELD_MAX_LENGTH - 1);
                    }
                    break;
                }
                case Section::AP:
                {
                    if (key_len == 4 && strncasecmp(key_start, "ssid", 4) == 0)
                    {
                        memcpy(config.ap.ssid, val_start,
                               val_len < CONFIG_FIELD_MAX_LENGTH - 1
                                   ? val_len
                                   : CONFIG_FIELD_MAX_LENGTH - 1);
                    }
                    else if (key_len == 8 && strncasecmp(key_start, "password", 8) == 0)
                    {
                        memcpy(config.ap.password, val_start,
                               val_len < CONFIG_FIELD_MAX_LENGTH - 1
                                   ? val_len
                                   : CONFIG_FIELD_MAX_LENGTH - 1);
                    }
                    break;
                }
                case Section::WEB:
                {
                    if (key_len == 4 && strncasecmp(key_start, "port", 4) == 0)
                    {
                        char val_buf[16];
                        size_t copy_len = val_len < sizeof(val_buf) - 1
                                              ? val_len
                                              : sizeof(val_buf) - 1;
                        memcpy(val_buf, val_start, copy_len);
                        val_buf[copy_len] = '\0';
                        char* endptr;
                        uint32_t parsed = static_cast<uint32_t>(strtoul(val_buf, &endptr, 10));
                        if (*endptr == '\0' && parsed <= UINT16_MAX)
                        {
                            config.web.port = static_cast<uint16_t>(parsed);
                        }
                    }
                    else if (key_len == 8 && strncasecmp(key_start, "password", 8) == 0)
                    {
                        memcpy(config.web.password, val_start,
                               val_len < CONFIG_FIELD_MAX_LENGTH - 1
                                   ? val_len
                                   : CONFIG_FIELD_MAX_LENGTH - 1);
                    }
                    break;
                }
                case Section::CAMERA:
                {
                    if (key_len == 19 &&
                        strncasecmp(key_start, "capture_interval_ms", 19) == 0)
                    {
                        char val_buf[16];
                        size_t copy_len = val_len < sizeof(val_buf) - 1
                                              ? val_len
                                              : sizeof(val_buf) - 1;
                        memcpy(val_buf, val_start, copy_len);
                        val_buf[copy_len] = '\0';
                        config.camera.capture_interval_ms =
                            static_cast<uint32_t>(strtoul(val_buf, nullptr, 10));
                    }
                    break;
                }
                case Section::VIDEO:
                {
                    if (key_len == 3 &&
                        strncasecmp(key_start, "fps", 3) == 0)
                    {
                        char val_buf[16];
                        size_t copy_len = val_len < sizeof(val_buf) - 1
                                              ? val_len
                                              : sizeof(val_buf) - 1;
                        memcpy(val_buf, val_start, copy_len);
                        val_buf[copy_len] = '\0';
                        uint32_t parsed = strtoul(val_buf, nullptr, 10);
                        if (parsed >= 1 && parsed <= 30)
                        {
                            config.video.fps = static_cast<uint16_t>(parsed);
                        }
                    }
                    else if (key_len == 10 &&
                             strncasecmp(key_start, "resolution", 10) == 0)
                    {
                        size_t copy_len = val_len < CONFIG_FIELD_MAX_LENGTH - 1
                                              ? val_len
                                              : CONFIG_FIELD_MAX_LENGTH - 1;
                        memcpy(config.video.resolution, val_start, copy_len);
                        config.video.resolution[copy_len] = '\0';
                    }
                    else if (key_len == 15 &&
                             strncasecmp(key_start, "clip_duration_s", 15) == 0)
                    {
                        char val_buf[16];
                        size_t copy_len = val_len < sizeof(val_buf) - 1
                                              ? val_len
                                              : sizeof(val_buf) - 1;
                        memcpy(val_buf, val_start, copy_len);
                        val_buf[copy_len] = '\0';
                        config.video.clip_duration_s =
                            static_cast<uint16_t>(strtoul(val_buf, nullptr, 10));
                    }
                    else if (key_len == 16 &&
                             strncasecmp(key_start, "max_file_size_mb", 16) == 0)
                    {
                        char val_buf[16];
                        size_t copy_len = val_len < sizeof(val_buf) - 1
                                              ? val_len
                                              : sizeof(val_buf) - 1;
                        memcpy(val_buf, val_start, copy_len);
                        val_buf[copy_len] = '\0';
                        config.video.max_file_size_mb =
                            static_cast<uint16_t>(strtoul(val_buf, nullptr, 10));
                    }
                    else if (key_len == 12 &&
                             strncasecmp(key_start, "jpeg_quality", 12) == 0)
                    {
                        char val_buf[16];
                        size_t copy_len = val_len < sizeof(val_buf) - 1
                                              ? val_len
                                              : sizeof(val_buf) - 1;
                        memcpy(val_buf, val_start, copy_len);
                        val_buf[copy_len] = '\0';
                        uint32_t parsed = strtoul(val_buf, nullptr, 10);
                        if (parsed <= 63)
                        {
                            config.video.jpeg_quality = static_cast<uint8_t>(parsed);
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

    // [ap]
    write("[ap]\n");
    write("ssid=");
    write(data.ap.ssid);
    write("\n");
    write("password=");
    write(data.ap.password);
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

    // [camera]
    write("[camera]\n");
    snprintf(num_buf, sizeof(num_buf), "%u", data.camera.capture_interval_ms);
    write("capture_interval_ms=");
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

    // Null-terminate if buffer has room
    if (buffer && written < buffer_size)
    {
        buffer[written] = '\0';
    }

    return written;
}
