#include "video_controller.h"
#include <cstring>
#include <cstdio>

// ============================================================
// Construction
// ============================================================

VideoController::VideoController(CameraController &cam,
                                 PeripheralManager &peripheral,
                                 TimeService &time,
                                 LoggingService &log)
    : m_camera(cam)
    , m_peripheral(peripheral)
    , m_time(time)
    , m_log(log)
    , m_sd_buffer(peripheral)
{
}

// ============================================================
// Initialization
// ============================================================

bool VideoController::init(const ConfigData::VideoConfig &config)
{
    m_config = config;

    // Compute frame interval from FPS (fps >= 1 guaranteed by IniParser)
    m_frame_interval_ms = (m_config.fps > 0)
                              ? (1000u / m_config.fps)
                              : 0;

    m_log.info("VIDEO", "Init: %ufps %s, clip %us, quality %u",
               m_config.fps, m_config.resolution,
               m_config.clip_duration_s, m_config.jpeg_quality);

    return true;
}

// ============================================================
// Resolution mapping
// ============================================================

VideoController::Resolution VideoController::map_resolution(const char *name)
{
    if (!name)
    {
        return {640, 480};  // VGA default
    }

    // String comparison using case-insensitive matching
    if (strcasecmp(name, "QQVGA") == 0) return {160, 120};
    if (strcasecmp(name, "QVGA") == 0)  return {320, 240};
    if (strcasecmp(name, "VGA") == 0)   return {640, 480};
    if (strcasecmp(name, "SVGA") == 0)  return {800, 600};
    if (strcasecmp(name, "XGA") == 0)   return {1024, 768};
    if (strcasecmp(name, "HD") == 0)    return {1280, 720};
    if (strcasecmp(name, "UXGA") == 0)  return {1600, 1200};

    return {640, 480};  // VGA fallback for unknown names
}

// ============================================================
// Frame size estimation
// ============================================================

uint32_t VideoController::avg_bytes_per_frame() const
{
    Resolution res = map_resolution(m_config.resolution);
    uint32_t pixels = static_cast<uint32_t>(res.width) * res.height;

    // Base bytes-per-pixel decreases with resolution (JPEG spatial redundancy)
    float bpp_base;
    if (pixels <= (320u * 240u))
    {
        bpp_base = 0.40f;
    }
    else if (pixels <= (640u * 480u))
    {
        bpp_base = 0.20f;
    }
    else if (pixels <= (1024u * 768u))
    {
        bpp_base = 0.12f;
    }
    else
    {
        bpp_base = 0.08f;
    }

    // Quality scaling: quality 10 ≈ base, quality 1 ≈ 3× base
    float quality_scale = 1.0f + 0.2f * (10 - m_config.jpeg_quality);

    return static_cast<uint32_t>(pixels * bpp_base * quality_scale);
}

uint32_t VideoController::max_frames() const
{
    uint32_t avg = avg_bytes_per_frame();
    return (avg > 0) ? static_cast<uint32_t>(CLIP_BUFFER_RAM_SIZE / avg)
                     : 1;
}

// ============================================================
// State machine
// ============================================================

void VideoController::tick()
{
    if (m_state == RecordingState::STOPPING)
    {
        // Save the explicit-stop flag before close_clip() clears it
        bool explicit_stop = m_stop_clip;
        close_clip();

        // Auto-rotation: if stop was not explicitly requested,
        // open a new clip and resume recording
        if (!explicit_stop)
        {
            open_clip();
            if (m_avi_writer.is_open())
            {
                m_state = RecordingState::RECORDING;
                m_log.info("VIDEO", "New clip: %s", m_file_path);
                return;
            }
        }

        m_state = RecordingState::IDLE;
        return;
    }

    if (m_state != RecordingState::RECORDING)
    {
        return;
    }

    // Check if stop was requested
    if (m_stop_clip)
    {
        m_state = RecordingState::STOPPING;
        return;
    }

    // Frame pacing: check if enough time has elapsed
    hal::millis_t now = m_time.now();
    hal::millis_t elapsed = TimeService::elapsed_ms(m_last_frame_time, now);

    if (elapsed >= m_frame_interval_ms)
    {
        capture_and_write_frame();
        m_last_frame_time = now;

        // Check clip termination conditions
        if (m_config.clip_duration_s > 0 &&
            m_frames_recorded >=
                static_cast<uint32_t>(m_config.fps) * m_config.clip_duration_s)
        {
            m_state = RecordingState::STOPPING;
            return;
        }

        if (m_config.max_file_size_mb > 0 &&
            m_file_size >= static_cast<uint32_t>(m_config.max_file_size_mb) *
                               1024UL * 1024UL)
        {
            m_state = RecordingState::STOPPING;
            return;
        }

        if (m_frames_recorded >= max_frames())
        {
            m_log.warn("VIDEO", "Max frames reached, closing clip");
            m_state = RecordingState::STOPPING;
            return;
        }
    }
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
    m_log.info("VIDEO", "Recording started: %s", m_file_path);
    return true;
}

