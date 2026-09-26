#include "video_controller.h"
#include "stream_service.h"
#include <chrono>
#include <cstring>
#include <cstdio>
#include <thread>

// Out-of-line definition for the static constexpr member declared in
// the header; required for odr-uses such as binding to
// std::chrono::milliseconds' const& parameter.
constexpr uint32_t VideoController::REQUEST_COMPLETION_TIMEOUT_MS;

namespace
{
/**
 * @brief True while the calling thread runs an executor activation.
 *
 * Set only by the capture task (or the native test thread driving the
 * scheduler callback) around capture_and_dispatch_frame(); command APIs
 * read it to detect calls made from within on_frame() and turn them
 * into fire-and-forget requests. Thread-local by design: another task
 * holding the executor token does not make a command fire-and-forget.
 */
thread_local bool t_executor_context = false;
}


// ============================================================
// Construction
// ============================================================

VideoController::VideoController(CameraController& cam,
                                 SdController& sd,
                                 TimeService& time,
                                 LoggingService& log,
                                 ICaptureScheduler& scheduler,
                                 MicService* mic)
    : m_camera(cam)
    , m_sd(sd)
    , m_time(time)
    , m_log(log)
    , m_scheduler(scheduler)
    , m_mic(mic)
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

    // Compute audio chunk size: samples per frame at 16kHz/16-bit/mono
    m_audio_chunk_size = MicService::SAMPLE_RATE / m_config.fps;  // samples per frame
    m_audio_buffer.resize(m_audio_chunk_size);

    m_log.info(SUBSYSTEM, "Init: %ufps %ux%u, clip %us, quality %u",
               m_config.fps, m_width, m_height,
               m_config.clip_duration_s, m_config.jpeg_quality);

    if (m_config.audio_enabled)
    {
        m_log.info(SUBSYSTEM, "Audio enabled: %ukHz 16-bit mono, chunk %u samples",
                   static_cast<unsigned>(MicService::SAMPLE_RATE / 1000),
                   static_cast<unsigned>(m_audio_chunk_size));
    }

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

    std::shared_ptr<CommandCompletion> completion = post_request(
        PipelineRequestType::START_RECORDING, nullptr, -1);
    if (!completion)
    {
        return false;
    }

    if (t_executor_context)
    {
        // Executor context (a consumer's on_frame): fire-and-forget.
        // The request is consumed at this activation's boundary or the
        // next one; waiting here would deadlock the capture task
        // against itself.
        return true;
    }

    bool ok = false;
    const bool completed = wait_for_completion(completion, &ok, nullptr);
    finish_command();
    return completed && ok;
}

void VideoController::stop_recording()
{
    std::shared_ptr<CommandCompletion> completion;
    {
        std::lock_guard<std::mutex> lock(m_command_mutex);

        // Post the stop whenever the capture loop runs, even though the
        // sink is not bound yet: a queued-but-unexecuted start keeps the
        // loop active, and posting keeps start/stop totally ordered
        // through the executor (the executor no-ops a stop that finds
        // nothing bound). With the loop down no start can be pending,
        // so the stop is a no-op.
        if (!m_loop_active.load(std::memory_order_relaxed))
        {
            return;
        }
        completion = post_request_locked(
            PipelineRequestType::STOP_RECORDING, nullptr, -1);
    }
    if (!completion)
    {
        return;
    }

    if (!t_executor_context)
    {
        bool ok = false;
        wait_for_completion(completion, &ok, nullptr);
        finish_command();
    }
    // Executor context: fire-and-forget; consumed at the dispatch
    // boundary (see start_recording()).
}

// ============================================================
// Frame-consumer registry
// ============================================================

int VideoController::acquire_free_slot()
{
    // Executor-only: no CAS needed, slots are mutated exclusively in
    // the capture task.
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
            m_slots[i].consumer.load(std::memory_order_relaxed) == consumer)
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

void VideoController::bind_slot(int id, IFrameConsumer* consumer)
{
    if (id < 0 || id >= static_cast<int>(MAX_FRAME_CONSUMERS))
    {
        return;
    }
    // The slot was reserved via acquire_free_slot(), so the used flag
    // is already set; binding the pointer completes the registration.
    m_slots[id].consumer.store(consumer, std::memory_order_release);
}

