#include "system_orchestrator.h"
#include "stream_service.h"
#include "web_auth_util.h"
#include "time_formatter.h"
#include "version.h"
#include "hal/time_hal.h"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <inttypes.h>
#include <memory>
#include <utility>

namespace
{

/** Minimum interval between SD storage-stat refreshes for GET_STATUS. */
constexpr hal::millis_t SD_STATS_TTL_MS = 60000;

} // namespace

SystemOrchestrator::SystemOrchestrator(VideoController& video,
                                       CameraController& camera,
                                       CommandHandler& cmd_handler,
                                       TimeService& time,
                                       ConfigService& config,
                                       LoggingService& log,
                                       StreamService& streams,
                                       hal::WebServerHal& web,
                                       SdController& sd,
                                       hal::NvsHal& nvs,
                                       hal::RestartHal& restart,
                                       HardwarePeripheralManager& hw_periph)
    : m_video(video)
    , m_camera(camera)
    , m_cmd_handler(cmd_handler)
    , m_time(time)
    , m_config(config)
    , m_log(log)
    , m_streams(streams)
    , m_web(web)
    , m_sd(sd)
    , m_nvs(nvs)
    , m_restart(restart)
    , m_hw_periph(hw_periph)
{
}

void SystemOrchestrator::init()
{
    // Latch SD availability once at initialization: a missing card and
    // a failed card initialization both latch streaming-only mode.
    // Recovery is insert-card-then-power-cycle (late insertion is not
    // re-detected at runtime).
    m_sd_available = m_sd.is_card_available();
    if (!m_sd_available)
    {
        m_log.warn(SUBSYSTEM, "SD unavailable - streaming-only mode "
                   "(recording and photo capture disabled)");
    }

    register_commands();
    register_stream_route();
    register_update_route();
    m_log.info(SUBSYSTEM, "SystemOrchestrator initialized");
}

bool SystemOrchestrator::is_sd_available() const
{
    return m_sd_available;
}

void SystemOrchestrator::tick()
{
    // Placeholder — schedule management and mode transitions
}

void SystemOrchestrator::register_commands()
{
    m_cmd_handler.register_handler(
        CommandCode::GET_STATUS,
        [this](const uint8_t* payload, size_t len,
               uint8_t* response, size_t& responseLen) -> CommandResult
        {
            return this->handle_get_status(payload, len, response, responseLen);
        });

    m_cmd_handler.register_handler(
        CommandCode::START_RECORDING,
        [this](const uint8_t* payload, size_t len,
               uint8_t* response, size_t& responseLen) -> CommandResult
        {
            return this->handle_start_recording(payload, len,
                                                response, responseLen);
        });

    m_cmd_handler.register_handler(
        CommandCode::STOP_RECORDING,
        [this](const uint8_t* payload, size_t len,
               uint8_t* response, size_t& responseLen) -> CommandResult
        {
            return this->handle_stop_recording(payload, len,
                                               response, responseLen);
        });

    m_cmd_handler.register_handler(
        CommandCode::TAKE_PHOTO,
        [this](const uint8_t* payload, size_t len,
               uint8_t* response, size_t& responseLen) -> CommandResult
        {
            return this->handle_take_photo(payload, len, response, responseLen);
        });

    m_cmd_handler.register_handler(
        CommandCode::SET_TIME,
        [this](const uint8_t* payload, size_t len,
               uint8_t* response, size_t& responseLen) -> CommandResult
        {
            return this->handle_set_time(payload, len, response, responseLen);
        });

    m_cmd_handler.register_handler(
        CommandCode::SET_CONFIG,
        [this](const uint8_t* payload, size_t len,
               uint8_t* response, size_t& responseLen) -> CommandResult
        {
            return this->handle_set_config(payload, len, response, responseLen);
        });

    m_cmd_handler.register_handler(
        CommandCode::GET_CONFIG,
        [this](const uint8_t* payload, size_t len,
               uint8_t* response, size_t& responseLen) -> CommandResult
        {
            return this->handle_get_config(payload, len, response, responseLen);
        });

    m_cmd_handler.register_handler(
        CommandCode::GET_CONFIG_VALUE,
        [this](const uint8_t* payload, size_t len,
               uint8_t* response, size_t& responseLen) -> CommandResult
        {
            return this->handle_get_config_value(payload, len, response, responseLen);
        });

    m_cmd_handler.register_handler(
        CommandCode::SET_CONFIG_VALUE,
        [this](const uint8_t* payload, size_t len,
               uint8_t* response, size_t& responseLen) -> CommandResult
        {
            return this->handle_set_config_value(payload, len, response, responseLen);
        });

    m_cmd_handler.register_handler(
        CommandCode::START_STREAM,
        [this](const uint8_t* payload, size_t len,
               uint8_t* response, size_t& responseLen) -> CommandResult
        {
            return this->handle_start_stream(payload, len, response, responseLen);
        });

    m_cmd_handler.register_handler(
        CommandCode::STOP_STREAM,
        [this](const uint8_t* payload, size_t len,
               uint8_t* response, size_t& responseLen) -> CommandResult
        {
            return this->handle_stop_stream(payload, len, response, responseLen);
        });

    m_cmd_handler.register_handler(
        CommandCode::LIST_STREAMS,
        [this](const uint8_t* payload, size_t len,
               uint8_t* response, size_t& responseLen) -> CommandResult
        {
            return this->handle_list_streams(payload, len, response, responseLen);
        });

    m_cmd_handler.register_handler(
        CommandCode::GET_VERSION,
        [this](const uint8_t* payload, size_t len,
               uint8_t* response, size_t& responseLen) -> CommandResult
        {
            return this->handle_get_version(payload, len, response, responseLen);
        });

    m_cmd_handler.register_handler(
        CommandCode::REBOOT,
        [this](const uint8_t* payload, size_t len,
               uint8_t* response, size_t& responseLen) -> CommandResult
        {
            return this->handle_reboot(payload, len, response, responseLen);
        });
}

