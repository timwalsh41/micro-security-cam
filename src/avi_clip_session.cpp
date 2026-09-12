#include "avi_clip_session.h"

#include "path_utils.h"
#include "time_formatter.h"

#include <cstdio>
#include <cstring>

AviClipSession::AviClipSession(SdController& sd, TimeService& time,
                               LoggingService& log)
    : m_sd(sd)
    , m_time(time)
    , m_log(log)
{
}

bool AviClipSession::init(const ConfigData::VideoConfig& config,
                          uint16_t width, uint16_t height)
{
    m_config = config;
    m_width = width;
    m_height = height;
    return true;
}

bool AviClipSession::open()
{
    // Build file path from unix timestamp (UTC)
    uint32_t seconds = m_time.unix_now();

    char date[9];
    char time_str[7];
    if (!TimeFormatter::format_date(seconds, date, sizeof(date)) ||
        !TimeFormatter::format_time(seconds, time_str, sizeof(time_str)))
    {
        m_log.warn(SUBSYSTEM, "Failed to format timestamp");
        return false;
    }

    // Build directory path: /YYYYMMDD
    char dir_path[16];
    snprintf(dir_path, sizeof(dir_path), "/%s", date);

    // Create the parent directory on the SD card
    m_sd.create_dir_sync(dir_path);

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
        return false;
    }

    // Store clip path for close()
    strncpy(m_clip_path, m_file_path, sizeof(m_clip_path) - 1);
    m_clip_path[sizeof(m_clip_path) - 1] = '\0';

    // Open AVI writer (generates header in memory).
    if (!m_avi_writer.open(m_file_path, m_config.fps, m_width, m_height))
    {
        return false;
    }

    // Write placeholder header to SD at position 0. This is the real
    // card probe: a failing write means no clip can be recorded, so
    // the open fails before the close-pending guard is latched and
    // before any frame could be silently discarded.
    AviBytes hdr = m_avi_writer.header();
    if (hdr.data && hdr.len > 0 && !write_to_sd(hdr.data, hdr.len))
    {
        m_log.error(SUBSYSTEM, "Clip header write failed: %s", m_file_path);
        m_avi_writer = AviWriter();  // Reset writer state: no clip is open
        return false;
    }

    // The clip awaits finalization from this point on; close() clears
    // the guard once finalization completes.
    m_close_pending = true;

    // Reset clip state
    m_frames_recorded = 0;
    m_file_size = 0;
    return true;
}

void AviClipSession::close()
{
    // Finalize AVI (patches header, builds index)
    m_avi_writer.close();

    // Append index to the file
    AviBytes idx = m_avi_writer.index();
    if (idx.data && idx.len > 0 && !write_to_sd(idx.data, idx.len))
    {
        m_log.error(SUBSYSTEM, "Clip index write failed: %s", m_clip_path);
    }

    // Seek to position 0 and overwrite the placeholder header
    AviBytes hdr = m_avi_writer.patched_header();
    if (hdr.data && hdr.len > 0 && !write_at_offset(m_clip_path, 0, hdr.data, hdr.len))
    {
        m_log.error(SUBSYSTEM, "Clip header patch write failed: %s", m_clip_path);
    }

    m_log.info(SUBSYSTEM, "Clip closed: %s (%u frames, %u bytes)",
               m_clip_path,
               static_cast<uint32_t>(m_frames_recorded),
               static_cast<uint32_t>(m_file_size));

    // Clean up
    m_frames_recorded = 0;
    m_file_size = 0;
    m_close_requested = false;
    m_avi_writer = AviWriter();  // Reset writer state

    // No unfinalized clip remains.
    m_close_pending = false;
}

void AviClipSession::append_frame(const uint8_t* data, size_t len)
{
    // Format as AVI chunk and write to SD
    m_avi_writer.write_frame(data, len);

    AviBytes chunk = m_avi_writer.last_frame_chunk();
    if (chunk.data && chunk.len > 0)
    {
        // Count only bytes actually written so the reported file size
        // stays truthful when writes fail (e.g. card removed mid-recording).
        if (write_to_sd(chunk.data, chunk.len))
        {
            m_file_size += static_cast<uint32_t>(chunk.len);
        }
    }

    m_frames_recorded++;

    // Check clip termination conditions (auto-rotation); closure itself
    // is handled by the pump's closure boundary after frame release.
    if (m_config.clip_duration_s > 0 &&
        m_frames_recorded >=
            static_cast<uint32_t>(m_config.fps) * m_config.clip_duration_s)
    {
        m_close_requested = true;
    }
    else if (m_config.max_file_size_mb > 0 &&
             m_file_size >= static_cast<uint32_t>(m_config.max_file_size_mb) *
                                1024UL * 1024UL)
    {
        m_close_requested = true;
    }
}

void AviClipSession::request_close()
{
    m_close_requested = true;
}

bool AviClipSession::is_close_requested() const
{
    return m_close_requested.load(std::memory_order_relaxed);
}

bool AviClipSession::consume_close_request()
{
    return m_close_requested.exchange(false, std::memory_order_relaxed);
}

void AviClipSession::clear_close_request()
{
    m_close_requested = false;
}

bool AviClipSession::is_open() const
{
    return m_avi_writer.is_open();
}

bool AviClipSession::close_pending() const
{
    return m_close_pending;
}

uint32_t AviClipSession::frames_recorded() const
{
    return m_frames_recorded;
}

uint32_t AviClipSession::file_size() const
{
    return m_file_size;
}

const char* AviClipSession::file_path() const
{
    return m_file_path;
}

const char* AviClipSession::clip_path() const
{
    return m_clip_path;
}

bool AviClipSession::make_unique_path(const char* base_path, char* out,
                                      size_t out_size)
{
    return PathUtils::make_unique_path(
        base_path,
        [this](const char* path) { return m_sd.exists_sync(path); },
        out,
        out_size);
}

bool AviClipSession::write_to_sd(const uint8_t* data, size_t len)
{
    return m_sd.write_sync(m_file_path, data, len,
                           hal::FileOpenMode::APPEND);
}

bool AviClipSession::write_at_offset(const char* path, uint32_t offset,
                                     const uint8_t* data, size_t len)
{
    return m_sd.write_at_sync(path, offset, data, len);
}