void VideoController::detach_consumer(int id)
{
    if (id < 0 || id >= static_cast<int>(MAX_FRAME_CONSUMERS))
    {
        return;
    }
    // Clear the occupied flag first so a concurrent status query
    // observes an empty slot; the pointer follows.
    m_slots[id].used.store(false, std::memory_order_release);
    m_slots[id].consumer.store(nullptr, std::memory_order_release);
}

int VideoController::register_frame_consumer(IFrameConsumer* consumer)
{
    if (consumer == nullptr || is_registered(consumer))
    {
        return -1;
    }

    std::shared_ptr<CommandCompletion> completion = post_request(
        PipelineRequestType::REGISTER_CONSUMER, consumer, -1);
    if (!completion)
    {
        return -1;
    }

    if (t_executor_context)
    {
        // Executor context: fire-and-forget; the id is not knowable
        // synchronously.
        return -1;
    }

    bool ok = false;
    int id = -1;
    if (!wait_for_completion(completion, &ok, &id))
    {
        return -1;
    }
    finish_command();
    return ok ? id : -1;
}

void VideoController::unregister_frame_consumer(int id)
{
    // Validate against a registry snapshot; the executor re-validates
    // exactly (slot id and bound consumer pointer) before detaching, so
    // a stale snapshot can never detach a rebound slot.
    if (id < 0 || id >= static_cast<int>(MAX_FRAME_CONSUMERS) ||
        !m_slots[id].used.load(std::memory_order_relaxed))
    {
        return;
    }
    IFrameConsumer* expected =
        m_slots[id].consumer.load(std::memory_order_relaxed);

    std::shared_ptr<CommandCompletion> completion = post_request(
        PipelineRequestType::UNREGISTER_CONSUMER, expected, id);
    if (!completion)
    {
        return;
    }

    if (!t_executor_context)
    {
        bool ok = false;
        wait_for_completion(completion, &ok, nullptr);
        finish_command();
    }
    // Executor context: fire-and-forget (see start_recording()).
}

// ============================================================
// Photo capture
// ============================================================

bool VideoController::capture_photo(const char* path)
{
    if (path == nullptr || path[0] == '\0')
    {
        return false;
    }

    std::shared_ptr<CommandCompletion> completion = post_request(
        PipelineRequestType::TAKE_PHOTO, nullptr, -1, path);
    if (!completion)
    {
        return false;
    }

    if (t_executor_context)
    {
        // Executor context (a consumer's on_frame): fire-and-forget.
        // The request is consumed at this activation's boundary or the
        // next one; waiting here would deadlock the capture task
        // against itself (see start_recording()).
        return true;
    }

    bool ok = false;
    const bool completed = wait_for_completion(completion, &ok, nullptr);
    finish_command();
    return completed && ok;
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
    // Already active — treat as a no-op success.
    if (is_registered(&m_stream_sink))
    {
        return true;
    }

    std::shared_ptr<CommandCompletion> completion = post_request(
        PipelineRequestType::START_STREAMING, nullptr, -1);
    if (!completion)
    {
        return false;
    }

    if (t_executor_context)
    {
        return true;  // executor context: fire-and-forget
    }

    bool ok = false;
    const bool completed = wait_for_completion(completion, &ok, nullptr);
    finish_command();
    return completed && ok;
}