CommandResult SystemOrchestrator::handle_get_status(
    const uint8_t* /* payload */, size_t /* len */,
    uint8_t* response, size_t& responseLen)
{
    bool recording = m_video.is_recording();
    bool streaming = m_video.is_streaming();
    uint32_t frames = m_video.frames_recorded();
    uint32_t file_size = m_video.file_size();
    uint8_t active_stream_slot_count = m_streams.active_slot_count();

    // SD storage stats for the web UI status panel, cached with a
    // bounded TTL so the 5 s status polling never issues SD
    // transactions per poll (a free-space query is a full FAT scan).
    // Photo/video counts cover today's date directory only: media is
    // never pruned, so a whole-card scan is unbounded, while the
    // current day is what the status panel shows.
    uint64_t free_bytes = 0;
    unsigned int photos = 0;
    unsigned int videos = 0;

    if (m_sd_available)
    {
        hal::millis_t now = hal::get_millis();
        bool expired = !m_sd_stats.valid ||
                       (now - m_sd_stats.refreshed_at) >= SD_STATS_TTL_MS;
        if (expired)
        {
            m_sd_stats.free_bytes = m_sd.free_bytes_sync();
            m_sd_stats.photos = 0;
            m_sd_stats.videos = 0;

            char date_dir[16];
            if (TimeFormatter::format_date(m_time.unix_now(), date_dir + 1,
                                           sizeof(date_dir) - 1))
            {
                date_dir[0] = '/';
                m_sd.list_dir_sync(date_dir,
                    [this](const char* name, size_t size, bool is_dir)
                    {
                        (void)size;
                        if (name != nullptr && !is_dir)
                        {
                            size_t name_len = strlen(name);
                            if (name_len >= 4 &&
                                strncmp(name + name_len - 4, ".jpg", 4) == 0)
                            {
                                m_sd_stats.photos++;
                            }
                            else if (name_len >= 4 &&
                                     strncmp(name + name_len - 4, ".avi", 4) == 0)
                            {
                                m_sd_stats.videos++;
                            }
                        }
                        return true;
                    });
            }

            m_sd_stats.refreshed_at = now;
            m_sd_stats.valid = true;
        }

        free_bytes = m_sd_stats.free_bytes;
        photos = m_sd_stats.photos;
        videos = m_sd_stats.videos;
    }

    // Build identity and live telemetry for the web UI status panel:
    // version (from version.h), core temperature, the applied frame
    // dimensions, and the cumulative delivered-frame count the UI
    // samples to render the preview framerate.
    char version_buf[32] = "unknown";
    if (!version::get_version_string(version_buf, sizeof(version_buf)))
    {
        strncpy(version_buf, "unknown", sizeof(version_buf) - 1);
        version_buf[sizeof(version_buf) - 1] = '\0';
    }
    const ResolvedCameraParams& cam_params = m_camera.get_params();

    int written = snprintf(
        reinterpret_cast<char*>(response),
        CommandHandler::MAX_RESPONSE_SIZE,
        "{\"recording\":%s,\"streaming\":%s,\"sd\":%s,\"frames\":%lu,"
        "\"file_size\":%lu,"
        "\"connected_streams\":%u,"
        "\"sd_free_bytes\":%" PRIu64 ",\"photos\":%u,\"videos\":%u,"
        "\"uptime_ms\":%lu,"
        "\"version\":\"%s\","
        "\"temperature_c\":%.2f,"
        "\"width\":%u,\"height\":%u,"
        "\"stream_frames\":%lu}",
        recording ? "true" : "false",
        streaming ? "true" : "false",
        m_sd_available ? "true" : "false",
        static_cast<unsigned long>(frames),
        static_cast<unsigned long>(file_size),
        static_cast<unsigned int>(active_stream_slot_count),
        free_bytes,
        photos,
        videos,
        static_cast<unsigned long>(hal::get_millis()),
        version_buf,
        static_cast<double>(m_hw_periph.read_temperature()),
        static_cast<unsigned int>(cam_params.width),
        static_cast<unsigned int>(cam_params.height),
        static_cast<unsigned long>(m_streams.total_frames_delivered())
    );

    if (written < 0 || static_cast<size_t>(written) >= CommandHandler::MAX_RESPONSE_SIZE)
    {
        m_log.error(SUBSYSTEM, "Status response too large");
        return CommandResult::INTERNAL_ERROR;
    }

    responseLen = static_cast<size_t>(written);
    return CommandResult::SUCCESS;
}

