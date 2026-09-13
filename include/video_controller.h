#ifndef VIDEO_CONTROLLER_H
#define VIDEO_CONTROLLER_H

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <mutex>
#include "avi_clip_session.h"
#include "camera_controller.h"
#include "sd_controller.h"
#include "time_service.h"
#include "logging_service.h"
#include "capture_scheduler.h"
#include "frame_consumer.h"
#include "ini_parser.h"
#include "path_utils.h"
#include "time_formatter.h"

/** Forward declaration for the minimal stream-sink interface. */
class IStreamService;

/**
 * @file video_controller.h
 * @brief Capture pipeline: frame pump plus recording session owner.
 *
 * The capture loop is the sole frame producer. Activities interested
 * in frames — SD recording and live streaming delivery — subscribe by
 * registering an IFrameConsumer; the loop runs while at least one
 * consumer is registered and is torn down (closing any connected
 * stream viewers) when the last one detaches.
 *
 * Lifecycle:
 *   init(config) → start_streaming() and/or start_recording()
 *       → [scheduler frame callback] × N → stop_recording() / stop_streaming()
 *
 * Recording and live preview are independent subscribers sharing one
 * capture loop. Stopping recording while streaming remains subscribed
 * finalizes the clip asynchronously on the next frame callback
 * (start_recording() is rejected until then); stopping the last
 * subscriber tears down the capture pipeline synchronously (bounded)
 * and closes all stream viewers.
 *
 * Clip rotation:
 *   When a clip reaches clip_duration_s or max_file_size_mb,
 *   AviClipSession::close() is called and a new clip is opened
 *   immediately (while recording remains subscribed).
 *
 * Photo capture:
 *   capture_photo(path) posts a TAKE_PHOTO request into the executor
 *   queue; the photo is captured and written to the given SD card
 *   path at an activation boundary (in the capture task, or on a
 *   command task via the serve path under the executor token). Photos
 *   work while recording and/or streaming are active — the executor
 *   serializes the photo against lifecycle changes and frame
 *   activations by construction.
 *
 * File naming:
 *   /YYYYMMDD/YYYYMMDD_HHMMSS_<WxH>_<FPS>fps.avi
 */

/**
 * @class VideoController
 * @brief Owns the capture pump, the consumer registry, and the
 *        AviClipSession (recording/AVI clip owner). The capture task
 *        is the single owner and executor of all pipeline state.
 *
 * Depends on CameraController for frame capture, SdController for
 * SD I/O (synchronous per-frame writes), TimeService for frame pacing,
 * LoggingService for diagnostics, and ICaptureScheduler for
 * platform-agnostic capture scheduling. The clip/AVI session state
 * (writer, paths, counters, close/rotation flag) lives in the
 * AviClipSession member.
 *
 * Executor model: lifecycle commands (start/stop of recording and
 * streaming, consumer registration/unregistration, one-shot photo
 * capture) do not mutate pipeline state directly. They post requests into a small bounded
 * queue; requests are executed only by the holder of the executor
 * token: the capture task claims it for each frame activation and
 * executes queued requests at the activation boundary, and a waiting
 * command task may also claim the token (under m_command_mutex) to
 * serve the queue itself while the capture task is between
 * activations. Pipeline state is therefore serialized by the token,
 * not by task affinity — clip open/close, for example, can run on a
 * command thread via the serve path. A start still binds for the next
 * frame activation regardless of which task executed the request.
 *
 * Command tasks hold m_command_mutex around request posting, scheduler
 * start/stop, and the serve path (the token claim plus
 * process_requests(), mutually exclusive with the post-join teardown
 * section), and wait for completion with the mutex released across the
 * wait loop iterations. The bounded scheduler join runs on the
 * command side, triggered once per executor-reported drain of the
 * registry, with the mutex released across the join: the capture task
 * can never block behind a joiner. A command issued from within
 * on_frame() (executor context) is fire-and-forget: it is queued and
 * consumed at the next activation boundary, never waited on. For every
 * drain the executor also arms a deferred viewer close, consumed
 * exactly once: a waiting command task's post-join teardown performs
 * the close, and when no command task consumes the drain (a
 * fire-and-forget command, or a waiter that timed out) the executor
 * closes any connected viewers itself at the next activation boundary
 * so they never hang in the meantime, while the drained signal stays
 * armed for the next command task of any kind (which then drives the
 * bounded join).
 */
