#include "video_controller.h"
#include "stream_service.h"
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
    , m_record_sink(*this)
    , m_stream_sink(*this)
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
    if (is_registered(&m_record_sink))
    {
        return false;
    }

    // Reject while a previously stopped clip has not been finalized by
    // the capture task yet (asynchronous stop when other consumers such
    // as streaming remain active). Opening a new clip before the old
    // one is closed would corrupt both files.
    if (m_clip_close_pending.load(std::memory_order_relaxed))
    {
        m_log.warn(SUBSYSTEM, "Recording start rejected: clip close pending");
        return false;
    }

    int id = find_free_slot();
    if (id < 0)
    {
        m_log.warn(SUBSYSTEM, "Recording start rejected: consumer registry full");
        return false;
    }

    m_stop_clip = false;
    open_clip();

    if (!m_avi_writer.is_open())
    {
        return false;
    }

    // Start the capture loop if it is not already running (reused when
    // e.g. streaming is subscribed).
    if (!ensure_loop_running())
    {
        close_clip();
        m_log.warn(SUBSYSTEM, "Recording start failed: scheduler busy");
        return false;
    }

    claim_slot(id, &m_record_sink);

    m_log.info(SUBSYSTEM, "Recording started: %s", m_file_path);
    return true;
}

void VideoController::stop_recording()
{
    int id = find_id(&m_record_sink);
    if (id < 0)
    {
        return;
    }

    // Request clip closure before detaching so the pipeline's final
    // callback (or the next frame callback when other consumers keep
    // the loop running) finalizes the clip.
    m_stop_clip = true;
    m_clip_close_pending = true;

    detach_consumer(id);

    if (!maybe_teardown_if_idle())
    {
        m_log.info(SUBSYSTEM,
                   "Recording stopped; capture loop serves remaining consumers, "
                   "clip closes on next frame");
    }
    else
    {
        m_log.info(SUBSYSTEM, "Recording stopped");
    }
}

// ============================================================
// Frame-consumer registry
// ============================================================