CommandResult SystemOrchestrator::handle_start_recording(
    const uint8_t* /* payload */, size_t /* len */,
    uint8_t* /* response */, size_t& /* responseLen */)
{
    // SD availability takes precedence over state-machine checks: the
    // card is a precondition of the command being meaningful at all.
    if (!m_sd_available)
    {
        m_log.warn(SUBSYSTEM, "Start recording rejected - SD unavailable");
        return CommandResult::SD_UNAVAILABLE;
    }

    if (m_video.is_recording())
    {
        m_log.warn(SUBSYSTEM, "Start recording rejected - already recording");
        return CommandResult::INVALID_STATE;
    }

    if (m_video.start_recording())
    {
        m_log.info(SUBSYSTEM, "Recording started");
        return CommandResult::SUCCESS;
    }

    m_log.error(SUBSYSTEM, "Failed to start recording");
    return CommandResult::INTERNAL_ERROR;
}

CommandResult SystemOrchestrator::handle_stop_recording(
    const uint8_t* /* payload */, size_t /* len */,
    uint8_t* /* response */, size_t& /* responseLen */)
{
    m_video.stop_recording();
    m_log.info(SUBSYSTEM, "Recording stop requested");
    return CommandResult::SUCCESS;
}

CommandResult SystemOrchestrator::handle_take_photo(
    const uint8_t* /* payload */, size_t /* len */,
    uint8_t* response, size_t& responseLen)
{
    // SD availability takes precedence over state-machine checks: the
    // card is a precondition of the command being meaningful, and the
    // gate avoids wasting a camera capture that could not be stored.
    if (!m_sd_available)
    {
        m_log.warn(SUBSYSTEM, "Take photo rejected - SD unavailable");
        return CommandResult::SD_UNAVAILABLE;
    }

    // Photo capture posts TAKE_PHOTO into the capture executor and is
    // served at an activation boundary, so photos work while recording
    // and/or streaming are active; camera and SD failures surface
    // synchronously to this caller as INTERNAL_ERROR.

    // Build photo path from unix timestamp (UTC)
    uint32_t seconds = m_time.unix_now();

    char date[9];
    char time_str[7];
    if (!TimeFormatter::format_date(seconds, date, sizeof(date)) ||
        !TimeFormatter::format_time(seconds, time_str, sizeof(time_str)))
    {
        m_log.error(SUBSYSTEM, "Failed to format timestamp");
        return CommandResult::INTERNAL_ERROR;
    }

    char base_path[64];
    snprintf(base_path, sizeof(base_path), "/%s/%s_%s.jpg", date, date, time_str);

    // Avoid overwriting a photo taken within the same second
    char photo_path[64];
    if (!m_video.make_unique_path(base_path, photo_path, sizeof(photo_path)))
    {
        m_log.error(SUBSYSTEM, "Could not allocate unique photo path");
        return CommandResult::INTERNAL_ERROR;
    }

    if (!m_video.capture_photo(photo_path))
    {
        m_log.error(SUBSYSTEM, "Photo capture/write failed");
        return CommandResult::INTERNAL_ERROR;
    }

    // Return path in response
    size_t path_len = strnlen(photo_path, sizeof(photo_path));
    if (path_len >= CommandHandler::MAX_RESPONSE_SIZE)
    {
        return CommandResult::INTERNAL_ERROR;
    }
    memcpy(response, photo_path, path_len);
    responseLen = path_len;

    return CommandResult::SUCCESS;
}