class VideoController
{
    public:
        /** Estimated file overhead per clip (header + index, bytes). */
        static constexpr uint32_t CLIP_OVERHEAD_BYTES = 232;

        /**
         * @brief Constructs a VideoController with injected dependencies.
         *
         * @param cam       Reference to CameraController for frame capture.
         * @param sd        Reference to SdController for SD file I/O.
         * @param time      Reference to TimeService for frame pacing.
         * @param log       Reference to LoggingService for diagnostics.
         * @param scheduler Reference to ICaptureScheduler for frame scheduling.
         */
        VideoController(CameraController& cam,
                        SdController& sd,
                        TimeService& time,
                        LoggingService& log,
                        ICaptureScheduler& scheduler);

        /**
         * @brief Initializes the video controller with the given configuration.
         *
         * Must be called before start_recording(). Stores the config and
         * validated dimensions, and computes frame interval.
         *
         * @param config Video recording configuration from ConfigService.
         * @param width  Validated pixel width from build_camera_config().
         * @param height Validated pixel height from build_camera_config().
         * @return true if initialization succeeded.
         */
        bool init(const ConfigData::VideoConfig& config,
                  uint16_t width, uint16_t height);

        /**
         * @brief Starts a recording session.
         *
         * Opens the first clip and subscribes the recording frame
         * consumer, starting the capture loop if it is not already
         * running (reusing it when streaming is already active).
         * Returns false if a recording is already in progress, if the
         * previous clip has not finished closing, or if the clip cannot
         * be opened.
         *
         * @return true if recording started successfully.
         */
        bool start_recording();

        /**
         * @brief Stops the current recording session.
         *
         * Queues a stop request that the capture task consumes at the
         * next activation: the recording consumer is detached and the
         * clip is finalized in the same activation. The stop is posted
         * whenever the capture loop is running — even when the sink is
         * not bound yet, because a queued-but-unexecuted start may still
         * be pending — so start/stop stay totally ordered through the
         * executor (a stop that finds nothing bound is a no-op there).
         * When other consumers (streaming) remain subscribed, the
         * capture loop keeps running; otherwise the executor reports the
         * drained registry and this function performs the bounded
         * teardown join, closing all connected stream viewers
         * afterwards. No-op when the capture loop is not running.
         */
        void stop_recording();

        /**
         * @brief Captures a single JPEG photo and writes it to SD.
         *
         * Posts a TAKE_PHOTO request into the executor queue; the
         * photo is captured and written at an activation boundary,
         * so it works while recording and/or streaming are active
         * (the executor serializes the photo against lifecycle
         * changes and frame activations). The SD write runs in
         * executor context, preserving the single-capture-task SD
         * writer discipline. From within on_frame() (executor
         * context) the request is fire-and-forget and the result is
         * not knowable synchronously.
         *
         * @param path Full SD card path for the photo file.
         * @return true if the photo was captured and written
         *         successfully (fire-and-forget in executor context).
         */
        bool capture_photo(const char* path);

        /**
         * @brief Builds a unique SD path from a candidate path.
         *
         * If the candidate already exists on the SD card, a serial
         * suffix (_1, _2, ...) is inserted before the extension. Used
         * for paths generated outside the clip lifecycle, e.g. photos.
         *
         * @param base_path Candidate path that may already exist.
         * @param out       Buffer receiving the unique path.
         * @param out_size  Size of the output buffer.
         * @return true if a unique path was written to @p out.
         */
        bool make_unique_path(const char* base_path, char* out, size_t out_size);

        /**
         * @brief Returns whether a recording session is subscribed.
         * @return true if recording is active.
         */
        bool is_recording() const;

        /**
         * @brief Returns whether live preview delivery is subscribed.
         * @return true if streaming is active.
         */
        bool is_streaming() const;

        /**
         * @brief Returns whether the capture loop is running for any
         *        consumer.
         *
         * @return true if at least one frame consumer is subscribed.
         */
        bool is_capture_active() const;

        /**
         * @brief Returns the number of frames recorded in the current clip.
         * @return Frame count for the active clip.
         */
        uint32_t frames_recorded() const;

        /**
         * @brief Returns the accumulated file size of the current clip (bytes).
         * @return File size estimate for the active clip.
         */
        uint32_t file_size() const;