int VideoController::find_free_slot() const
{
    for (size_t i = 0; i < MAX_FRAME_CONSUMERS; i++)
    {
        if (!m_slots[i].used.load(std::memory_order_relaxed))
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int VideoController::find_id(const IFrameConsumer* consumer) const
{
    if (consumer == nullptr)
    {
        return -1;
    }
    for (size_t i = 0; i < MAX_FRAME_CONSUMERS; i++)
    {
        if (m_slots[i].used.load(std::memory_order_relaxed) &&
            m_slots[i].consumer == consumer)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool VideoController::is_registered(const IFrameConsumer* consumer) const
{
    return find_id(consumer) >= 0;
}

size_t VideoController::consumer_count() const
{
    size_t count = 0;
    for (size_t i = 0; i < MAX_FRAME_CONSUMERS; i++)
    {
        if (m_slots[i].used.load(std::memory_order_relaxed))
        {
            count++;
        }
    }
    return count;
}

void VideoController::claim_slot(int id, IFrameConsumer* consumer)
{
    if (id < 0 || id >= static_cast<int>(MAX_FRAME_CONSUMERS))
    {
        return;
    }
    m_slots[id].consumer = consumer;
    m_slots[id].used.store(true, std::memory_order_release);
}

void VideoController::detach_consumer(int id)
{
    if (id < 0 || id >= static_cast<int>(MAX_FRAME_CONSUMERS))
    {
        return;
    }
    // Clear the occupied flag first so an in-flight dispatch observes
    // an empty slot rather than a dangling pointer; the pointer follows.
    m_slots[id].used.store(false, std::memory_order_release);
    m_slots[id].consumer = nullptr;
}

int VideoController::register_frame_consumer(IFrameConsumer* consumer)
{
    if (consumer == nullptr || is_registered(consumer))
    {
        return -1;
    }

    int id = find_free_slot();
    if (id < 0)
    {
        m_log.warn(SUBSYSTEM, "Consumer registration rejected: registry full");
        return -1;
    }

    if (!ensure_loop_running())
    {
        m_log.warn(SUBSYSTEM, "Consumer registration rejected: scheduler busy");
        return -1;
    }

    claim_slot(id, consumer);
    return id;
}

void VideoController::unregister_frame_consumer(int id)
{
    if (id < 0 || id >= static_cast<int>(MAX_FRAME_CONSUMERS) ||
        !m_slots[id].used.load(std::memory_order_relaxed))
    {
        return;
    }

    detach_consumer(id);
    maybe_teardown_if_idle();
}

// ============================================================
// Photo capture
// ============================================================

bool VideoController::capture_photo(const char* path)
{
    if (is_capture_active())
    {
        m_log.warn(SUBSYSTEM, "capture_photo rejected: capture loop active");
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

void VideoController::set_stream_service(IStreamService* stream_service)
{
    m_stream_service = stream_service;
}

// ============================================================
// Streaming control
// ============================================================

bool VideoController::start_streaming()
{
    if (is_registered(&m_stream_sink))
    {
        // Preview is already subscribed — treat as a no-op success.
        return true;
    }

    int id = find_free_slot();
    if (id < 0)
    {
        m_log.warn(SUBSYSTEM, "Streaming start rejected: consumer registry full");
        return false;
    }

    // Start the capture loop if it is not already running (reused when
    // e.g. recording is subscribed).
    if (!ensure_loop_running())
    {
        m_log.warn(SUBSYSTEM, "Streaming start failed: scheduler busy");
        return false;
    }

    claim_slot(id, &m_stream_sink);

    m_log.info(SUBSYSTEM, "Streaming started");
    return true;
}

void VideoController::stop_streaming()
{
    int id = find_id(&m_stream_sink);
    if (id < 0)
    {
        return;
    }

    detach_consumer(id);
    m_log.info(SUBSYSTEM, "Streaming stopped");

    // Last consumer gone: tear down the loop and close all viewers.
    maybe_teardown_if_idle();
}

// ============================================================
// State queries
// ============================================================

bool VideoController::is_recording() const
{
    return is_registered(&m_record_sink);
}

bool VideoController::is_streaming() const
{
    return is_registered(&m_stream_sink);
}

bool VideoController::is_capture_active() const
{
    return consumer_count() > 0;
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
// Capture loop lifecycle
// ============================================================

bool VideoController::ensure_loop_running()
{
    if (m_loop_active)
    {
        return true;
    }

    m_loop_active = m_scheduler.start(m_frame_interval_ms,
                                      [this]() { capture_and_dispatch_frame(); });
    return m_loop_active;
}

void VideoController::teardown_capture_pipeline()
{
    // Stop the capture loop first: the scheduler's final callback runs
    // any pending clip closure, and after the (bounded) join no further
    // dispatch can race the writer close below.
    m_scheduler.stop();
    m_loop_active = false;

    // Close all connected viewers: with the loop gone they would
    // otherwise hang forever on a silent multipart response.
    if (m_stream_service != nullptr)
    {
        m_stream_service->stop_all_streams();
    }
}

bool VideoController::maybe_teardown_if_idle()
{
    if (consumer_count() == 0 && m_loop_active)
    {
        teardown_capture_pipeline();
        return true;
    }
    return false;
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
// Frame pump
// ============================================================

void VideoController::capture_and_dispatch_frame()
{
    bool closing = m_stop_clip.load(std::memory_order_relaxed);

    // Guard against stale callbacks (e.g. a task notification that was
    // already queued after teardown): never capture unless a consumer is
    // subscribed or a stopped clip still awaits its final closure.
    if (consumer_count() == 0 && !closing)
    {
        return;
    }

    CameraCaptureResult result = m_camera.capture_frame();
    if (result != CameraCaptureResult::SUCCESS)
    {
        m_log.warn(SUBSYSTEM, "Frame capture failed");

        // Never wedge the pipeline: if a stopped clip was awaiting its
        // final closure, finalize it even though this frame failed.
        if (closing)
        {
            close_clip();
            m_clip_close_pending = false;
        }
        return;
    }

    hal::CameraFrame frame = m_camera.get_frame();

    // Dispatch the raw JPEG frame to every subscribed consumer.
    for (size_t i = 0; i < MAX_FRAME_CONSUMERS; i++)
    {
        if (!m_slots[i].used.load(std::memory_order_acquire))
        {
            continue;
        }
        IFrameConsumer* consumer = m_slots[i].consumer;
        if (consumer != nullptr)
        {
            consumer->on_frame(frame.data, frame.len);
        }
    }

    m_camera.release_frame();

    handle_clip_closure();
}

void VideoController::write_avi_frame(const uint8_t* data, size_t len)
{
    // Format as AVI chunk and write to SD
    m_avi_writer.write_frame(data, len);

    AviBytes chunk = m_avi_writer.last_frame_chunk();
    if (chunk.data && chunk.len > 0)
    {
        write_to_sd(chunk.data, chunk.len);
        m_file_size += static_cast<uint32_t>(chunk.len);
    }

    m_frames_recorded++;

    // Check clip termination conditions (auto-rotation); closure itself
    // is handled by the pump after frame release.
    if (m_config.clip_duration_s > 0 &&
        m_frames_recorded >=
            static_cast<uint32_t>(m_config.fps) * m_config.clip_duration_s)
    {
        m_stop_clip = true;
    }
    else if (m_config.max_file_size_mb > 0 &&
             m_file_size >= static_cast<uint32_t>(m_config.max_file_size_mb) *
                                1024UL * 1024UL)
    {
        m_stop_clip = true;
    }
}

void VideoController::handle_clip_closure()
{
    if (!m_stop_clip.exchange(false, std::memory_order_relaxed))
    {
        return;
    }

    close_clip();
    m_clip_close_pending = false;

    if (is_recording())
    {
        // Auto-rotation: recording remains subscribed — open new clip
        // and resume.
        open_clip();
        if (m_avi_writer.is_open())
        {
            m_log.info(SUBSYSTEM, "New clip: %s", m_clip_path);
        }
    }
    // Explicit stop: the recording consumer is already detached; the
    // clip stays closed.
}

void VideoController::RecordSink::on_frame(const uint8_t* data, size_t len)
{
    m_vc.write_avi_frame(data, len);
}

void VideoController::StreamSink::on_frame(const uint8_t* data, size_t len)
{
    IStreamService* stream_service = m_vc.m_stream_service;
    if (stream_service != nullptr && data != nullptr && len > 0)
    {
        stream_service->deliver_frame(data, len);
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
