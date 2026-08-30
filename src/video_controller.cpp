#include "video_controller.h"
#include "stream_service.h"
#include <chrono>
#include <cstring>
#include <cstdio>
#include <thread>

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
    if (!is_registered(&m_record_sink))
    {
        return;
    }

    std::shared_ptr<CommandCompletion> completion = post_request(
        PipelineRequestType::STOP_RECORDING, nullptr, -1);
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

    if (!m_executor_active.load(std::memory_order_relaxed))
    {
        bool ok = false;
        wait_for_completion(completion, &ok, nullptr);
        finish_command();
    }
}

// ============================================================
// Photo capture
// ============================================================

bool VideoController::capture_photo(const char* path)
{
    {
        // Serialize the gate check with lifecycle commands so it cannot
        // pass while a command is mid-post; the SD I/O below runs
        // without the mutex. (The full photo capability — pump-served
        // capture while activities run — is deferred; this keeps the
        // idle-only capture semantics.)
        std::lock_guard<std::mutex> lock(m_command_mutex);
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
    if (!is_registered(&m_stream_sink))
    {
        return;
    }

    std::shared_ptr<CommandCompletion> completion = post_request(
        PipelineRequestType::STOP_STREAMING, nullptr, -1);
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

    if (consumer_count() > 0)
    {
        // A concurrent command registered consumers while the mutex was
        // released (its request was consumed by the join's final
        // callback): honor it by restoring the loop and deferring this
        // teardown to a later drain.
        if (!ensure_loop_running())
        {
            m_log.warn(SUBSYSTEM, "Teardown aborted: loop restart failed");
        }
        return;
    }

    // Close all connected viewers: with the loop gone they would
    // otherwise hang forever on a silent multipart response.
    if (m_stream_service != nullptr)
    {
        m_stream_service->stop_all_streams();
    }
}

// ============================================================
// Clip lifecycle
// ============================================================

void VideoController::process_pending_viewer_close()
{
    // Executor-only flag: the close runs at the dispatch boundary so it
    // cannot interleave with an in-flight deliver_frame() in this task.
    if (m_close_viewers_pending && m_stream_service != nullptr)
    {
        m_close_viewers_pending = false;
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
    // subscribed or a stopped clip still awaits its final closure.
    if (consumer_count() == 0 && !closing)
    {
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
    PipelineRequestType type, IFrameConsumer* consumer, int slot_id)
{
    std::lock_guard<std::mutex> lock(m_command_mutex);

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

        bool expected = false;
        if (m_executor_active.compare_exchange_strong(expected, true,
                                                       std::memory_order_acq_rel))
        {
            process_requests();
            m_executor_active.store(false, std::memory_order_release);
        }
        else
        {
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
    // Claim the whole queued batch atomically. Posters run under
    // m_command_mutex and publish slot content before bumping the count,
    // so the acquire side of the exchange orders all published requests
    // before consumption. m_queue_head is executor-only.
    int n = m_queued.exchange(0, std::memory_order_acq_rel);
    if (n <= 0)
    {
        return 0;
    }

    // Snapshot the claimed batch's completion boxes up front: the
    // executor then works on local copies, and a returning command task
    // can never overlap the executor while the rest of its batch is
    // still running.
    std::shared_ptr<CommandCompletion> completions[MAX_PENDING_REQUESTS];
    for (int i = 0; i < n; i++)
    {
        const int slot = (m_queue_head + i) % static_cast<int>(MAX_PENDING_REQUESTS);
        completions[i] = m_requests[slot].completion;
    }

    bool detached = false;
    for (int i = 0; i < n; i++)
    {
        PipelineRequest& req = m_requests[m_queue_head];
        m_queue_head = (m_queue_head + 1) % static_cast<int>(MAX_PENDING_REQUESTS);
        execute_request(req, &detached);
    }

    // Signal completions only after the whole batch executed.
    for (int i = 0; i < n; i++)
    {
        const int slot =
            (m_queue_head - n + i + static_cast<int>(MAX_PENDING_REQUESTS)) %
            static_cast<int>(MAX_PENDING_REQUESTS);
        if (completions[i])
        {
            completions[i]->ok = m_requests[slot].result_ok;
            completions[i]->id = m_requests[slot].result_id;
            completions[i]->done.store(true, std::memory_order_release);
        }
    }
    return n;
}

void VideoController::execute_request(PipelineRequest& req, bool* detached)
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
            executor_unregister(req.consumer, req.slot_id);
            *detached = true;
            ok = true;
            break;
    }

    req.result_ok = ok;
    req.result_id = id;

    // The executor alone detects the transition to a drained registry
    // (a no-op stop on an already-empty registry does not re-signal);
    // a command task consumes this signal exactly once and drives the
    // bounded join. A viewer close deferred by a stop command is
    // subsumed by the teardown's synchronous close after the join.
    if (*detached && consumer_count() == 0)
    {
        m_close_viewers_pending = false;
        m_pipeline_drained.store(true, std::memory_order_release);
    }
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
    m_close_viewers_pending = false;

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
        m_close_viewers_pending = true;
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

void VideoController::executor_unregister(IFrameConsumer* expected, int slot_id)
{
    if (slot_id < 0 || slot_id >= static_cast<int>(MAX_FRAME_CONSUMERS))
    {
        return;
    }
    // Re-validate exactly: only detach if the slot still binds the
    // consumer the command validated against (a stale snapshot must
    // never detach a rebound slot).
    if (m_slots[slot_id].consumer.load(std::memory_order_acquire) != expected)
    {
        return;
    }
    detach_consumer(slot_id);
}

// ============================================================
// Path utilities
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