        /**
         * @brief Begins live preview (streaming without SD recording).
         *
         * Queues a start request that the capture task consumes at the
         * next activation, subscribing the streaming frame consumer and
         * starting the capture loop first if it is not already running
         * (e.g. during recording), so frames are delivered to connected
         * stream clients. No-op (returns true) if streaming is already
         * subscribed.
         *
         * @return true if preview is active after the call.
         */
        bool start_streaming();

        /**
         * @brief Stops live preview.
         *
         * Queues a stop request that the capture task consumes at the
         * next activation, unsubscribing the streaming consumer. As with
         * stop_recording(), the stop is posted whenever the capture loop
         * is running (even against a queued-but-unexecuted start) so
         * start/stop stay totally ordered; a stop that finds nothing
         * bound is a no-op in the executor.
         * Connected stream viewers are closed whenever streaming turns
         * off: when other consumers keep the capture loop running, the
         * close runs in the executor at that activation's dispatch
         * boundary; otherwise the executor reports the drained registry
         * and this function performs the bounded teardown join, closing
         * the viewers after the join. No-op when the capture loop is not
         * running.
         */
        void stop_streaming();

        /**
         * @brief Sets the stream-sink service for frame delivery.
         *
         * The sink receives every captured JPEG frame while the
         * streaming consumer is subscribed. May be null to disable
         * streaming.
         *
         * @param stream_service Optional IStreamService implementation.
         */
        void set_stream_service(IStreamService* stream_service);

        /**
         * @brief Maximum number of simultaneously subscribed frame
         *        consumers (recording + streaming + headroom for
         *        additional consumers).
         */
        static constexpr size_t MAX_FRAME_CONSUMERS = 3;

        /**
         * @brief Registers a frame consumer with the capture pipeline.
         *
         * Starts the capture loop if it is not already running. The
         * same consumer pointer may not be registered twice.
         *
         * Recording and streaming subscribe as built-in sinks owned
         * by the controller, bound by the executor when it executes
         * the start requests; this method is the public API for
         * additional external consumers only.
         *
         * @param consumer Consumer to subscribe; must outlive the
         *                 registration.
         * @return Registration id (>= 0), or -1 if the registry is full,
         *         the consumer is null or already registered, or the
         *         request could not be served.
         */
        int register_frame_consumer(IFrameConsumer* consumer);

        /**
         * @brief Unsubscribes a registered frame consumer.
         *
         * When the last consumer unsubscribes, the executor reports the
         * drained registry and the bounded teardown join runs on the
         * calling task, closing all connected stream viewers afterwards.
         *
         * @param id Registration id returned by register_frame_consumer().
         */
        void unregister_frame_consumer(int id);

    private:
        /** Pipeline commands the executor consumes at dispatch boundaries. */
        enum class PipelineRequestType : uint8_t
        {
            START_RECORDING,
            STOP_RECORDING,
            START_STREAMING,
            STOP_STREAMING,
            REGISTER_CONSUMER,
            UNREGISTER_CONSUMER,
            TAKE_PHOTO,
        };

        /**
         * @brief Completion box for one posted pipeline command.
         *
         * Waiter-owned via shared_ptr: the executor writes the result
         * and signals done; a waiter that timed out may already be gone,
         * and the executor's shared copy keeps the box alive.
         */
        struct CommandCompletion
        {
            std::atomic<bool> done{false};
            bool ok{false};
            int id{-1};
        };

        /** Maximum photo path length carried by a request (bytes). */
        static constexpr size_t MAX_PHOTO_PATH = 64;

        /**
         * @brief One queued executor request.
         */
        struct PipelineRequest
        {
            PipelineRequestType type{};
            IFrameConsumer* consumer{nullptr};  ///< REGISTER/UNREGISTER
            int slot_id{-1};                    ///< UNREGISTER target slot
            char photo_path[MAX_PHOTO_PATH]{};  ///< TAKE_PHOTO target path
            std::shared_ptr<CommandCompletion> completion;
            bool result_ok{false};              ///< Filled by the executor
            int result_id{-1};                  ///< Filled by the executor
        };

        /**
         * @brief Frame consumer that formats AVI chunks and writes them
         *        to SD while recording is subscribed.
         */
        class RecordSink : public IFrameConsumer
        {
            public:
                explicit RecordSink(VideoController& vc) : m_vc(vc) {}
                void on_frame(const uint8_t* data, size_t len) override;

            private:
                VideoController& m_vc;
        };

        /**
         * @brief Frame consumer that forwards frames to the stream
         *        service while streaming is subscribed.
         */
        class StreamSink : public IFrameConsumer
        {
            public:
                explicit StreamSink(VideoController& vc) : m_vc(vc) {}
                void on_frame(const uint8_t* data, size_t len) override;