void VideoController::stop_streaming()
{
    std::shared_ptr<CommandCompletion> completion;
    {
        std::lock_guard<std::mutex> lock(m_command_mutex);

        // Post the stop whenever the capture loop runs (see
        // stop_recording() for the ordering rationale).
        if (!m_loop_active.load(std::memory_order_relaxed))
        {
            return;
        }
        completion = post_request_locked(
            PipelineRequestType::STOP_STREAMING, nullptr, -1);
    }
    if (!completion)
    {
        return;
    }

    if (!t_executor_context)
    {
        bool ok = false;
        wait_for_completion(completion, &ok, nullptr);
        finish_command();
    }
    // Executor context: fire-and-forget (see start_recording()).
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

const char* VideoController::current_clip_path() const
{
    return m_clip_session.clip_path();
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

    // Start with the mutex held (command side); the executor's first
    // activation serves any request queued before this call.
    m_loop_active = m_scheduler.start(m_frame_interval_ms,
                                      [this]() { capture_and_dispatch_frame(); });
    return m_loop_active;
}

void VideoController::finish_command()
{
    std::unique_lock<std::mutex> lock(m_command_mutex);

    // The executor alone reports the transition to a drained registry,
    // and the flag is consumed exactly once, so concurrent command
    // tasks collapse into one join.
    if (!m_pipeline_drained.exchange(false, std::memory_order_acq_rel))
    {
        return;
    }

    if (m_join_in_progress)
    {
        // Another command task owns the join window (a drain arriving
        // while a join is in flight). Its post-join recheck observes the
        // final registry state and completes the teardown.
        return;
    }
    m_join_in_progress = true;

    // The bounded scheduler join runs on the command side with the
    // mutex released: the executor never waits on a lock holder, and no
    // joiner holds the mutex the executor could be blocked behind.
    lock.unlock();
    m_scheduler.stop();
    lock.lock();

    m_loop_active = false;
    m_join_in_progress = false;

    if (consumer_count() > 0 ||
        m_queued.load(std::memory_order_acquire) > 0)
    {
        // A concurrent command registered consumers while the mutex was
        // released (its request was consumed by the join's final
        // callback), or another command was posted during the join
        // window and is still queued: honor it by restoring the loop
        // and deferring this teardown to a later drain.
        //
        // Registry-empty-but-queued subcase: the existing viewers
        // belong to the ended session — close them now (the deferred
        // close is consumed exactly once, and the queued work
        // re-establishes its own state at its next boundary). Re-arm
        // m_pipeline_drained so the queued work's own command
        // completion re-decides the teardown at the next
        // finish_command().
        if (consumer_count() == 0 &&
            m_queued.load(std::memory_order_acquire) > 0)
        {
            if (m_close_viewers_pending.load(std::memory_order_acquire))
            {
                m_close_viewers_pending.store(false, std::memory_order_release);
                if (m_stream_service != nullptr)
                {
                    m_stream_service->stop_all_streams();
                }
            }
            m_pipeline_drained.store(true, std::memory_order_release);
        }
        if (!ensure_loop_running())
        {
            m_log.warn(SUBSYSTEM, "Teardown aborted: loop restart failed");
        }
        return;
    }

    // Close all connected viewers: with the loop gone they would
    // otherwise hang forever on a silent multipart response. Every
    // drain arms the deferred close, consumed exactly once: if the
    // executor's final join activation already closed the viewers, the
    // flag is clear and this close is skipped. Reads/clears are
    // mutually exclusive with the serve path (also under
    // m_command_mutex) and ordered against the executor's writers
    // (process_pending_viewer_close, executor_start_streaming's stale
    // clear, execute_request's drain arm) by the atomic contract.
    if (m_close_viewers_pending.load(std::memory_order_acquire))
    {
        m_close_viewers_pending.store(false, std::memory_order_release);
        if (m_stream_service != nullptr)
        {
            m_stream_service->stop_all_streams();
        }
    }
}

// ============================================================
// Clip lifecycle
// ============================================================

void VideoController::process_pending_viewer_close()
{
    // Executor-only flag: the close runs at the dispatch boundary so it
    // cannot interleave with an in-flight deliver_frame() in this task.
    if (m_close_viewers_pending.load(std::memory_order_acquire) &&
        m_stream_service != nullptr)
    {
        m_close_viewers_pending.store(false, std::memory_order_release);
        m_stream_service->stop_all_streams();
    }
}

// ============================================================
// Frame pump
// ============================================================

void VideoController::capture_and_dispatch_frame()
{
    // Executor entry: everything in this activation runs against the
    // executor-owned state under the executor token. The token is held
    // by a waiting command task only while this task is between
    // activations, so the wait here is bounded by that task's bounded
    // serve section (never by a scheduler join).
    bool expected = false;
    while (!m_executor_active.compare_exchange_strong(expected, true,
                                                      std::memory_order_acq_rel))
    {
        expected = false;
        std::this_thread::yield();
    }
    t_executor_context = true;

    // Executor boundary: serve queued pipeline commands first, so their
    // effects (bindings, detachments, clip operations) are applied
    // before this activation's capture. An activation that consumed
    // requests is a pure service pass and captures nothing: commands
    // take effect at the boundary, frames flow on the next one.
    if (process_requests() > 0)
    {
        t_executor_context = false;
        m_executor_active.store(false, std::memory_order_release);
        return;
    }

    bool closing = m_clip_session.is_close_requested();

    // Guard against stale callbacks (e.g. a task notification that was
    // already queued after teardown): never capture unless a consumer is
    // subscribed or a stopped clip still awaits its final closure. The
    // idle pass still drains a viewer close deferred by a fire-and-forget
    // stop so connected viewers never hang on a silent response.
    if (consumer_count() == 0 && !closing)
    {
        process_pending_viewer_close();
        t_executor_context = false;
        m_executor_active.store(false, std::memory_order_release);
        return;
    }

    CameraCaptureResult result = m_camera.capture_frame();
    if (result != CameraCaptureResult::SUCCESS)
    {
        m_log.warn(SUBSYSTEM, "Frame capture failed");

        // Never wedge the pipeline: if a stopped clip was awaiting its
        // final closure, finalize it even though this frame failed.
        // Clip operations are executor-only (task affinity), so this
        // finalization is mutually exclusive against command-side
        // lifecycle mutations by construction.
        if (closing)
        {
            m_clip_session.close();
        }
        process_pending_viewer_close();
        t_executor_context = false;
        m_executor_active.store(false, std::memory_order_release);
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
        IFrameConsumer* consumer = m_slots[i].consumer.load(
            std::memory_order_acquire);
        if (consumer != nullptr)
        {
            consumer->on_frame(frame.data, frame.len);
        }
    }

    m_camera.release_frame();

    // Drain audio from ring buffer and write to AVI at activation boundaries
    drain_audio_to_clip();

    handle_clip_closure();

    // Drain a viewer close deferred by a stop_streaming command while
    // other consumers kept the loop alive: running here serializes the
    // close with frame delivery inside the capture task, regardless of
    // whether this frame captured successfully.
    process_pending_viewer_close();

    t_executor_context = false;
    m_executor_active.store(false, std::memory_order_release);
}

void VideoController::handle_clip_closure()
{
    // Executor-only: the close/finalize/reopen decision runs in the
    // capture task; commands can only change the registry at activation
    // boundaries (their requests are consumed at the top of this
    // function's owning activation), so the decision is totally ordered
    // against command-side lifecycle mutations by task affinity.
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
// Executor: command queue
// ============================================================
// The capture task is the sole executor of pipeline state. Command
// tasks post requests into a fixed ring and wait (bounded) for the
// executor to consume them at the next dispatch boundary; a command
// issued from within on_frame() is fire-and-forget. See the class
// docstring in video_controller.h for the concurrency contract.

std::shared_ptr<VideoController::CommandCompletion> VideoController::post_request(
    PipelineRequestType type, IFrameConsumer* consumer, int slot_id,
    const char* photo_path)
{
    std::lock_guard<std::mutex> lock(m_command_mutex);
    return post_request_locked(type, consumer, slot_id, photo_path);
}

std::shared_ptr<VideoController::CommandCompletion>
VideoController::post_request_locked(
    PipelineRequestType type, IFrameConsumer* consumer, int slot_id,
    const char* photo_path)
{
    // The ring handoff runs under m_queue_mutex so the executor's
    // claim-and-snapshot of a claimed batch can never interleave with a
    // poster overwriting a slot (possible when the ring was full and a
    // poster's tail had wrapped onto the batch head).
    std::lock_guard<std::mutex> queue_lock(m_queue_mutex);

    if (m_queued.load(std::memory_order_relaxed) >=
        static_cast<int>(MAX_PENDING_REQUESTS))
    {
        m_log.warn(SUBSYSTEM, "Command rejected: request queue full");
        return nullptr;
    }

    const int tail = m_queue_tail.load(std::memory_order_relaxed);
    PipelineRequest& req = m_requests[tail];
    req.type = type;
    req.consumer = consumer;
    req.slot_id = slot_id;
    if (photo_path != nullptr)
    {
        // The request owns the path: a waiter that times out may be
        // gone by the time the executor consumes the request, so the
        // path cannot reference the caller's buffer.
        strncpy(req.photo_path, photo_path, sizeof(req.photo_path) - 1);
        req.photo_path[sizeof(req.photo_path) - 1] = '\0';
    }
    else
    {
        req.photo_path[0] = '\0';
    }
    req.completion = std::make_shared<CommandCompletion>();

    // Make sure an executor exists to serve the request. The loop's
    // first activation consumes everything published so far. The request
    // is published (tail advanced, count bumped) only once the scheduler
    // can run: an unpublished slot is invisible to the executor and is
    // simply overwritten by the next post.
    if (!ensure_loop_running())
    {
        // The scheduler cannot run (wedged or refused start): the
        // command is dropped unexecuted rather than left queued forever.
        m_log.error(SUBSYSTEM,
                    "Command dropped: capture scheduler unavailable");
        return nullptr;
    }

    m_queue_tail.store((tail + 1) % static_cast<int>(MAX_PENDING_REQUESTS),
                       std::memory_order_release);
    m_queued.fetch_add(1, std::memory_order_release);

    return req.completion;
}

bool VideoController::wait_for_completion(
    const std::shared_ptr<CommandCompletion>& completion,
    bool* ok, int* id)
{
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() +
                          std::chrono::milliseconds(REQUEST_COMPLETION_TIMEOUT_MS);

    // The executor signals completion via the shared box; the command
    // task waits with m_command_mutex released, so it can never block
    // the capture task behind a lock holder. Typical wait: at most one
    // frame interval (the executor's next dispatch boundary) plus clip
    // SD I/O. While the capture task is between activations, the
    // waiting task may claim the executor token (try-acquire — it never
    // blocks the capture task) and serve the queued batch itself; on
    // the platform scheduler the capture task usually consumes the
    // request first at its own boundary.
    while (!completion->done.load(std::memory_order_acquire))
    {
        if (clock::now() >= deadline)
        {
            m_log.error(SUBSYSTEM,
                        "Command timed out waiting for the capture task; "
                        "it may still execute later");
            return false;
        }

        // Hold m_command_mutex across the token claim and the serve so
        // this critical section is mutually exclusive with
        // finish_command()'s post-join section (which holds the same
        // mutex). The capture task's activation never takes
        // m_command_mutex, so the executor token spin is still bounded
        // by this bounded serve work and the capture task cannot block
        // behind the mutex.
        std::lock_guard<std::mutex> serve_lock(m_command_mutex);
        bool expected = false;
        if (m_executor_active.compare_exchange_strong(expected, true,
                                                       std::memory_order_acq_rel))
        {
            process_requests();
            m_executor_active.store(false, std::memory_order_release);
        }
        else
        {
            // Token held by an in-flight capture-task activation: yield
            // and retry on the next iteration (the mutex is released
            // when serve_lock goes out of scope here).
            std::this_thread::yield();
        }
    }

    *ok = completion->ok;
    if (id != nullptr)
    {
        *id = completion->id;
    }
    return true;
}

int VideoController::process_requests()
{
    // Claim the whole queued batch and snapshot it into local storage
    // under m_queue_mutex: posters taking the same mutex can then never
    // overwrite a slot this batch has claimed but not yet read, and the
    // executor works on its own copies from here on. m_queue_head is
    // executor-only.
    PipelineRequest batch[MAX_PENDING_REQUESTS];
    int n = 0;
    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        n = m_queued.exchange(0, std::memory_order_acq_rel);
        if (n <= 0)
        {
            return 0;
        }
        for (int i = 0; i < n; i++)
        {
            const int slot =
                (m_queue_head + i) % static_cast<int>(MAX_PENDING_REQUESTS);
            batch[i] = m_requests[slot];
        }
        m_queue_head =
            (m_queue_head + n) % static_cast<int>(MAX_PENDING_REQUESTS);
    }

    bool detached = false;
    bool photo_served = false;
    for (int i = 0; i < n; i++)
    {
        execute_request(batch[i], &detached, &photo_served);
    }

    // Batch-level drain arming: the executor alone detects the
    // transition to a drained registry (a no-op stop on an already-empty
    // registry does not re-signal); a waiting command task consumes this
    // signal exactly once and drives the bounded join, whose synchronous
    // close handles the viewers. A fire-and-forget request (from
    // on_frame()) or one whose waiter has timed out has no command task
    // guaranteed to consume the signal: the drained signal stays armed
    // for the next command task of any kind, and the deferred viewer
    // close is armed unconditionally so the executor closes connected
    // viewers itself at the next activation boundary and they never hang
    // in the meantime (the close is consumed exactly once, so a drain
    // never closes the viewers twice).
    //
    // A served TAKE_PHOTO counts like a detach when the registry is
    // empty afterwards: the photo request started the loop (or found it
    // running), and with no consumer left subscribed the loop has
    // nothing to serve, so the waiter's command completion tears it
    // down. The decision runs against the final registry state of the
    // batch: a sequence like UNREGISTER_CONSUMER(A) followed by
    // REGISTER_CONSUMER(B) re-fills the registry, so a per-request
    // arming after UNREGISTER would spuriously stop_all_streams()
    // connected viewers despite B still subscribing.
    if ((detached || photo_served) && consumer_count() == 0)
    {
        m_close_viewers_pending.store(true, std::memory_order_release);
        m_pipeline_drained.store(true, std::memory_order_release);
    }

    // Signal completions only after the whole batch executed.
    for (int i = 0; i < n; i++)
    {
        if (batch[i].completion)
        {
            batch[i].completion->ok = batch[i].result_ok;
            batch[i].completion->id = batch[i].result_id;
            batch[i].completion->done.store(true, std::memory_order_release);
        }
    }
    return n;
}

void VideoController::execute_request(PipelineRequest& req,
                                      bool* detached, bool* photo_served)
{
    bool ok = false;
    int id = -1;

    switch (req.type)
    {
        case PipelineRequestType::START_RECORDING:
            ok = executor_start_recording();
            break;
        case PipelineRequestType::STOP_RECORDING:
            *detached = executor_stop_recording() || *detached;
            ok = true;
            break;
        case PipelineRequestType::START_STREAMING:
            ok = executor_start_streaming();
            break;
        case PipelineRequestType::STOP_STREAMING:
            *detached = executor_stop_streaming() || *detached;
            ok = true;
            break;
        case PipelineRequestType::REGISTER_CONSUMER:
            id = executor_register(req.consumer);
            ok = id >= 0;
            break;
        case PipelineRequestType::UNREGISTER_CONSUMER:
            *detached = executor_unregister(req.consumer, req.slot_id) || *detached;
            ok = true;
            break;
        case PipelineRequestType::TAKE_PHOTO:
            ok = executor_take_photo(req.photo_path);
            *photo_served = true;
            break;
    }

    req.result_ok = ok;
    req.result_id = id;
}

bool VideoController::executor_start_recording()
{
    if (is_registered(&m_record_sink))
    {
        return false;
    }

    // Reject while an unfinalized clip is still open: opening a new
    // clip before the old one is closed would corrupt both files. All
    // clip operations are executor-only, so this check is ordered
    // against the capture task's own close/reopen sections.
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

    bind_slot(id, &m_record_sink);

    // Start mic service if audio is enabled
    if (m_mic != nullptr && m_config.audio_enabled)
    {
        m_mic->start();
    }

    m_log.info(SUBSYSTEM, "Recording started: %s", m_clip_session.file_path());
    return true;
}

bool VideoController::executor_stop_recording()
{
    int id = find_id(&m_record_sink);
    if (id < 0)
    {
        return false;
    }

    // Request clip closure before detaching so the closure pass later
    // in this activation (or the capture-failure path) finalizes the
    // clip; while recording remains subscribed (rotation), the pass
    // reopens instead.
    m_clip_session.request_close();
    detach_consumer(id);

    // Stop mic service
    if (m_mic != nullptr && m_config.audio_enabled)
    {
        m_mic->stop();
    }

    m_log.info(SUBSYSTEM, "Recording stopped");
    return true;
}

bool VideoController::executor_start_streaming()
{
    if (is_registered(&m_stream_sink))
    {
        return true;  // already active: no-op success
    }

    int id = acquire_free_slot();
    if (id < 0)
    {
        m_log.warn(SUBSYSTEM, "Streaming start rejected: consumer registry full");
        return false;
    }

    bind_slot(id, &m_stream_sink);

    // A deferred viewer close from a stop/restart within one frame
    // period is obsolete: streaming mode is active again.
    m_close_viewers_pending.store(false, std::memory_order_release);

    m_log.info(SUBSYSTEM, "Streaming started");
    return true;
}

bool VideoController::executor_stop_streaming()
{
    int id = find_id(&m_stream_sink);
    if (id < 0)
    {
        return false;
    }

    detach_consumer(id);
    m_log.info(SUBSYSTEM, "Streaming stopped");

    // Viewers exist only while streaming mode is active: close them as
    // soon as the sink detaches, even when recording keeps the loop
    // running. With survivors, the close is deferred to this
    // activation's dispatch boundary (process_pending_viewer_close) so
    // it cannot interleave with an in-flight deliver_frame(); a drained
    // registry hands the close to the command-side teardown after the
    // join.
    if (consumer_count() > 0)
    {
        m_close_viewers_pending.store(true, std::memory_order_release);
    }
    return true;
}

int VideoController::executor_register(IFrameConsumer* consumer)
{
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

    bind_slot(id, consumer);
    return id;
}

bool VideoController::executor_unregister(IFrameConsumer* expected, int slot_id)
{
    if (slot_id < 0 || slot_id >= static_cast<int>(MAX_FRAME_CONSUMERS))
    {
        return false;
    }
    // Re-validate exactly: only detach if the slot still binds the
    // consumer the command validated against (a stale snapshot must
    // never detach a rebound slot).
    if (m_slots[slot_id].consumer.load(std::memory_order_acquire) != expected)
    {
        return false;
    }
    detach_consumer(slot_id);
    return true;
}

bool VideoController::executor_take_photo(const char* path)
{
    if (path == nullptr || path[0] == '\0')
    {
        return false;
    }

    // Executor-only, at an activation boundary: request execution
    // precedes this activation's own frame capture (a service pass
    // captures nothing), and the executor token excludes any concurrent
    // activation, so the camera is free here. The SD write runs in
    // executor context — the same discipline as the per-frame clip
    // appends — so photo writes are serialized against recording I/O
    // by construction.
    CameraCaptureResult result = m_camera.capture_frame();
    if (result != CameraCaptureResult::SUCCESS)
    {
        m_log.warn(SUBSYSTEM, "Photo capture failed");
        return false;
    }

    hal::CameraFrame frame = m_camera.get_frame();

    // Ensure the parent directory exists before writing (mirrors
    // AviClipSession::open()).
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
        m_log.info(SUBSYSTEM, "Photo saved: %s (%u bytes)", path,
                   static_cast<unsigned>(frame.len));
    }
    else
    {
        m_log.warn(SUBSYSTEM, "Photo SD write failed: %s", path);
    }
    return ok;
}

// ============================================================
// Path utilities
// ============================================================

void VideoController::set_mic_service(MicService* mic)
{
    m_mic = mic;
}

void VideoController::drain_audio_to_clip()
{
    if (m_mic == nullptr || !m_config.audio_enabled || !m_clip_session.is_audio_enabled())
    {
        return;
    }

    // Drain audio from ring buffer at activation boundaries
    size_t samples = m_mic->get_audio_chunk(m_audio_buffer.data(), m_audio_chunk_size);
    if (samples > 0)
    {
        // Write audio chunk to AVI clip (interleaved with video frames)
        m_clip_session.append_audio_chunk(
            reinterpret_cast<const uint8_t*>(m_audio_buffer.data()),
            samples * sizeof(int16_t));
    }
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