CommandResult SystemOrchestrator::handle_set_time(
    const uint8_t* payload, size_t len,
    uint8_t* /* response */, size_t& /* responseLen */)
{
    if (len < sizeof(uint32_t))
    {
        m_log.warn(SUBSYSTEM, "SET_TIME payload too short (%zu bytes)", len);
        return CommandResult::INVALID_ARGS;
    }

    uint32_t unix_time = 0;
    memcpy(&unix_time, payload, sizeof(uint32_t));

    m_time.set_unix_offset(unix_time, m_time.now());
    auto now = m_time.localtime();

    m_log.info(SUBSYSTEM, "Time set to %lu", static_cast<unsigned long>(unix_time));
    m_log.info(SUBSYSTEM, "Current date-time is %u-%u-%u %u:%u:%u UTC",
        1900 + now.tm_year,
        1 + now.tm_mon,
        now.tm_mday,
        now.tm_hour,
        now.tm_min,
        now.tm_sec);
    
    return CommandResult::SUCCESS;
}

CommandResult SystemOrchestrator::handle_set_config(
    const uint8_t* payload, size_t len,
    uint8_t* /* response */, size_t& /* responseLen */)
{
    if (payload == nullptr || len == 0)
    {
        m_log.warn(SUBSYSTEM, "SET_CONFIG: missing destination argument");
        return CommandResult::INVALID_ARGS;
    }

    // Payload is a null-terminated destination string: "nvs" or "sd"
    // (case-insensitive).
    char dest_buf[8];
    size_t dest_len = len < sizeof(dest_buf) - 1 ? len : sizeof(dest_buf) - 1;
    memcpy(dest_buf, payload, dest_len);
    dest_buf[dest_len] = '\0';
    for (char* c = dest_buf; *c != '\0'; ++c)
    {
        *c = static_cast<char>(std::tolower(
            static_cast<unsigned char>(*c)));
    }

    if (strcmp(dest_buf, "nvs") == 0)
    {
        if (!m_config.save_nvs(m_nvs))
        {
            m_log.error(SUBSYSTEM, "Config save to NVS failed");
            return CommandResult::INTERNAL_ERROR;
        }
        m_log.info(SUBSYSTEM, "Config saved to NVS");
        return CommandResult::SUCCESS;
    }

    if (strcmp(dest_buf, "sd") == 0)
    {
        // SD availability takes precedence over any other check.
        if (!m_sd_available)
        {
            m_log.warn(SUBSYSTEM, "Config save rejected - SD unavailable");
            return CommandResult::SD_UNAVAILABLE;
        }

        if (!m_config.save_sd(m_sd))
        {
            m_log.error(SUBSYSTEM, "Config save to SD failed");
            return CommandResult::INTERNAL_ERROR;
        }
        m_log.info(SUBSYSTEM, "Config saved to SD");
        return CommandResult::SUCCESS;
    }

    m_log.warn(SUBSYSTEM, "SET_CONFIG: unknown destination '%s'", dest_buf);
    return CommandResult::INVALID_ARGS;
}

