#include "video_controller.h"
#include <cstring>
#include <cstdio>

// ============================================================
// Construction
// ============================================================

VideoController::VideoController(CameraController& cam,
                                 SdController& sd,
                                 TimeService& time,
                                 LoggingService& log,
                                 ICaptureScheduler& scheduler)
    : m_camera(cam)
    , m_sd(sd)
    , m_time(time)
    , m_log(log)
    , m_scheduler(scheduler)
{
}

// ============================================================
// Initialization
// ============================================================

bool VideoController::init(const ConfigData::VideoConfig& config,
                           uint16_t width, uint16_t height)
{
    m_config = config;
    m_width = width;
    m_height = height;

    m_frame_interval_ms = (m_config.fps > 0)
                              ? (1000u / m_config.fps)
                              : 0;

    m_log.info(SUBSYSTEM, "Init: %ufps %ux%u, clip %us, quality %u",
               m_config.fps, m_width, m_height,
               m_config.clip_duration_s, m_config.jpeg_quality);

    return true;
}

// ============================================================
// Recording control
// ============================================================

bool VideoController::start_recording()
{
    if (m_state != RecordingState::IDLE)
    {
        return false;
    }

    m_stop_clip = false;
    open_clip();

    if (!m_avi_writer.is_open())
    {
        return false;
    }

    m_state = RecordingState::RECORDING;

    // Start the capture scheduler — invokes capture_and_write_frame() on each frame
    if (!m_scheduler.start(m_frame_interval_ms,
                           [this]() { capture_and_write_frame(); }))
    {
        // Scheduler busy (e.g. a task left over from a timed-out stop):
        // undo the just-opened clip and stay in IDLE
        close_clip();
        m_state = RecordingState::IDLE;
        m_log.warn(SUBSYSTEM, "Recording start failed: scheduler busy");
        return false;
    }

    m_log.info(SUBSYSTEM, "Recording started: %s", m_file_path);
    return true;
}

void VideoController::stop_recording()
{
    if (m_state == RecordingState::RECORDING)
    {
        m_stop_clip = true;

        m_scheduler.stop();
    }
}

bool VideoController::capture_photo(const char* path)
{
    if (m_state != RecordingState::IDLE)
    {
        m_log.warn(SUBSYSTEM, "capture_photo rejected: not idle");
        return false;
    }

    CameraCaptureResult result = m_camera.capture_frame();
    if (result != CameraCaptureResult::SUCCESS)
    {
        m_log.warn(SUBSYSTEM, "Photo capture failed");
        return false;
    }

    hal::CameraFrame frame = m_camera.get_frame();

    // Ensure the parent directory exists before writing (mirrors open_clip()).
    // The first photo of a new day would otherwise fail to open the file.
    char dir_path[64];
    if (PathUtils::extract_parent_dir(path, dir_path, sizeof(dir_path)))
    {
        create_directory(dir_path);
    }

    // Write JPEG data directly (no AVI framing)
    bool ok = m_sd.write_sync(path, frame.data, frame.len,
                              hal::FileOpenMode::WRITE);

    m_camera.release_frame();

    if (ok)
    {
        m_log.info(SUBSYSTEM, "Photo saved: %s (%u bytes)", path, frame.len);
    }
    else
    {
        m_log.warn(SUBSYSTEM, "Photo SD write failed: %s", path);
    }
    return ok;
}

void VideoController::set_stream_service(void* /*stream_service*/)
{
    // TODO: wire to StreamService in future phase
}

// ============================================================
// State queries
// ============================================================

bool VideoController::is_recording() const
{
    return m_state == RecordingState::RECORDING;
}

RecordingState VideoController::state() const
{
    return m_state;
}

uint32_t VideoController::frames_recorded() const
{
    return m_frames_recorded;
}

uint32_t VideoController::file_size() const
{
    return m_file_size;
}

// ============================================================
// Clip lifecycle
// ============================================================