            private:
                VideoController& m_vc;
        };

        /**
         * @brief One registry entry for a subscribed frame consumer.
         *
         * Mutated only by the executor (capture task); the used flag and
         * consumer pointer stay atomic so command tasks can read
         * registry state (activity queries, unregister validation)
         * without locks.
         */
        struct ConsumerSlot
        {
            std::atomic<bool> used{false};
            std::atomic<IFrameConsumer*> consumer{nullptr};
        };

        /**
         * @brief Returns whether the given consumer is registered.
         */
        bool is_registered(const IFrameConsumer* consumer) const;

        /**
         * @brief Reserves a free slot for the executor.
         *
         * Executor-only: slots are mutated exclusively in the capture
         * task, so a plain scan-and-set suffices (the CAS protocol died
         * with the command-side registry mutation).
         *
         * @return Reserved slot id, or -1 if the registry is full.
         */
        int acquire_free_slot();

        /**
         * @brief Releases a reserved slot whose registration failed
         *        before the consumer was bound.
         */
        void release_slot(int id);

        /**
         * @brief Returns the slot id the consumer is registered under,
         *        or -1 if not registered.
         */
        int find_id(const IFrameConsumer* consumer) const;

        /**
         * @brief Returns the number of currently subscribed consumers.
         */
        size_t consumer_count() const;

        /**
         * @brief Binds a consumer into a reserved slot.
         */
        void bind_slot(int id, IFrameConsumer* consumer);

        /**
         * @brief Releases a consumer's slot (flag first, then pointer).
         */
        void detach_consumer(int id);

        /**
         * @brief Ensures the capture loop is running.
         *
         * Command side only; called under m_command_mutex and
         * m_queue_mutex after a request has been queued so the executor's
         * first activation serves it.
         *
         * @return true if the loop is running after the call.
         */
        bool ensure_loop_running();

        /**
         * @brief Posts a pipeline request for the executor.
         *
         * Command side; takes m_command_mutex and delegates to
         * post_request_locked().
         *
         * @param type     Command to execute.
         * @param consumer Consumer for REGISTER/UNREGISTER requests.
         * @param slot_id  Target slot for UNREGISTER requests.
         * @param photo_path Target path for TAKE_PHOTO requests.
         * @return Completion box, or nullptr when the request could not
         *         be queued/served.
         */
        std::shared_ptr<CommandCompletion> post_request(
            PipelineRequestType type, IFrameConsumer* consumer, int slot_id,
            const char* photo_path = nullptr);

        /**
         * @brief Posts a pipeline request with m_command_mutex held.
         *
         * Command side; the caller must hold m_command_mutex (commands
         * that must decide and post atomically against other command
         * tasks use this variant). Fails when the request queue is full
         * or the capture scheduler cannot run (the request could never
         * be consumed). Commands posted while the loop is down are
         * served by the executor's first activation, which starts
         * synchronously with the scheduler.
         *
         * @param type     Command to execute.
         * @param consumer Consumer for REGISTER/UNREGISTER requests.
         * @param slot_id  Target slot for UNREGISTER requests.
         * @param photo_path Target path for TAKE_PHOTO requests.
         * @return Completion box, or nullptr when the request could not
         *         be queued/served.
         */
        std::shared_ptr<CommandCompletion> post_request_locked(
            PipelineRequestType type, IFrameConsumer* consumer, int slot_id,
            const char* photo_path = nullptr);

        /**
         * @brief Waits for a posted request to be executed.
         *
         * Called with m_command_mutex released: the executor never
         * waits on a command task, and no holder blocks the capture
         * task behind this wait. The executor serves the request at its
         * next dispatch boundary; while the capture task is between
         * activations, the waiting task may claim the executor token
         * and serve the queued batch itself (try-acquire, never
         * blocking the capture task).
         *
         * @param completion Box returned by post_request().
         * @param ok         Receives the executor's success result.
         * @param id         Receives the registration id (may be null).
         * @return false on timeout (the request may still execute
         *         later; status queries remain authoritative).
         */
        bool wait_for_completion(
            const std::shared_ptr<CommandCompletion>& completion,
            bool* ok, int* id);

