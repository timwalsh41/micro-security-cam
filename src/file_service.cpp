#include "file_service.h"
#include "web_auth_util.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

/**
 * @brief Validates a date-directory segment ("/YYYYMMDD").
 */
static bool is_date_dir(const char* dir)
{
    if (dir == nullptr)
    {
        return false;
    }
    size_t len = strlen(dir);
    if (len != 9 || dir[0] != '/')
    {
        return false;
    }
    for (size_t i = 1; i <= 8; i++)
    {
        if (!isdigit(static_cast<unsigned char>(dir[i])))
        {
            return false;
        }
    }
    return true;
}

// ============================================================
// Construction / registration
// ============================================================

FileService::FileService(SdController& sd, CommandHandler& cmd_handler,
                         LoggingService& log)
    : m_sd(sd)
    , m_cmd_handler(cmd_handler)
    , m_log(log)
{
    m_password[0] = '\0';
}

void FileService::init(hal::WebServerHal& web, const char* web_password)
{
    // Store password for auth checks (register-time snapshot).
    if (web_password != nullptr)
    {
        strncpy(m_password, web_password, sizeof(m_password) - 1);
        m_password[sizeof(m_password) - 1] = '\0';
    }
    else
    {
        m_password[0] = '\0';
    }

    m_cmd_handler.register_handler(
        CommandCode::LIST_FILES,
        [this](const uint8_t* payload, size_t len,
               uint8_t* response, size_t& responseLen) -> CommandResult
        {
            return this->handle_list_files(payload, len, response, responseLen);
        });

    web.on("/files", hal::HttpMethod::GET,
           [this](hal::RequestRef& request)
           {
               this->handle_files_request(request);
           });

    web.on("/download", hal::HttpMethod::GET,
           [this](hal::RequestRef& request)
           {
               this->handle_download_request(request);
           });

    web.on("/logs", hal::HttpMethod::GET,
           [this](hal::RequestRef& request)
           {
               this->handle_logs_request(request);
           });

    web.on("/log", hal::HttpMethod::GET,
           [this](hal::RequestRef& request)
           {
               this->handle_log_request(request);
           });
}

void FileService::set_active_clip_provider(
    std::function<const char*()> provider)
{
    m_active_clip = std::move(provider);
}

// ============================================================
// Path validation
// ============================================================

bool FileService::is_valid_path(const char* path)
{
    if (path == nullptr || path[0] != '/')
    {
        return false;
    }
    if (strchr(path, '\\') != nullptr)
    {
        return false;
    }

    // Each '/'-separated segment must be non-empty and not "." or "..".
    const char* seg = path + 1;
    while (*seg != '\0')
    {
        const char* end = strchr(seg, '/');
        size_t len = (end != nullptr)
            ? static_cast<size_t>(end - seg)
            : strlen(seg);

        if (len == 0 ||
            (len == 1 && seg[0] == '.') ||
            (len == 2 && seg[0] == '.' && seg[1] == '.'))
        {
            return false;
        }

        if (end == nullptr)
        {
            break;
        }
        seg = end + 1;
        // A '/' immediately followed by the end of the string is a
        // trailing slash (empty final segment).
        if (*seg == '\0')
        {
            return false;
        }
    }
    return true;
}

bool FileService::is_log_file_name(const char* name)
{
    if (name == nullptr)
    {
        return false;
    }
    size_t len = strlen(name);
    if (len != 12 || strncmp(name + 8, ".log", 4) != 0)
    {
        return false;
    }
    for (size_t i = 0; i < 8; i++)
    {
        if (!isdigit(static_cast<unsigned char>(name[i])))
        {
            return false;
        }
    }
    return true;
}

bool FileService::is_allowed_listing_dir(const char* dir)
{
    if (!is_valid_path(dir))
    {
        return false;
    }
    if (strcmp(dir, "/") == 0 || strcmp(dir, "/logs") == 0)
    {
        return true;
    }
    return is_date_dir(dir);
}

