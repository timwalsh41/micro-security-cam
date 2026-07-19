#include "system_orchestrator.h"
#include <cstdio>
#include <cstring>

static constexpr const char* SUBSYSTEM = "SysOrch";

SystemOrchestrator::SystemOrchestrator(VideoController& video,
                                       CameraController& camera,
                                       CommandHandler& cmd_handler,
                                       TimeService& time,
                                       ConfigService& config,
                                       LoggingService& log)
    : m_video(video)
    , m_camera(camera)
    , m_cmd_handler(cmd_handler)
    , m_time(time)
    , m_config(config)
    , m_log(log)
{
}

void SystemOrchestrator::init()
{
    register_commands();
    m_log.info(SUBSYSTEM, "SystemOrchestrator initialized");
}

void SystemOrchestrator::tick()
{
    // Placeholder — grows in Slice 7 for schedule evaluation
    // and mode transitions.
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
        "{\"recording\":%s,\"state\":%d,\"frames\":%lu,\"file_size\":%lu}",
        recording ? "true" : "false",
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
    uint8_t* /* response */, size_t& /* responseLen */)
{
    CameraCaptureResult result = m_camera.capture_frame();
    if (result != CameraCaptureResult::SUCCESS)
    {
        m_log.error(SUBSYSTEM, "Photo capture failed");
        return CommandResult::INTERNAL_ERROR;
    }

    hal::CameraFrame frame = m_camera.get_frame();
    m_log.info(SUBSYSTEM, "Photo captured (%u bytes)", frame.len);
    m_camera.release_frame();

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
    m_log.info(SUBSYSTEM, "Time set to %lu", static_cast<unsigned long>(unix_time));
    return CommandResult::SUCCESS;
}

CommandResult SystemOrchestrator::handle_set_config(
    const uint8_t* /* payload */, size_t /* len */,
    uint8_t* /* response */, size_t& /* responseLen */)
{
    // Config update from web deferred to Slice 5.
    return CommandResult::INVALID_STATE;
}