        /**
         * @brief Completes a command: runs the teardown join if the
         *        executor reported a drained registry.
         *
         * Command side; takes m_command_mutex. The bounded scheduler
         * join runs exactly once per drain, with the mutex released.
         * After the join a registry recheck honors a concurrent
         * registration by restarting the loop; otherwise all connected
         * stream viewers are closed so they never outlive frame
         * delivery. Concurrent drains collapse into one join (the
         * drained flag is consumed exactly once).
         */
        void finish_command();

        /**
         * @brief Captures one frame and dispatches it to consumers.
         *
         * Executor entry (called by the ICaptureScheduler callback on
         * each frame trigger). First serves queued pipeline commands —
         * their effects are visible to this activation's capture — then
         * captures a frame, dispatches it to every subscribed consumer,
         * releases the frame buffer, and drains any deferred viewer
         * close and pending clip closure. An activation that only
         * serves commands (no consumers, nothing to finalize) captures
         * nothing.
         */
        void capture_and_dispatch_frame();

        /**
         * @brief Consumes and executes every queued request.
         *
         * Executor-only: claims the queued batch under m_queue_mutex,
         * snapshots it into local storage, and runs each request against
         * the executor-owned pipeline state.
         *
         * @return The number of requests consumed (0 when the queue was
         *         empty); an activation that consumed requests is a pure
         *         service pass and captures no frame.
         */
        int process_requests();

        /**
         * @brief Executes one request (executor-only); results are
         *        stored on the request and signaled by the caller after
         *        the whole claimed batch has run. Sets @p detached when
         *        the request unsubscribed a consumer, and @p photo_served
         *        when the request was a served TAKE_PHOTO (both feed the
         *        batch-level drain decision).
         */
        void execute_request(PipelineRequest& req, bool* detached,
                             bool* photo_served);

        /** @brief Executor-side start_recording. */
        bool executor_start_recording();

        /** @brief Executor-side stop_recording; true if it detached. */
        bool executor_stop_recording();

        /** @brief Executor-side start_streaming. */
        bool executor_start_streaming();

        /** @brief Executor-side stop_streaming; true if it detached. */
        bool executor_stop_streaming();

        /** @brief Executor-side consumer registration. */
        int executor_register(IFrameConsumer* consumer);

        /**
         * @brief Executor-side one-shot photo capture.
         *
         * Runs at an activation boundary (capture task, or a command
         * task via the serve path under the executor token): the
         * camera is free — request execution precedes the
         * activation's own frame capture, and the token excludes any
         * concurrent activation — so the frame is captured, written
         * to SD, and released entirely in executor context.
         *
         * @param path Full SD card path for the photo file.
         * @return true if the photo was captured and written.
         */
        bool executor_take_photo(const char* path);

        /**
         * @brief Executor-side consumer unregistration; true if it detached.
         *
         * Re-validates the slot against the expected consumer pointer so
         * a stale command can never detach a rebound slot. Returns true
         * only when the slot was valid and still bound the expected
         * consumer, so a no-op unregister cannot spuriously signal a
         * drain.
         */
        bool executor_unregister(IFrameConsumer* expected, int slot_id);

        /**
         * @brief Finalizes the current clip if closure was requested.
         *
         * Executor-only: the close/finalize/reopen decision runs in the
         * capture task, mutually exclusive with command-side lifecycle
         * mutations by task affinity (commands can only change the
         * registry at activation boundaries). On explicit stop the clip
         * stays closed; while recording remains subscribed (rotation
         * limit), a fresh clip opens immediately.
         */
        void handle_clip_closure();

        /**
         * @brief Drains a viewer close deferred by stop_streaming().
         *
         * Called from capture-task activation boundaries so the close
         * cannot interleave with an in-flight frame delivery inside
         * this task.
         */
        void process_pending_viewer_close();

        /**
         * @brief Creates a directory on the SD card.
         *
         * @param path Directory path to create.
         */
        void create_directory(const char* path);

        static constexpr const char* SUBSYSTEM = "VIDEO";

        // Dependencies (injected)
        CameraController& m_camera;
        SdController& m_sd;
        TimeService& m_time;
        LoggingService& m_log;
        ICaptureScheduler& m_scheduler;

        /** Optional stream-sink for frame delivery (may be null). */
        IStreamService* m_stream_service{nullptr};

        // Owned components
        // Clip/AVI session: owns the AVI writer, clip paths, per-clip
        // counters, and the close/rotation request flag. All clip
        // operations run in the executor only (task affinity;
        // append_frame is the lock-free pump path).
        AviClipSession m_clip_session;

