#ifndef VIDEO_CONTROLLER_H
#define VIDEO_CONTROLLER_H

#include <atomic>
#include <cstdint>
#include <cstddef>
#include "camera_controller.h"
#include "sd_controller.h"
#include "avi_writer.h"
#include "time_service.h"
#include "logging_service.h"
#include "capture_scheduler.h"
#include "ini_parser.h"
#include "path_utils.h"
#include "time_formatter.h"

/** Forward declaration for the minimal stream-sink interface. */
class IStreamService;

/**
 * @file video_controller.h
 * @brief Video recording orchestrator for MJPEG video capture.
 *
 * Coordinates camera frame capture, AVI container formatting, and
 * per-frame SD card writing into a recording session. Delegates all
 * capture scheduling to an injected ICaptureScheduler, keeping this
 * class free of platform-specific code.
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
 * Recording and live preview are independent activities sharing one
 * capture loop, which runs while either is active. Tearing down the
 * loop (when the last activity ends) also closes all stream viewers.
 *
 * Clip rotation:
 *   When a clip reaches clip_duration_s or max_file_size_mb,
 *   close_clip() is called and a new clip is opened immediately
 *   (if recording was not explicitly stopped).
 *
 * Photo capture:
 *   capture_photo(path) captures a single JPEG frame and writes
 *   it directly to the given SD card path.
 *
 * File naming:
 *   /YYYYMMDD/YYYYMMDD_HHMMSS_<WxH>_<FPS>fps.avi
 */

/**
 * @brief Recording states reported by state().
 *
 * These are derived views over two independent activity flags
 * (recording and streaming). RECORDING means frames are being
 * captured and written to SD regardless of whether viewers are
 * connected; use is_streaming() to determine live-preview delivery.
 */
enum class RecordingState : uint8_t
{
    IDLE,       ///< Neither recording nor streaming; capture loop off
    STREAMING,  ///< Live preview only: capture loop on, frames delivered to streams
    RECORDING   ///< Recording to SD (with or without live preview delivery)
};

/**
 * @class VideoController
 * @brief Orchestrates MJPEG video recording to SD card.
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
         * Opens the first clip, ensures the capture loop is running
         * (reusing it when streaming is already active), and marks
         * recording active. Returns false if a recording is already in
         * progress, if the previous clip has not finished closing, or
         * if the clip cannot be opened.
         *
         * @return true if recording started successfully.
         */
        bool start_recording();

        /**
         * @brief Stops the current recording session.
         *
         * Clears the recording state immediately so status queries
         * observe the stop right away. When streaming remains active,
         * the capture loop keeps running and the clip is finalized by
         * the next frame callback; otherwise the capture pipeline is
         * torn down synchronously (bounded) and any connected stream
         * viewers are closed. Safe to call in any state — no-op if not
         * recording.
         */
        void stop_recording();

        /**
         * @brief Captures a single JPEG photo and writes it to SD.
         *
         * Captures one frame from the camera, writes the JPEG data
         * to the given path, and releases the frame buffer. Will
         * fail if video recording is in progress.
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
         * @brief Returns whether the controller is actively recording.
         * @return true if recording is active.
         */
        bool is_recording() const;

        /**
         * @brief Returns whether live preview delivery is active.
         * @return true if streaming mode is active.
         */
        bool is_streaming() const;

        /**
         * @brief Returns the current recording state.
         *
         * Derived view over the independent recording and streaming
         * activities: RECORDING takes precedence over STREAMING, and
         * either being active implies the capture loop is running.
         *
         * @return RecordingState enum value.
         */
        RecordingState state() const;

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
         * Marks streaming active and starts the capture loop if it is
         * not already running (e.g. during recording), so frames are
         * delivered to connected stream clients. No-op (returns true)
         * if streaming is already active.
         *
         * @return true if preview is active after the call.
         */
        bool start_streaming();

        /**
         * @brief Stops live preview.
         *
         * Clears streaming mode. During recording the capture loop
         * keeps serving the recording; otherwise the capture pipeline
         * is torn down and all connected stream viewers are closed.
         * No-op if streaming is not active.
         */
        void stop_streaming();

        /**
         * @brief Sets the stream-sink service for frame delivery.
         *
         * The sink receives every captured JPEG frame (both preview and
         * recording frames). May be null to disable streaming.
         *
         * @param stream_service Optional IStreamService implementation.
         */
        void set_stream_service(IStreamService* stream_service);

    private:
        /**
         * @brief Returns whether the capture loop is currently running.
         *
         * The loop runs while either activity (recording or streaming)
         * is active.
         */
        bool loop_running() const;

        /**
         * @brief Tears down the capture pipeline completely.
         *
         * Stops the capture scheduler (bounded join; its final callback
         * closes any open clip) and then closes all connected stream
         * viewers. Called whenever the last active activity ends, so
         * viewers never outlive frame delivery.
         */
        void teardown_capture_pipeline();

        /**
         * @brief Captures a single frame and writes it to SD.
         *
         * Called by the ICaptureScheduler callback on each frame trigger.
         * Calls CameraController::capture_frame(), formats the AVI chunk
         * via AviWriter::write_frame(), writes the chunk to SD, updates
         * clip byte count, and releases the frame. Also checks clip
         * rotation conditions.
         */
        void capture_and_write_frame();

        /**
         * @brief Opens a new AVI clip file.
         *
         * Builds the file path from the current timestamp, creates the
         * parent directory, writes a placeholder AVI header, and resets
         * AviWriter state.
         */
        void open_clip();

        /**
         * @brief Closes the current clip and writes the final AVI to SD.
         *
         * Appends the index, then seeks to position 0 to overwrite the
         * placeholder header with the patched header containing final
         * sizes.
         */
        void close_clip();

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

        // State
        std::atomic<bool> m_recording{false};      ///< Recording activity active
        std::atomic<bool> m_streaming{false};      ///< Streaming (preview) activity active
        std::atomic<bool> m_stop_clip{false};
        std::atomic<bool> m_clip_close_pending{false}; ///< Set until the capture task finalizes a stopped clip
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
