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
    , m_clip_session(sd, time, log)
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

    m_clip_session.init(config, width, height);

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
    std::lock_guard<std::mutex> lock(m_lifecycle_mutex);

    // Registry guard: the recording sink is registered exactly while
    // the activity is active, and this section is serialized against
    // all other lifecycle mutations by the mutex, so a duplicate start
    // can never slip through and a failure path needs no manual
    // claim reset.
    if (is_registered(&m_record_sink))
    {
        return false;
    }

    // Reject while an unfinalized clip is still open: after an
    // asynchronous stop (other consumers such as streaming kept the
    // loop running) the capture task finalizes the clip on its next
    // activation. Opening a new clip before the old one is closed
    // would corrupt both files. All clip operations run under the
    // lifecycle mutex, so this read is ordered against the capture
    // task's close/reopen sections.
    if (m_clip_session.close_pending())
    {
        m_log.warn(SUBSYSTEM, "Recording start rejected: clip close pending");
        return false;
    }

    int id = acquire_free_slot();
    if (id < 0)
    {
        m_log.warn(SUBSYSTEM, "Recording start rejected: consumer registry full");
        return false;
    }

    m_clip_session.clear_close_request();
    if (!m_clip_session.open())
    {
        release_slot(id);
        return false;
    }

    // Start the capture loop if it is not already running (reused when
    // e.g. streaming is subscribed).
    if (!ensure_loop_running())
    {
        m_clip_session.close();
        release_slot(id);
        m_log.warn(SUBSYSTEM, "Recording start failed: scheduler busy");
        return false;
    }

    claim_slot(id, &m_record_sink);

    m_log.info(SUBSYSTEM, "Recording started: %s", m_clip_session.file_path());
    return true;
}