CommandResult SystemOrchestrator::handle_get_config(
    const uint8_t* /* payload */, size_t /* len */,
    uint8_t* response, size_t& responseLen)
{
    char buf[CommandHandler::MAX_RESPONSE_SIZE];
    size_t serialized_len = m_config.serialize(buf, sizeof(buf));

    // Serialize always reports the full size needed. A config that
    // exceeds the response buffer (i.e. exceeds the storage buffer
    // limit) cannot be transferred intact.
    if (serialized_len >= CommandHandler::MAX_RESPONSE_SIZE)
    {
        m_log.error(SUBSYSTEM, "Serialized config too large (%zu bytes)",
                    serialized_len);
        return CommandResult::INTERNAL_ERROR;
    }

    memcpy(response, buf, serialized_len);
    responseLen = serialized_len;
    return CommandResult::SUCCESS;
}

CommandResult SystemOrchestrator::handle_get_config_value(
    const uint8_t* payload, size_t len,
    uint8_t* response, size_t& responseLen)
{
    if (payload == nullptr || len == 0)
    {
        return CommandResult::INVALID_ARGS;
    }

    // Payload is a null-terminated "section.key" string.
    const char* key = reinterpret_cast<const char*>(payload);

    char value_buf[CONFIG_FIELD_MAX_LENGTH];
    if (!m_config.get_config_value(key, value_buf, sizeof(value_buf)))
    {
        m_log.warn(SUBSYSTEM, "GET_CONFIG_VALUE: unknown key '%s'", key);
        return CommandResult::INVALID_ARGS;
    }

    size_t val_len = strnlen(value_buf, CONFIG_FIELD_MAX_LENGTH);
    if (val_len >= CommandHandler::MAX_RESPONSE_SIZE)
    {
        return CommandResult::INTERNAL_ERROR;
    }

    memcpy(response, value_buf, val_len);
    responseLen = val_len;
    return CommandResult::SUCCESS;
}

CommandResult SystemOrchestrator::handle_set_config_value(
    const uint8_t* payload, size_t len,
    uint8_t* /* response */, size_t& /* responseLen */)
{
    if (payload == nullptr || len == 0)
    {
        return CommandResult::INVALID_ARGS;
    }

    // Payload format: "section.key=value"
    // Find the '=' separator.
    const char* data = reinterpret_cast<const char*>(payload);
    const char* eq = static_cast<const char*>(memchr(data, '=', len));
    if (eq == nullptr || eq == data)
    {
        m_log.warn(SUBSYSTEM, "SET_CONFIG_VALUE: missing '=' separator");
        return CommandResult::INVALID_ARGS;
    }

    // Extract key (before '=') as a temporary null-terminated string.
    size_t key_len = static_cast<size_t>(eq - data);
    if (key_len >= CONFIG_FIELD_MAX_LENGTH)
    {
        return CommandResult::INVALID_ARGS;
    }
    char key_buf[CONFIG_FIELD_MAX_LENGTH];
    memcpy(key_buf, data, key_len);
    key_buf[key_len] = '\0';

    // Value is after '='.
    const char* val = eq + 1;

    if (!m_config.set_config_value(key_buf, val))
    {
        m_log.warn(SUBSYSTEM, "SET_CONFIG_VALUE: unknown key '%s'", key_buf);
        return CommandResult::INVALID_ARGS;
    }

    m_log.info(SUBSYSTEM, "Config '%s' set to '%s'", key_buf, val);
    return CommandResult::SUCCESS;
}

// ============================================================
// Stream commands
// ============================================================

CommandResult SystemOrchestrator::handle_start_stream(
    const uint8_t* /* payload */, size_t /* len */,
    uint8_t* /* response */, size_t& /* responseLen */)
{
    if (m_video.start_streaming())
    {
        m_log.info(SUBSYSTEM, "Streaming started");
        return CommandResult::SUCCESS;
    }

    m_log.error(SUBSYSTEM, "Failed to start streaming");
    return CommandResult::INTERNAL_ERROR;
}

CommandResult SystemOrchestrator::handle_stop_stream(
    const uint8_t* /* payload */, size_t /* len */,
    uint8_t* /* response */, size_t& /* responseLen */)
{
    // stop_streaming() is idempotent and owns the teardown rules: during
    // recording it only ends preview delivery, otherwise it tears down
    // the capture pipeline and closes all connected viewers.
    m_video.stop_streaming();
    m_log.info(SUBSYSTEM, "Streaming stopped");
    return CommandResult::SUCCESS;
}

