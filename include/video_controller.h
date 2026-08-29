#ifndef VIDEO_CONTROLLER_H
#define VIDEO_CONTROLLER_H

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <mutex>
#include "camera_controller.h"
#include "sd_controller.h"
#include "avi_writer.h"
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
 * AVI Write Strategy: Per-Frame SD Writes
 * Each captured frame is formatted by AviWriter and immediately
 * written to SD. At clip close, the index is appended, then the
 * file is seeked to position 0 to overwrite the placeholder AVI
 * header with the final patched header (updated sizes).
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
 *   close_clip() is called and a new clip is opened immediately
 *   (while recording remains subscribed).
 *
 * Photo capture:
 *   capture_photo(path) captures a single JPEG frame and writes
 *   it directly to the given SD card path; it is rejected whenever
 *   the capture loop is running for any consumer.
 *
 * File naming:
 *   /YYYYMMDD/YYYYMMDD_HHMMSS_<WxH>_<FPS>fps.avi
 */

/**
 * @class VideoController
 * @brief Owns the capture pump, the consumer registry, and the
 *        recording (AVI/clip) session.
 *
 * Depends on CameraController for frame capture, SdController for
 * SD I/O (synchronous per-frame writes), AviWriter for AVI
 * formatting, TimeService for frame pacing, LoggingService for
 * diagnostics, and ICaptureScheduler for platform-agnostic capture
 * scheduling.
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
         * Unsubscribes the recording consumer immediately so status
         * queries observe the stop right away. When other consumers
         * (streaming) remain subscribed, the capture loop keeps running
         * and the clip is finalized by the next frame callback;
         * otherwise the capture pipeline is torn down synchronously
         * (bounded) and any connected stream viewers are closed. Safe
         * to call in any state — no-op if not recording.
         */
        void stop_recording();

        /**
         * @brief Captures a single JPEG photo and writes it to SD.
         *
         * Captures one frame from the camera, writes the JPEG data
         * to the given path, and releases the frame buffer. Will
         * fail while the capture loop is running for any consumer.
         *
         * @param path Full SD card path for the photo file.
         * @return true if the photo was captured and written successfully.
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
         * While this returns true, single-frame photo capture is
         * rejected because the camera/frame buffers are in use.
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
         * Subscribes the streaming frame consumer, starting the capture
         * loop if it is not already running (e.g. during recording), so
         * frames are delivered to connected stream clients. No-op
         * (returns true) if streaming is already subscribed.
         *
         * @return true if preview is active after the call.
         */
        bool start_streaming();

        /**
         * @brief Stops live preview.
         *
         * Unsubscribes the streaming consumer. Connected stream viewers
         * are closed whenever streaming turns off: when other consumers
         * keep the capture loop running, the close is deferred to the
         * next dispatch boundary so it cannot race an in-flight frame
         * delivery inside the capture task; otherwise the capture
         * pipeline is torn down (bounded) and viewers are closed after
         * the join. No-op if streaming is not active.
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
         * by the controller and claimed internally by
         * start_recording() / start_streaming(); this method is the
         * public API for additional external consumers only.
         *
         * @param consumer Consumer to subscribe; must outlive the
         *                 registration.
         * @return Registration id (>= 0), or -1 if the registry is full
         *         or the consumer is null or already registered.
         */
        int register_frame_consumer(IFrameConsumer* consumer);

        /**
         * @brief Unsubscribes a registered frame consumer.
         *
         * When the last consumer unsubscribes, the capture pipeline is
         * torn down (bounded) and all connected stream viewers are
         * closed.
         *
         * @param id Registration id returned by register_frame_consumer().
         */
        void unregister_frame_consumer(int id);

    private:
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
         */
        struct ConsumerSlot
        {
            /// Slot flag: compare-and-swap reserved by registrars before
            /// the consumer pointer is bound; polled by the capture task
            /// (a reserved-but-unbound slot reads as a null consumer and
            /// is skipped by dispatch).
            std::atomic<bool> used{false};
            IFrameConsumer* consumer{nullptr};
        };

        /**
         * @brief Returns whether the given consumer is registered.
         */
        bool is_registered(const IFrameConsumer* consumer) const;

        /**
         * @brief Atomically reserves a free slot.
         *
         * Claims the slot's used flag with a compare-and-swap so two
         * concurrent registrars can never reserve the same slot.
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
         * @brief Binds a consumer into a free slot.
         */
        void claim_slot(int id, IFrameConsumer* consumer);

        /**
         * @brief Releases a consumer's slot (flag first, then pointer).
         */
        void detach_consumer(int id);

        /**
         * @brief Tears down the pipeline if no consumer remains.
         *
         * @param lock Caller-held lifecycle mutex lock; released around
         *             the bounded scheduler join inside.
         * @return true if a teardown ran to completion; false when the
         *         registry is not idle or another teardown already owns
         *         the scheduler join (single-flight).
         */
        bool maybe_teardown_if_idle(std::unique_lock<std::mutex>& lock);

        /**
         * @brief Ensures the capture loop is running.
         *
         * @return true if the loop is running after the call.
         */
        bool ensure_loop_running();

        /**
         * @brief Tears down the capture pipeline completely.
         *
         * Releases the lifecycle mutex, stops the capture scheduler
         * (bounded join; its final callback closes any pending clip),
         * then re-acquires the mutex and rechecks the registry: if a
         * concurrent start registered consumers while the mutex was
         * released, the teardown aborts, restarts the loop for them,
         * and defers to a later stop. Otherwise it marks the loop
         * inactive and closes all connected stream viewers. Called when
         * the last consumer unsubscribes, so viewers never outlive
         * frame delivery.
         *
         * Teardown entry is single-flight: the platform scheduler's
         * stop() is not reentrant, so a second caller arriving while
         * another teardown owns the join window skips its own stop and
         * reports false; the owner's post-join recheck observes the
         * final registry state and completes or defers correctly.
         *
         * @param lock Caller-held lifecycle mutex lock.
         * @return true if the teardown completed; false if it aborted,
         *         or if another teardown already owns the join.
         */
        bool teardown_capture_pipeline(std::unique_lock<std::mutex>& lock);

        /**
         * @brief Captures one frame and dispatches it to consumers.
         *
         * Called by the ICaptureScheduler callback on each frame trigger.
         * Captures a frame, dispatches it to every subscribed consumer,
         * releases the frame buffer, then drains any deferred viewer
         * close and pending clip closure. Also serves as the
         * finalization pass for a clip whose recording consumer detached
         * before closure.
         */
        void capture_and_dispatch_frame();

        /**
         * @brief Formats one frame as an AVI chunk and writes it to SD.
         *
         * Updates clip counters and requests clip closure via
         * m_stop_clip when a rotation limit is reached.
         */
        void write_avi_frame(const uint8_t* data, size_t len);

        /**
         * @brief Finalizes the current clip if closure was requested.
         *
         * On explicit stop the clip stays closed; while recording
         * remains subscribed (rotation limit), a fresh clip opens
         * immediately.
         */
        void handle_clip_closure();

        /**
         * @brief Opens a new AVI clip file.
         *
         * Builds the file path from the current timestamp, creates the
         * parent directory, writes a placeholder AVI header, and resets
         * AviWriter state.
         */
        void open_clip();

        /**
         * @brief Closes the current clip if one is open and writes the
         *        final AVI to SD.
         *
         * Appends the index, then seeks to position 0 to overwrite the
         * placeholder header with the patched header containing final
         * sizes. Clears the clip-close-pending guard.
         */
        void close_clip();

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

        /**
         * @brief Writes a single buffer to SD synchronously.
         *
         * Convenience wrapper around m_sd.write_sync().
         *
         * @param data Pointer to data.
         * @param len  Number of bytes.
         * @return true if write succeeded.
         */
        bool write_to_sd(const uint8_t* data, size_t len);

        /**
         * @brief Writes data at a specific file offset via SD.
         *
         * Convenience wrapper around m_sd.write_at_sync().
         *
         * @param path   File path.
         * @param offset Byte offset.
         * @param data   Pointer to data.
         * @param len    Number of bytes.
         * @return true if seek + write succeeded.
         */
        bool write_at_offset(const char* path, uint32_t offset,
                             const uint8_t* data, size_t len);

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
        AviWriter m_avi_writer;

        // Frame-consumer registry (command-task contexts mutate via
        // compare-and-swap slot reservations; the capture task reads
        // slots between dispatches)
        ConsumerSlot m_slots[MAX_FRAME_CONSUMERS]{};

        // Built-in consumers (one instance each)
        RecordSink m_record_sink;
        StreamSink m_stream_sink;

        // State
        std::atomic<bool> m_stop_clip{false};
        std::atomic<bool> m_close_viewers_pending{false}; ///< Viewer close deferred to the next dispatch boundary
        // Per-activity claims: exchanged at entry of start_recording() /
        // start_streaming() so concurrent starts arriving from different
        // command tasks are mutually exclusive; held while the activity
        // is subscribed and released when it stops.
        std::atomic<bool> m_record_claim{false};
        std::atomic<bool> m_stream_claim{false};
        // True while an unfinalized clip is open; owned exclusively by
        // the capture task (set by open_clip(), cleared by close_clip()).
        std::atomic<bool> m_clip_close_pending{false};
        /**
         * @brief Serializes lifecycle transitions between command
         *        transports.
         *
         * Public mutators (start/stop of recording and streaming,
         * consumer registration/unregistration, and the photo-capture
         * gate check) hold this mutex across their mutation sections so
         * transitions arriving concurrently from the web-transport and
         * serial-transport tasks can never interleave: a start cannot
         * bind into a loop that is being torn down, and a deferred
         * viewer-close decision cannot be reordered behind a concurrent
         * streaming restart. The frame pump path itself stays lock-free
         * over the registry slots; consumers invoking start_recording()
         * from on_frame() acquire it only for a short critical section:
         * stop paths release this mutex around the bounded scheduler
         * join inside teardown_capture_pipeline(), so the capture task
         * can never block behind a joiner (no holder ever blocks
         * indefinitely on the capture task). After the join, a post-join
         * registry recheck either honors a concurrent registration or
         * completes the teardown.
         */
        mutable std::mutex m_lifecycle_mutex;
        std::atomic<bool> m_loop_active{false};        ///< Capture scheduler running
        // Exchange-based single-flight claim guarding the scheduler
        // join inside teardown_capture_pipeline(): the platform
        // scheduler's stop() is not reentrant, so only one teardown at
        // a time may drive it; losers skip their own stop and defer to
        // the owner's post-join recheck. Command-task contexts only;
        // always cleared on every teardown exit path.
        std::atomic<bool> m_teardown_in_progress{false};
        hal::millis_t m_frame_interval_ms{100};
        std::atomic<uint32_t> m_frames_recorded{0};
        std::atomic<uint32_t> m_file_size{0};
        ConfigData::VideoConfig m_config{};
        uint16_t m_width{640};    ///< Validated capture width in pixels
        uint16_t m_height{480};   ///< Validated capture height in pixels

        /**
         * @brief Generated file path for the current clip.
         */
        char m_file_path[64]{};

        /**
         * @brief File path for the currently open clip (persistent across close/open).
         */
        char m_clip_path[64]{};
};

#endif // VIDEO_CONTROLLER_H