void VideoController::open_clip()
{
    // Build file path from unix timestamp (UTC)
    uint32_t seconds = m_time.unix_now();

    char date[9];
    char time_str[7];
    if (!TimeFormatter::format_date(seconds, date, sizeof(date)) ||
        !TimeFormatter::format_time(seconds, time_str, sizeof(time_str)))
    {
        m_log.warn(SUBSYSTEM, "Failed to format timestamp");
        return;
    }

    // Build directory path: /YYYYMMDD
    char dir_path[16];
    snprintf(dir_path, sizeof(dir_path), "/%s", date);

    // Create the parent directory on the SD card
    create_directory(dir_path);

    // Build file path: /YYYYMMDD/YYYYMMDD_HHMMSS_<WxH>_<FPS>fps.avi
    char base_path[sizeof(m_file_path)];
    snprintf(base_path, sizeof(base_path),
             "/%s/%s_%s_%ux%u_%ufps.avi",
             date, date, time_str,
             m_width, m_height, m_config.fps);

    // Avoid clobbering a clip from an earlier recording within the same
    // second by uniquifying with a serial suffix
    if (!make_unique_path(base_path, m_file_path, sizeof(m_file_path)))
    {
        m_log.warn(SUBSYSTEM, "Could not allocate unique clip path");
        return;
    }

    // Store clip path for close_clip()
    strncpy(m_clip_path, m_file_path, sizeof(m_clip_path) - 1);
    m_clip_path[sizeof(m_clip_path) - 1] = '\0';

    // Open AVI writer (generates header in memory)
    m_avi_writer.open(m_file_path, m_config.fps, m_width, m_height);

    // Write placeholder header to SD at position 0
    AviBytes hdr = m_avi_writer.header();
    if (hdr.data && hdr.len > 0)
    {
        write_to_sd(hdr.data, hdr.len);
    }

    // Reset clip state
    m_frames_recorded = 0;
    m_file_size = 0;
}

void VideoController::close_clip()
{
    // Finalize AVI (patches header, builds index)
    m_avi_writer.close();

    // Append index to the file
    AviBytes idx = m_avi_writer.index();
    if (idx.data && idx.len > 0)
    {
        write_to_sd(idx.data, idx.len);
    }

    // Seek to position 0 and overwrite the placeholder header
    AviBytes hdr = m_avi_writer.patched_header();
    if (hdr.data && hdr.len > 0)
    {
        write_at_offset(m_clip_path, 0, hdr.data, hdr.len);
    }

    m_log.info(SUBSYSTEM, "Clip closed: %s (%u frames, %u bytes)",
               m_clip_path,
               static_cast<uint32_t>(m_frames_recorded),
               static_cast<uint32_t>(m_file_size));

    // Clean up
    m_frames_recorded = 0;
    m_file_size = 0;
    m_stop_clip = false;
    m_avi_writer = AviWriter();  // Reset writer state
}

// ============================================================
// Frame capture
// ============================================================

void VideoController::capture_and_write_frame()
{
    // Guard against stale callbacks (e.g. a task notification that was
    // already queued when stop was requested): never capture outside
    // the RECORDING state.
    if (m_state != RecordingState::RECORDING)
    {
        return;
    }

    CameraCaptureResult result = m_camera.capture_frame();
    if (result != CameraCaptureResult::SUCCESS)
    {
        m_log.warn(SUBSYSTEM, "Frame capture failed");
        return;
    }

    hal::CameraFrame frame = m_camera.get_frame();

    // Format as AVI chunk and write to SD
    m_avi_writer.write_frame(frame.data, frame.len);

    AviBytes chunk = m_avi_writer.last_frame_chunk();
    if (chunk.data && chunk.len > 0)
    {
        write_to_sd(chunk.data, chunk.len);
        m_file_size += static_cast<uint32_t>(chunk.len);
    }

    m_camera.release_frame();
    m_frames_recorded++;

    // Check clip termination conditions (auto-rotation)
    bool clip_limit_reached = false;
    if (m_config.clip_duration_s > 0 &&
        m_frames_recorded >=
            static_cast<uint32_t>(m_config.fps) * m_config.clip_duration_s)
    {
        clip_limit_reached = true;
    }
    else if (m_config.max_file_size_mb > 0 &&
             m_file_size >= static_cast<uint32_t>(m_config.max_file_size_mb) *
                                1024UL * 1024UL)
    {
        clip_limit_reached = true;
    }

    // Handle clip closure
    if (m_stop_clip || clip_limit_reached)
    {
        bool explicit_stop = m_stop_clip;
        close_clip();

        if (!explicit_stop)
        {
            // Auto-rotation: open new clip and resume
            open_clip();
            if (m_avi_writer.is_open())
            {
                m_log.info(SUBSYSTEM, "New clip: %s", m_clip_path);
            }
        }
        else
        {
            m_state = RecordingState::IDLE;
        }
    }
}

// ============================================================
// SD I/O helpers
// ============================================================

bool VideoController::write_to_sd(const uint8_t* data, size_t len)
{
    return m_sd.write_sync(m_file_path, data, len, hal::FileOpenMode::APPEND);
}

bool VideoController::write_at_offset(const char* path, uint32_t offset,
                                      const uint8_t* data, size_t len)
{
    return m_sd.write_at_sync(path, offset, data, len);
}

bool VideoController::make_unique_path(const char* base_path, char* out, size_t out_size)
{
    return PathUtils::make_unique_path(
        base_path,
        [this](const char* path) { return m_sd.exists_sync(path); },
        out,
        out_size);
}

// ============================================================
// Directory creation
// ============================================================

void VideoController::create_directory(const char* path)
{
    m_sd.create_dir_sync(path);
}
