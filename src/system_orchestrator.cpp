#include "system_orchestrator.h"
#include "stream_service.h"
#include "web_auth_util.h"
#include "time_formatter.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <utility>

SystemOrchestrator::SystemOrchestrator(VideoController& video,
                                       CameraController& camera,
                                       CommandHandler& cmd_handler,
                                       TimeService& time,
                                       ConfigService& config,
                                       LoggingService& log,
                                       StreamService& streams,
                                       hal::WebServerHal& web)
    : m_video(video)
    , m_camera(camera)
    , m_cmd_handler(cmd_handler)
    , m_time(time)
    , m_config(config)
    , m_log(log)
    , m_streams(streams)
    , m_web(web)
{
}

void SystemOrchestrator::init()
{
    register_commands();
    register_stream_route();
    m_log.info(SUBSYSTEM, "SystemOrchestrator initialized");
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
}

CommandResult SystemOrchestrator::handle_get_status(
    const uint8_t* /* payload */, size_t /* len */,
    uint8_t* response, size_t& responseLen)
{
    bool recording = m_video.is_recording();
    int state = static_cast<int>(m_video.state());
    uint32_t frames = m_video.frames_recorded();
    uint32_t file_size = m_video.file_size();

    int written = snprintf(
        reinterpret_cast<char*>(response),
        CommandHandler::MAX_RESPONSE_SIZE,
        "{\"recording\":%s,\"streaming\":%s,\"state\":%d,\"frames\":%lu,"
        "\"file_size\":%lu}",
        recording ? "true" : "false",
        m_video.is_streaming() ? "true" : "false",
        state,
        static_cast<unsigned long>(frames),
        static_cast<unsigned long>(file_size));

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
    if (m_video.is_recording())
    {
        m_log.warn(SUBSYSTEM, "Start recording rejected — already recording");
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
    if (m_video.is_recording())
    {
        m_log.warn(SUBSYSTEM, "Take photo rejected — recording in progress");
        return CommandResult::INVALID_STATE;
    }

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
    const uint8_t* /* payload */, size_t /* len */,
    uint8_t* /* response */, size_t& /* responseLen */)
{
    // Placeholder - config update deferred
    return CommandResult::INVALID_STATE;
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
// /stream route
// ============================================================

void SystemOrchestrator::register_stream_route()
{
    m_web.on("/stream", hal::HttpMethod::GET,
             [this](hal::RequestRef& request)
             {
                 this->handle_stream_request(request);
             });
}

void SystemOrchestrator::handle_stream_request(hal::RequestRef& request)
{
    // Basic auth using the configured web password.
    const char* auth = request.auth_header();
    const char* password = m_config.config().web.password;
    if (!check_basic_auth(auth, password))
    {
        request.send(401, "application/json",
                     "{\"error\":\"authentication required\"}");
        return;
    }

    // Streaming mode must be explicitly started before a viewer can
    // connect. Viewers exist only while streaming is active, which
    // guarantees they are closed when the capture pipeline tears down.
    if (!m_video.is_streaming())
    {
        request.send(409, "application/json",
                     "{\"error\":\"streaming not started\"}");
        return;
    }

    std::unique_ptr<hal::StreamWriter> writer(request.create_stream_writer());
    if (!writer)
    {
        request.send(500, "application/json",
                     "{\"error\":\"streaming unavailable\"}");
        return;
    }

    int slot = m_streams.start_stream(std::move(writer));
    if (slot < 0)
    {
        // start_stream() takes the writer by value (unique_ptr). When no
        // slot is free it returns -1 without transferring ownership, so
        // its parameter destructor deletes the writer here. That does not
        // close the connection (the writer holds AsyncClient as a
        // non-owning pointer); request.send() closes it.
        request.send(503, "application/json",
                     "{\"error\":\"no free stream slots\"}");
        return;
    }

    m_log.info(SUBSYSTEM, "Stream viewer connected (slot %d)", slot);
}