bool FileService::is_allowed_download_path(const char* path)
{
    if (!is_valid_path(path))
    {
        return false;
    }

    // Daily log files: /logs/YYYYMMDD.log
    if (strncmp(path, "/logs/", 6) == 0)
    {
        const char* name = path + 6;
        return strchr(name, '/') == nullptr && is_log_file_name(name);
    }

    // Media files: /YYYYMMDD/<name> — exactly one path segment after
    // the date directory. Root-level files (e.g. /config.ini) never
    // match because there is no date-directory segment.
    const char* slash = strchr(path + 1, '/');
    if (slash == nullptr)
    {
        return false;
    }
    size_t dir_len = static_cast<size_t>(slash - path);
    if (dir_len > 9)
    {
        return false;
    }
    char dir_buf[16];
    strncpy(dir_buf, path, dir_len);
    dir_buf[dir_len] = '\0';
    if (!is_date_dir(dir_buf))
    {
        return false;
    }

    const char* name = slash + 1;
    return name[0] != '\0' && strchr(name, '/') == nullptr;
}

// ============================================================
// LIST_FILES dispatch command
// ============================================================

CommandResult FileService::handle_list_files(
    const uint8_t* payload, size_t len,
    uint8_t* response, size_t& responseLen)
{
    if (!m_sd.is_card_available())
    {
        return CommandResult::SD_UNAVAILABLE;
    }

    // Parse "<dir>[ <page>]" from the payload (may be non-terminated).
    char arg_buf[80];
    size_t copy_len = (len < sizeof(arg_buf) - 1) ? len : sizeof(arg_buf) - 1;
    if (payload != nullptr && copy_len > 0)
    {
        memcpy(arg_buf, payload, copy_len);
    }
    arg_buf[copy_len] = '\0';

    uint32_t page = 0;
    char* space = strrchr(arg_buf, ' ');
    if (space != nullptr && space[1] != '\0')
    {
        page = strtoul(space + 1, nullptr, 10);
        *space = '\0';
    }

    const char* dir = arg_buf;
    if (dir[0] == '\0')
    {
        dir = "/";
    }

    if (!is_allowed_listing_dir(dir))
    {
        m_log.warn(SUBSYSTEM, "LIST_FILES rejected path %s", dir);
        return CommandResult::INVALID_ARGS;
    }

    size_t written = build_listing_json(
        dir, page, reinterpret_cast<char*>(response),
        CommandHandler::MAX_RESPONSE_SIZE);
    if (written == 0)
    {
        return CommandResult::INTERNAL_ERROR;
    }

    responseLen = written;
    return CommandResult::SUCCESS;
}

// ============================================================
// Listing builder
// ============================================================

size_t FileService::build_listing_json(const char* dir, uint32_t page,
                                       char* buf, size_t max_len)
{
    struct EntryInfo
    {
        std::string name;
        size_t size;
        bool is_dir;
    };

    // Single CS-arbitrated directory scan collecting entry metadata.
    std::vector<EntryInfo> entries;
    bool ok = m_sd.list_dir_sync(dir,
        [&entries](const char* name, size_t size, bool is_dir)
        {
            if (name != nullptr)
            {
                entries.push_back({name, size, is_dir});
            }
            return true;
        });
    if (!ok)
    {
        return 0;
    }

    const size_t total = entries.size();
    const size_t first = static_cast<size_t>(page) * PAGE_SIZE;

    // Page header.
    int written = snprintf(buf, max_len,
                           "{\"dir\":\"%s\",\"page\":%u,\"total\":%u,\"entries\":[",
                           dir,
                           static_cast<unsigned>(page),
                           static_cast<unsigned>(total));
    if (written < 0 || static_cast<size_t>(written) >= max_len)
    {
        return 0;
    }
    size_t pos = static_cast<size_t>(written);

    // Active clip path for the recording flag (empty when not recording).
    const char* active = (m_active_clip != nullptr) ? m_active_clip() : "";

    size_t index = first;
    bool buffer_full = false;
    for (; index < total && index < first + PAGE_SIZE; index++)
    {
        const EntryInfo& entry = entries[index];

        // Full path of the entry, for the active-clip comparison.
        std::string full_path = (strcmp(dir, "/") == 0)
            ? "/" + entry.name
            : std::string(dir) + "/" + entry.name;
        bool recording = (active[0] != '\0') && (full_path == active);

        char entry_json[176];
        int n = snprintf(entry_json, sizeof(entry_json),
                         "%s{\"n\":\"%s\",\"s\":%lu,\"d\":%s,\"r\":%s}",
                         (index > first) ? "," : "",
                         entry.name.c_str(),
                         static_cast<unsigned long>(entry.size),
                         entry.is_dir ? "true" : "false",
                         recording ? "true" : "false");
        if (n < 0 || static_cast<size_t>(n) >= sizeof(entry_json))
        {
            continue; // Name too long for the JSON scratch buffer: skip.
        }

        // Keep room for the closing "]}" and trailer.
        if (pos + static_cast<size_t>(n) + 48 >= max_len)
        {
            buffer_full = true;
            break;
        }
        memcpy(buf + pos, entry_json, static_cast<size_t>(n));
        pos += static_cast<size_t>(n);
    }

    bool has_more = buffer_full || (index < total);

    int trailer = snprintf(buf + pos, max_len - pos,
                           "],\"has_more\":%s}",
                           has_more ? "true" : "false");
    if (trailer < 0 || static_cast<size_t>(trailer) >= max_len - pos)
    {
        return 0;
    }
    pos += static_cast<size_t>(trailer);

    return pos;
}