void VideoController::stop_recording()
{
    std::unique_lock<std::mutex> lock(m_lifecycle_mutex);

    int id = find_id(&m_record_sink);
    if (id < 0)
    {
        return;
    }

    // Request clip closure before detaching so the pipeline's final
    // callback (or the next frame callback when other consumers keep
    // the loop running) finalizes the clip.
    m_clip_session.request_close();

    detach_consumer(id);

    if (!maybe_teardown_if_idle(lock))
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

int VideoController::acquire_free_slot()
{
    for (size_t i = 0; i < MAX_FRAME_CONSUMERS; i++)
    {
        bool expected = false;
        if (m_slots[i].used.compare_exchange_strong(expected, true,
                                                    std::memory_order_acq_rel))
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void VideoController::release_slot(int id)
{
    if (id < 0 || id >= static_cast<int>(MAX_FRAME_CONSUMERS))
    {
        return;
    }
    m_slots[id].used.store(false, std::memory_order_release);
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
    // The slot was reserved via acquire_free_slot(), so the used flag
    // is already set; binding the pointer after the reservation store
    // makes the pointer visible to the capture task once it observes
    // the flag (dispatch skips null consumers in the interim).
    m_slots[id].consumer = consumer;
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
    std::lock_guard<std::mutex> lock(m_lifecycle_mutex);

    if (consumer == nullptr || is_registered(consumer))
    {
        return -1;
    }

    int id = acquire_free_slot();
    if (id < 0)
    {
        m_log.warn(SUBSYSTEM, "Consumer registration rejected: registry full");
        return -1;
    }

    if (!ensure_loop_running())
    {
        release_slot(id);
        m_log.warn(SUBSYSTEM, "Consumer registration rejected: scheduler busy");
        return -1;
    }

    claim_slot(id, consumer);
    return id;
}

void VideoController::unregister_frame_consumer(int id)
{
    std::unique_lock<std::mutex> lock(m_lifecycle_mutex);

    if (id < 0 || id >= static_cast<int>(MAX_FRAME_CONSUMERS) ||
        !m_slots[id].used.load(std::memory_order_relaxed))
    {
        return;
    }

    detach_consumer(id);
    maybe_teardown_if_idle(lock);
}

// ============================================================
// Photo capture
// ============================================================

bool VideoController::capture_photo(const char* path)
{
    {
        // Serialize the gate check with lifecycle transitions so it
        // cannot pass while a concurrent start is mid-registration; the
        // SD I/O below runs without the mutex.
        std::lock_guard<std::mutex> lock(m_lifecycle_mutex);
        if (is_capture_active())
        {
            m_log.warn(SUBSYSTEM, "capture_photo rejected: capture loop active");
            return false;
        }
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
    std::lock_guard<std::mutex> lock(m_lifecycle_mutex);

    // Already active — treat as a no-op success. The registry guard is
    // exact here: this section is serialized against all other
    // lifecycle mutations by the mutex, and the streaming sink is
    // registered exactly while the activity is active.
    if (is_registered(&m_stream_sink))
    {
        return true;
    }

    int id = acquire_free_slot();
    if (id < 0)
    {
        m_log.warn(SUBSYSTEM, "Streaming start rejected: consumer registry full");
        return false;
    }

    // Start the capture loop if it is not already running (reused when
    // e.g. recording is subscribed).
    if (!ensure_loop_running())
    {
        release_slot(id);
        m_log.warn(SUBSYSTEM, "Streaming start failed: scheduler busy");
        return false;
    }

    claim_slot(id, &m_stream_sink);

    // A deferred viewer close from a stop/restart within one frame
    // period is obsolete: streaming mode is active again.
    m_close_viewers_pending.store(false, std::memory_order_release);

    m_log.info(SUBSYSTEM, "Streaming started");
    return true;
}

void VideoController::stop_streaming()
{
    std::unique_lock<std::mutex> lock(m_lifecycle_mutex);

    int id = find_id(&m_stream_sink);
    if (id < 0)
    {
        return;
    }

    detach_consumer(id);
    m_log.info(SUBSYSTEM, "Streaming stopped");

    // Viewers exist only while streaming mode is active: close them as
    // soon as the StreamSink detaches, even when other consumers keep
    // the capture loop running for recording. When the loop survives,
    // the close is deferred to the next dispatch boundary so it cannot
    // interleave with an in-flight deliver_frame() inside the capture
    // task; full teardown closes the viewers itself after the join.
    if (!maybe_teardown_if_idle(lock))
    {
        // Only defer when the loop survives for other consumers; a dead
        // or already-torn-down loop has no dispatch boundary to drain
        // the flag, and full teardown closes the viewers itself.
        if (m_loop_active.load(std::memory_order_acquire))
        {
            m_close_viewers_pending.store(true, std::memory_order_release);
        }
    }
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
    return m_clip_session.frames_recorded();
}

uint32_t VideoController::file_size() const
{
    return m_clip_session.file_size();
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

bool VideoController::teardown_capture_pipeline(
    std::unique_lock<std::mutex>& lock)
{
    // Single-flight entry: the platform scheduler's stop() is not
    // reentrant-safe, so only one caller at a time may drive the join.
    // A caller arriving while another teardown owns the join window
    // skips its own stop; the owner's post-join recheck below observes
    // the final registry state and completes or defers correctly.
    if (m_teardown_in_progress.exchange(true, std::memory_order_acq_rel))
    {
        return false;
    }

    // A viewer close queued by stop_streaming() is subsumed by the
    // synchronous close below; clear it first so the scheduler's final
    // callback does not run a duplicate close. If this teardown aborts
    // below, the flag is restored for activities that still need it.
    const bool was_pending =
        m_close_viewers_pending.exchange(false, std::memory_order_release);

    // Release the lifecycle mutex around the scheduler stop/join: a
    // consumer invoking start_recording() from within on_frame() must
    // never block behind this joiner, or the bounded join would wait on
    // a task waiting on us and wedge the scheduler until reboot.
    lock.unlock();
    m_scheduler.stop();
    lock.lock();

    if (consumer_count() > 0)
    {
        // A concurrent start registered consumers while the mutex was
        // released: honor it by restoring the loop and deferring this
        // teardown to a later stop. Under pathological start/stop churn
        // a teardown attempt may be deferred repeatedly, but it can
        // never complete under a live registry.
        m_loop_active = false;
        if (!ensure_loop_running())
        {
            m_log.warn(SUBSYSTEM, "Teardown aborted: loop restart failed");
        }
        if (was_pending && !is_streaming())
        {
            m_close_viewers_pending.store(true, std::memory_order_release);
        }
        m_teardown_in_progress.store(false, std::memory_order_release);
        return false;
    }

    m_loop_active = false;

    // Close all connected viewers: with the loop gone they would
    // otherwise hang forever on a silent multipart response.
    if (m_stream_service != nullptr)
    {
        m_stream_service->stop_all_streams();
    }

    // Any deferred viewer close requested while the mutex was released
    // during the join is subsumed by the synchronous close above.
    m_close_viewers_pending.store(false, std::memory_order_release);

    m_teardown_in_progress.store(false, std::memory_order_release);
    return true;
}

bool VideoController::maybe_teardown_if_idle(
    std::unique_lock<std::mutex>& lock)
{
    if (consumer_count() == 0 && m_loop_active)
    {
        return teardown_capture_pipeline(lock);
    }
    return false;
}

// ============================================================
// Clip lifecycle
// ============================================================

void VideoController::process_pending_viewer_close()
{
    if (m_close_viewers_pending.exchange(false, std::memory_order_relaxed) &&
        m_stream_service != nullptr)
    {
        m_stream_service->stop_all_streams();
    }
}

// ============================================================
// Frame pump
// ============================================================

void VideoController::capture_and_dispatch_frame()
{
    bool closing = m_clip_session.is_close_requested();

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
        // Clip operations are mutually exclusive against command-task
        // lifecycle mutations: the lock here covers the capture-failure
        // finalization the same way the closure handler's lock does, so a
        // concurrent start_recording() cannot race an in-flight clip
        // close (torn-clip class). Critical sections on the command side
        // are bounded, and stop paths release the mutex across the
        // scheduler join, so this acquisition never blocks behind a
        // joiner.
        if (closing)
        {
            std::lock_guard<std::mutex> lock(m_lifecycle_mutex);
            m_clip_session.close();
        }
        process_pending_viewer_close();
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

    // Drain a viewer close deferred by stop_streaming() while other
    // consumers kept the loop alive: running here serializes the close
    // with frame delivery inside the capture task, regardless of
    // whether this frame captured successfully.
    process_pending_viewer_close();
}

void VideoController::handle_clip_closure()
{
    // Serialize the whole close/reopen decision against command-task
    // lifecycle mutations: a STOP/START landing inside the rotation
    // window can no longer interleave with the capture task's clip
    // finalization or reopen (torn-clip class). The critical section is
    // bounded SD I/O; stop paths release the mutex across the scheduler
    // join, so this acquisition never blocks behind a joiner.
    std::lock_guard<std::mutex> lock(m_lifecycle_mutex);

    if (!m_clip_session.consume_close_request())
    {
        return;
    }

    m_clip_session.close();

    if (is_recording())
    {
        // Auto-rotation: recording remains subscribed — open new clip
        // and resume.
        if (m_clip_session.open())
        {
            m_log.info(SUBSYSTEM, "New clip: %s", m_clip_session.clip_path());
        }
    }
    // Explicit stop: the recording consumer is already detached; the
    // clip stays closed.
}

void VideoController::RecordSink::on_frame(const uint8_t* data, size_t len)
{
    // Lock-free pump path into the clip session (see the session's
    // locking contract in avi_clip_session.h).
    m_vc.m_clip_session.append_frame(data, len);
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