CommandResult SystemOrchestrator::handle_list_streams(
    const uint8_t* /* payload */, size_t /* len */,
    uint8_t* response, size_t& responseLen)
{
    int written = snprintf(
        reinterpret_cast<char*>(response),
        CommandHandler::MAX_RESPONSE_SIZE,
        "{\"active_streams\":%u}",
        static_cast<unsigned int>(m_streams.active_slot_count()));

    if (written < 0 ||
        static_cast<size_t>(written) >= CommandHandler::MAX_RESPONSE_SIZE)
    {
        return CommandResult::INTERNAL_ERROR;
    }

    responseLen = static_cast<size_t>(written);
    return CommandResult::SUCCESS;
}

// ============================================================
// Device info and maintenance commands
// ============================================================

CommandResult SystemOrchestrator::handle_get_version(
    const uint8_t* /* payload */, size_t /* len */,
    uint8_t* response, size_t& responseLen)
{
    char version_buf[32];
    if (!version::get_version_string(version_buf, sizeof(version_buf)))
    {
        m_log.error(SUBSYSTEM, "Version string does not fit buffer");
        return CommandResult::INTERNAL_ERROR;
    }

    int written = snprintf(
        reinterpret_cast<char*>(response),
        CommandHandler::MAX_RESPONSE_SIZE,
        "{\"version\":\"%s\"}",
        version_buf);

    if (written < 0 ||
        static_cast<size_t>(written) >= CommandHandler::MAX_RESPONSE_SIZE)
    {
        return CommandResult::INTERNAL_ERROR;
    }

    responseLen = static_cast<size_t>(written);
    return CommandResult::SUCCESS;
}

CommandResult SystemOrchestrator::handle_reboot(
    const uint8_t* /* payload */, size_t /* len */,
    uint8_t* /* response */, size_t& /* responseLen */)
{
    // The restart itself is deferred by the RestartHal implementation:
    // the dispatching request handler still has to deliver its response
    // (HTTP reply or serial reply) before the device goes down.
    m_log.warn(SUBSYSTEM, "Reboot requested - restarting in %lu ms",
               static_cast<unsigned long>(REBOOT_DELAY_MS));
    if (!m_restart.schedule_restart(REBOOT_DELAY_MS))
    {
        m_log.error(SUBSYSTEM, "Failed to schedule restart");
        return CommandResult::INTERNAL_ERROR;
    }
    return CommandResult::SUCCESS;
}

// ============================================================
// /stream route
// ============================================================

void SystemOrchestrator::register_stream_route()
{
    // Snapshot the web password at registration time: auth compares
    // against this stable copy rather than the live config struct,
    // which runtime config updates can mutate while a request handler
    // runs.
    strncpy(m_stream_password, m_config.config().web.password,
            sizeof(m_stream_password) - 1);
    m_stream_password[sizeof(m_stream_password) - 1] = '\0';

    m_web.on("/stream", hal::HttpMethod::GET,
             [this](hal::RequestRef& request)
             {
                 this->handle_stream_request(request);
             });
}