// ============================================================
// Route authentication
// ============================================================

bool FileService::check_auth(hal::RequestRef& request)
{
    if (m_password[0] == '\0')
    {
        // File access requires a configured web password: without one
        // there is no credential to gate on, so the routes are refused
        // outright (parity with the config-command policy).
        request.send(401, "application/json",
                     "{\"error\":\"authentication required\"}");
        return false;
    }

    if (!check_basic_auth(request.auth_header(), m_password))
    {
        request.send_unauthorized();
        m_log.warn(SUBSYSTEM, "Auth rejected: invalid or missing credentials");
        return false;
    }
    return true;
}

// ============================================================
// /files route
// ============================================================

void FileService::handle_files_request(hal::RequestRef& request)
{
    if (!check_auth(request))
    {
        return;
    }

    if (!m_sd.is_card_available())
    {
        request.send(503, "application/json",
                     "{\"error\":\"sd unavailable\"}");
        return;
    }

    const char* dir = request.arg("dir");
    if (dir == nullptr || dir[0] == '\0')
    {
        dir = "/";
    }
    if (!is_allowed_listing_dir(dir))
    {
        request.send(403, "application/json",
                     "{\"error\":\"path not allowed\"}");
        return;
    }

    uint32_t page = strtoul(request.arg("page"), nullptr, 10);

    char json[CommandHandler::MAX_RESPONSE_SIZE];
    size_t written = build_listing_json(dir, page, json, sizeof(json));
    if (written == 0)
    {
        request.send(404, "application/json",
                     "{\"error\":\"directory not found\"}");
        return;
    }

    request.send(200, "application/json", json);
}

// ============================================================
// /download route
// ============================================================