        // Frame-consumer registry (mutated only by the executor; the
        // atomic flag/pointer pair lets command tasks read registry
        // state without locks)
        ConsumerSlot m_slots[MAX_FRAME_CONSUMERS]{};

        // Built-in consumers (one instance each)
        RecordSink m_record_sink;
        StreamSink m_stream_sink;

        // State (executor-owned unless noted)
        // Viewer close deferred to the current activation's dispatch
        // boundary. Atomic because two writer classes coexist without a
        // shared lock: executor paths arm/clear it from
        // capture_and_dispatch_frame() (process_pending_viewer_close,
        // executor_start_streaming clearing a stale flag) and from
        // process_requests() (batch-level drain arming), while
        // finish_command()
        // reads/clears it under m_command_mutex. Read-only after the
        // loop is gone so no synchronization with the executor is
        // possible; the atomic contract is what makes the disjoint-lock
        // accesses well-defined.
        std::atomic<bool> m_close_viewers_pending{false};
        // Command side only (under m_command_mutex): capture scheduler
        // running.
        std::atomic<bool> m_loop_active{false};
        // Set by the executor when a command detached a consumer and the
        // registry is now empty; consumed exactly once by a command task,
        // which then drives the bounded scheduler join. A drain reported
        // for a fire-and-forget command (no waiting task) stays armed
        // until the next command task runs.
        std::atomic<bool> m_pipeline_drained{false};
        // Command side (under m_command_mutex): a bounded scheduler join
        // is in flight. A second drain arriving during the join window is
        // deferred to the owner's post-join recheck, which observes the
        // final registry state.
        bool m_join_in_progress{false};
        /**
         * @brief Executor token: mutual exclusion for pipeline state
         *        mutation.
         *
         * The capture task holds the token for its whole activation
         * (pump, clip operations, request execution). A waiting command
         * task may claim the token — only while the capture task is
         * provably between activations (try-acquire, never blocking) —
         * to execute the queued request batch itself; on the platform
         * scheduler the capture task usually consumes requests first at
         * its next boundary, so the token handover is the rare path.
         * Commands issued while the caller holds the token (from within
         * on_frame()) are detected via this flag and turned into
         * fire-and-forget requests.
         */
        std::atomic<bool> m_executor_active{false};
        /**
         * @brief Serializes command-side pipeline control.
         *
         * Guarded sections: request posting (the bounded command queue
         * and its ring bookkeeping), scheduler start/stop decisions
         * (m_loop_active), and the photo-capture gate check. The
         * executor (capture task) never acquires this mutex — it reads
         * its own plain state and claims the queue via the dedicated
         * m_queue_mutex, held only around the ring handoff — so no
         * holder can ever block the capture task behind a joiner; the
         * bounded scheduler join runs from command contexts with the
         * mutex released. Commands wait for executor completion with
         * the mutex released, bounded by a timeout.
         */
        mutable std::mutex m_command_mutex;

        /**
         * @brief Serializes the request-ring handoff between posters
         *        and the executor.
         *
         * Held briefly: posters write and publish one slot, the
         * executor claims the queued batch and snapshots it into local
         * storage. Never held across waits, joins, or request
         * execution, so neither side can block the other behind long
         * work.
         */
        std::mutex m_queue_mutex;

        /// Bounded wait for executor completion of a posted command
        /// (budget: one frame interval plus clip SD I/O, with margin).
        static constexpr uint32_t REQUEST_COMPLETION_TIMEOUT_MS = 1000;
        // Executor request queue: a fixed ring of request slots. Posters
        // (serialized by m_command_mutex, ring handoff under
        // m_queue_mutex) write a slot and publish it; the executor
        // claims the whole queued batch with one exchange of m_queued
        // and snapshots it under m_queue_mutex, then advances
        // m_queue_head (executor-only). A full queue fails the command.
        static constexpr size_t MAX_PENDING_REQUESTS = 8;
        PipelineRequest m_requests[MAX_PENDING_REQUESTS]{};
        std::atomic<int> m_queued{0};
        std::atomic<int> m_queue_tail{0};
        int m_queue_head{0};
        hal::millis_t m_frame_interval_ms{100};
        ConfigData::VideoConfig m_config{};
        uint16_t m_width{640};    ///< Validated capture width in pixels
        uint16_t m_height{480};   ///< Validated capture height in pixels
};

#endif // VIDEO_CONTROLLER_H