void VideoController::stop_recording()
{
    if (m_state == RecordingState::RECORDING)
    {
        m_stop_clip = true;
    }
}

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
    hal::millis_t now = m_time.now();
    m_clip_start_time = now;

    // TEMPORARY IMPLEMENTATION - pending unix time integration
    // Build file path from current timestamp
    // Pattern: /YYYYMMDD/YYYYMMDD_HHMMSS_<WxH>_<FPS>fps.avi
    // Convert milliseconds to seconds for time formatting
    // Note: Date calculation uses a simplified 365-day/year, 30-day/month
    // model. Month/day will drift from real calendar dates. A full calendar
    // implementation (e.g. mktime) can replace this when needed.
    uint32_t seconds = static_cast<uint32_t>(now / 1000u);
    uint32_t ss = seconds % 60;
    uint32_t mm = (seconds / 60) % 60;
    uint32_t hh = (seconds / 3600) % 24;

    // Use day-of-year for date (simplified — not a full calendar)
    uint32_t day_of_year = (seconds / 86400u) % 365;
    uint32_t year = 1970 + (seconds / (86400u * 365u));
    uint32_t month = (day_of_year / 30) + 1;
    uint32_t day = (day_of_year % 30) + 1;

    Resolution res = map_resolution(m_config.resolution);

    // Build directory path: /YYYYMMDD
    char dir_path[16];
    snprintf(dir_path, sizeof(dir_path), "/%04u%02u%02u",
             year, month, day);

    // Create the parent directory on the SD card
    create_directory(dir_path);

    // Build file path: /YYYYMMDD/YYYYMMDD_HHMMSS_<WxH>_<FPS>fps.avi
    snprintf(m_file_path, sizeof(m_file_path),
             "/%04u%02u%02u/%04u%02u%02u_%02u%02u%02u_%ux%u_%ufps.avi",
             year, month, day,
             year, month, day, hh, mm, ss,
             res.width, res.height, m_config.fps);

    // Open AVI writer
    m_avi_writer.open(m_file_path, m_config.fps, res.width, res.height);

    // Open SD write buffer
    m_sd_buffer.open(m_file_path);

    // Reset clip state
    m_frame_chunks.clear();
    m_frames_recorded = 0;
    m_file_size = 0;
    m_last_frame_time = now - m_frame_interval_ms;
}

void VideoController::close_clip()
{
    // Finalize the AVI (patches header, builds index)
    m_avi_writer.close();

    // Buffer the patched header
    AviBytes hdr = m_avi_writer.patched_header();
    if (hdr.data && hdr.len > 0)
    {
        m_sd_buffer.buffer_frame(hdr.data, hdr.len);
    }

    // Buffer all accumulated frame chunks
    for (size_t i = 0; i < m_frame_chunks.size(); i++)
    {
        const FrameChunk &chunk = m_frame_chunks[i];
        m_sd_buffer.buffer_frame(chunk.data.data(), chunk.data.size());
    }

    // Buffer the frame index
    AviBytes idx = m_avi_writer.index();
    if (idx.data && idx.len > 0)
    {
        m_sd_buffer.buffer_frame(idx.data, idx.len);
    }

    // Single flush to SD
    m_sd_buffer.flush();
    m_sd_buffer.reset();

    m_log.info("VIDEO", "Clip closed: %s (%u frames, %u bytes)",
               m_file_path, m_frames_recorded, m_file_size);

    // Clean up
    m_frame_chunks.clear();
    m_frames_recorded = 0;
    m_file_size = 0;
    m_stop_clip = false;
}

// ============================================================
// Frame capture
// ============================================================

void VideoController::capture_and_write_frame()
{
    CameraCaptureResult result = m_camera.capture_frame();
    if (result != CameraCaptureResult::SUCCESS)
    {
        m_log.warn("VIDEO", "Frame capture failed");
        return;
    }

    hal::CameraFrame frame = m_camera.get_frame();

    // Format as AVI chunk
    m_avi_writer.write_frame(frame.data, frame.len);

    // Copy the chunk to our accumulator
    // (last_frame_chunk() is only valid until next write_frame())
    AviBytes chunk = m_avi_writer.last_frame_chunk();
    if (chunk.data && chunk.len > 0)
    {
        FrameChunk fc;
        fc.data.assign(chunk.data, chunk.data + chunk.len);
        m_frame_chunks.push_back(fc);
        m_file_size += static_cast<uint32_t>(chunk.len);
    }

    m_camera.release_frame();
    m_frames_recorded++;
}

// ============================================================
// Directory creation
// ============================================================

void VideoController::create_directory(const char *path)
{
    m_peripheral.create_dir(path, 1000);
}