void FileService::handle_download_request(hal::RequestRef& request)
{
    if (!check_auth(request))
    {
        return;
    }

    if (!m_sd.is_card_available())
    {
        request.send(503, "application/json",
                     "{\"error\":\"sd unavailable\"}");
        return;
    }

    const char* path = request.arg("path");
    if (path == nullptr || path[0] == '\0')
    {
        request.send(400, "application/json",
                     "{\"error\":\"missing 'path' parameter\"}");
        return;
    }
    if (!is_allowed_download_path(path))
    {
        m_log.warn(SUBSYSTEM, "Download rejected path %s", path);
        request.send(403, "application/json",
                     "{\"error\":\"path not allowed\"}");
        return;
    }

    const char* active = (m_active_clip != nullptr) ? m_active_clip() : "";
    if (active[0] != '\0' && strcmp(path, active) == 0)
    {
        // The active clip's AVI header is unfinalized until close.
        request.send(409, "application/json",
                     "{\"error\":\"clip still recording\"}");
        return;
    }

    if (!m_sd.exists_sync(path))
    {
        request.send(404, "application/json",
                     "{\"error\":\"file not found\"}");
        return;
    }

    size_t total = m_sd.size_sync(path);
    if (total == 0)
    {
        // Existing but empty: a zero-length body is still a valid
        // download response.
        request.send(200, "application/octet-stream", "");
        return;
    }

    // Serve in bounded CS-arbitrated read transactions from the
    // transport's task context. The lambda owns its path copy, since
    // the request's arg buffer is only valid for the handler call.
    std::string owned_path(path);
    hal::DataFiller filler =
        [this, owned_path](uint8_t* buf, size_t max_len, size_t offset) -> size_t
        {
            size_t chunk = (max_len < DOWNLOAD_CHUNK_SIZE)
                ? max_len : DOWNLOAD_CHUNK_SIZE;
            return m_sd.read_at_sync(owned_path.c_str(),
                                     static_cast<uint32_t>(offset),
                                     buf, chunk);
        };

    if (!request.send_data_response("application/octet-stream", total, filler))
    {
        request.send(500, "application/json",
                     "{\"error\":\"download unavailable\"}");
        return;
    }

    m_log.info(SUBSYSTEM, "Download started: %s (%zu bytes)", path, total);
}

// ============================================================
// /logs and /log routes
// ============================================================

void FileService::handle_logs_request(hal::RequestRef& request)
{
    if (!check_auth(request))
    {
        return;
    }

    if (!m_sd.is_card_available())
    {
        request.send(503, "application/json",
                     "{\"error\":\"sd unavailable\"}");
        return;
    }

    char json[CommandHandler::MAX_RESPONSE_SIZE];
    size_t written = build_listing_json("/logs", 0, json, sizeof(json));
    if (written == 0)
    {
        request.send(404, "application/json",
                     "{\"error\":\"logs directory not found\"}");
        return;
    }

    request.send(200, "application/json", json);
}

void FileService::handle_log_request(hal::RequestRef& request)
{
    if (!check_auth(request))
    {
        return;
    }

    if (!m_sd.is_card_available())
    {
        request.send(503, "application/json",
                     "{\"error\":\"sd unavailable\"}");
        return;
    }

    const char* file = request.arg("file");
    if (file == nullptr || file[0] == '\0')
    {
        request.send(400, "application/json",
                     "{\"error\":\"missing 'file' parameter\"}");
        return;
    }
    if (!is_log_file_name(file))
    {
        request.send(403, "application/json",
                     "{\"error\":\"path not allowed\"}");
        return;
    }

    char path[32];
    snprintf(path, sizeof(path), "/logs/%s", file);

    if (!m_sd.exists_sync(path))
    {
        request.send(404, "application/json",
                     "{\"error\":\"file not found\"}");
        return;
    }

    size_t total = m_sd.size_sync(path);
    if (total == 0)
    {
        // Existing but empty log: empty text body.
        request.send(200, "text/plain", "");
        return;
    }

    // Tail-limited transfer: serve at most the last LOG_TAIL_MAX_BYTES
    // so a long-lived log cannot grow the response unboundedly.
    size_t base_offset = (total > LOG_TAIL_MAX_BYTES)
        ? total - LOG_TAIL_MAX_BYTES : 0;
    size_t tail_len = total - base_offset;

    std::string owned_path(path);
    hal::DataFiller filler =
        [this, owned_path, base_offset](uint8_t* buf, size_t max_len,
                                        size_t offset) -> size_t
        {
            size_t chunk = (max_len < DOWNLOAD_CHUNK_SIZE)
                ? max_len : DOWNLOAD_CHUNK_SIZE;
            return m_sd.read_at_sync(owned_path.c_str(),
                                     static_cast<uint32_t>(base_offset + offset),
                                     buf, chunk);
        };

    if (!request.send_data_response("text/plain", tail_len, filler))
    {
        request.send(500, "application/json",
                     "{\"error\":\"log unavailable\"}");
        return;
    }

    m_log.info(SUBSYSTEM, "Log viewed: %s (%zu bytes of tail)", path, tail_len);
}