void SystemOrchestrator::handle_stream_request(hal::RequestRef& request)
{
    // Basic auth using the register-time password snapshot.
    const char* auth = request.auth_header();
    const char* password = m_stream_password;
    if (!check_basic_auth(auth, password))
    {
        request.send_unauthorized();
        return;
    }

    // Streaming mode must be explicitly started before a viewer can
    // connect. Viewers exist only while streaming is active, which
    // guarantees they are closed when the capture pipeline tears down.
    // This gate is advisory only: a stop_streaming() can complete in
    // the window between it and the slot bind below, so the bind is
    // always followed by a revalidation (bind-then-revalidate).
    if (!m_video.is_streaming())
    {
        request.send(409, "application/json",
                     "{\"error\":\"streaming not started\"}");
        return;
    }

    std::shared_ptr<StreamFrameChannel> channel =
        std::make_shared<StreamFrameChannel>();
    StreamFrameChannel* channel_ptr = channel.get();

    int slot = m_streams.start_stream(channel);
    if (slot < 0)
    {
        // start_stream() shares ownership of the channel; when no slot
        // is free it returns -1 and our reference deletes it here.
        // request.send() attaches the error response and closes the
        // connection.
        request.send(503, "application/json",
                     "{\"error\":\"no free stream slots\"}");
        return;
    }

    // Revalidate the bind: if a stop completed in the gate-to-bind
    // window the sink is already gone, and no future teardown boundary
    // will close this slot (the stop already ran its viewer close).
    // Free the slot and reject the viewer instead of leaving it hung
    // on a silent multipart response. A teardown completing after this
    // recheck is harmless: its own stop_all_streams() snapshot then
    // necessarily includes this bound slot and closes it.
    if (!m_video.is_streaming())
    {
        m_streams.stop_stream(static_cast<uint8_t>(slot));
        request.send(503, "application/json",
                     "{\"error\":\"streaming not started\"}");
        return;
    }

    // Attach a real streaming response: the transport writes the HTTP
    // headers, pulls multipart parts from the channel under framework
    // flow control (no raw client writes, no send-buffer truncation),
    // and destroys the response on disconnect or completion. Its end
    // callback frees the viewer slot, and the request/response lifecycle
    // stays entirely inside the transport (no per-viewer leak).
    auto on_end = [this, channel_ptr]()
    {
        m_streams.on_stream_ended(channel_ptr);
    };
    if (!request.send_stream_response(channel, STREAM_BOUNDARY_TOKEN, on_end))
    {
        // The response could not be attached: end the stream directly.
        // The channel has no transport reference, so freeing the slot
        // here is safe.
        m_streams.stop_stream(static_cast<uint8_t>(slot));
        m_streams.on_stream_ended(channel_ptr);
        request.send(500, "application/json",
                     "{\"error\":\"streaming unavailable\"}");
        return;
    }

    m_log.info(SUBSYSTEM, "Stream viewer connected (slot %d)", slot);
}

// ============================================================
// /update route (bulk config from the web UI)
// ============================================================

void SystemOrchestrator::register_update_route()
{
    // Snapshot the web password at registration time, mirroring the
    // /stream route: auth compares against a stable copy rather than
    // the live config struct.
    strncpy(m_update_password, m_config.config().web.password,
            sizeof(m_update_password) - 1);
    m_update_password[sizeof(m_update_password) - 1] = '\0';

    m_web.on("/update", hal::HttpMethod::POST,
             [this](hal::RequestRef& request)
             {
                 this->handle_update_request(request);
             });
}

void SystemOrchestrator::handle_update_request(hal::RequestRef& request)
{
    // Config updates carry credentials, so like the config commands
    // they are only served when a web password is configured.
    if (m_update_password[0] == '\0')
    {
        request.send(401, "application/json",
                     "{\"error\":\"authentication required\"}");
        return;
    }
    if (!check_basic_auth(request.auth_header(), m_update_password))
    {
        request.send_unauthorized();
        return;
    }

    const char* dest = request.arg("dest");
    const bool save_to_sd = (dest != nullptr && strcmp(dest, "sd") == 0);

    if (save_to_sd && !m_sd_available)
    {
        request.send(503, "application/json",
                     "{\"error\":\"sd unavailable\"}");
        return;
    }

    const char* ini = request.form_value("config");
    if (ini == nullptr || ini[0] == '\0')
    {
        request.send(400, "application/json",
                     "{\"error\":\"missing 'config' parameter\"}");
        return;
    }

    // A form value larger than the transport's buffer was truncated in
    // transit: applying the cut text would silently drop trailing
    // config sections while reporting success.
    if (request.form_value_truncated("config"))
    {
        request.send(413, "application/json",
                     "{\"error\":\"config too large\"}");
        return;
    }

    // Apply the INI text as an overlay onto the live config.
    if (!m_config.overlay(reinterpret_cast<const uint8_t*>(ini),
                          strlen(ini)))
    {
        request.send(400, "application/json",
                     "{\"error\":\"invalid config\"}");
        return;
    }

    bool saved = save_to_sd
        ? m_config.save_sd(m_sd)
        : m_config.save_nvs(m_nvs);
    if (!saved)
    {
        request.send(500, "application/json",
                     "{\"error\":\"config save failed\"}");
        return;
    }

    m_log.info(SUBSYSTEM, "Config updated via /update (dest=%s)",
               save_to_sd ? "sd" : "nvs");
    request.send(200, "application/json", "{\"result\":\"ok\"}");
}
