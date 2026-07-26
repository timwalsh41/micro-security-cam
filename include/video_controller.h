#ifndef VIDEO_CONTROLLER_H
#define VIDEO_CONTROLLER_H

#include <cstdint>
#include <cstddef>
#include <vector>
#include "camera_controller.h"
#include "peripheral_manager.h"
#include "avi_writer.h"
#include "sd_write_buffer.h"
#include "time_service.h"
#include "logging_service.h"
#include "ini_parser.h"

/**
 * @file video_controller.h
 * @brief Video recording orchestrator for MJPEG video capture.
 *
 * Coordinates camera frame capture, AVI container formatting, and
 * buffered SD card writing into a recording session. Operates as a
 * cooperative-polling service driven by tick() from the main loop.
 *
 * AVI Write Strategy: Buffer-All-At-Close
 * The complete AVI (header + all frame chunks + index) is accumulated
 * in RAM during recording, then written to SD in a single flush at
 * clip end. This produces correct AVI files with the header at byte
 * offset 0, at the cost of higher peak RAM usage.
 *
 * Lifecycle:
 *   init(config) → start_recording() → tick() × N → stop_recording()
 *
 * Clip rotation:
 *   When a clip reaches clip_duration_s, max_file_size_mb, or
 *   max_frames(), close_clip() is called and a new clip is opened
 *   immediately (if recording was not explicitly stopped).
 *
 * File naming:
 *   /YYYYMMDD/YYYYMMDD_HHMMSS_<WxH>_<FPS>fps.avi
 */

/**
 * @brief Recording state machine states.
 */
enum class RecordingState : uint8_t
{
    IDLE,       ///< Not recording
    RECORDING,  ///< Actively capturing and writing frames
    STOPPING    ///< Closing current clip (flushing to SD)
};

/**
 * @class VideoController
 * @brief Orchestrates MJPEG video recording to SD card.
 *
 * Depends on CameraController for frame capture, PeripheralManager
 * for SD I/O (via SdWriteBuffer), AviWriter for AVI formatting,
 * TimeService for frame pacing, and LoggingService for diagnostics.
 */
class VideoController
{
    public:
        /** RAM budget for frame accumulation buffer (bytes). */
        static constexpr size_t CLIP_BUFFER_RAM_SIZE = 4 * 1024 * 1024;

        /**
         * @brief Constructs a VideoController with injected dependencies.
         *
         * @param cam        Reference to CameraController for frame capture.
         * @param peripheral Reference to PeripheralManager for SD file I/O.
         * @param time       Reference to TimeService for frame pacing.
         * @param log        Reference to LoggingService for diagnostics.
         */
        VideoController(CameraController& cam,
                        PeripheralManager& peripheral,
                        TimeService& time,
                        LoggingService& log);

        /**
         * @brief Initializes the video controller with the given configuration.
         *
         * Must be called before start_recording(). Stores the config and
         * validated dimensions, logs video settings at INFO level.
         *
         * @param config Video recording configuration from ConfigService.
         * @param width  Validated pixel width from build_camera_config().
         * @param height Validated pixel height from build_camera_config().
         * @return true if initialization succeeded.
         */
        bool init(const ConfigData::VideoConfig& config,
                  uint16_t width, uint16_t height);

        /**
         * @brief Advances the recording state machine.
         *
         * When in RECORDING state, checks frame interval and captures
         * frames at the configured FPS. Also checks clip termination
         * conditions (duration, file size, max frames). When a limit
         * is reached, transitions to STOPPING and closes the clip.
         *
         * When in STOPPING state, completes the clip close and returns
         * to IDLE.
         *
         * Call this periodically from the main loop.
         */
        void tick();

        /**
         * @brief Starts a recording session.
         *
         * Transitions from IDLE to RECORDING and opens the first clip.
         * Returns false if not in IDLE state.
         *
         * @return true if recording started successfully.
         */
        bool start_recording();

        /**
         * @brief Stops the current recording session.
         *
         * Flags the current clip for closure. The actual stop occurs
         * on the next tick() call when close_clip() executes.
         * Safe to call in any state — no-op if not recording.
         */
        void stop_recording();

        /**
         * @brief Returns whether the controller is actively recording.
         * @return true if state is RECORDING.
         */
        bool is_recording() const;

        /**
         * @brief Returns the current recording state.
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
         * @brief Estimated average bytes per frame for the current config.
         *
         * Computed from the stored dimensions (set by init()) and JPEG
         * quality settings. Used by max_frames() as a RAM-safety backstop.
         *
         * @return Estimated bytes per frame.
         */
        uint32_t avg_bytes_per_frame() const;

        /**
         * @brief Maximum frames per clip for the current config.
         *
         * Derived from CLIP_BUFFER_RAM_SIZE / avg_bytes_per_frame().
         * Recomputes on every call, so it stays correct if config changes.
         *
         * @return Maximum number of frames before the clip is force-closed.
         */
        uint32_t max_frames() const;

    private:
        /**
         * @brief Holds a copy of an AVI frame chunk for replay at clip close.
         */
        struct FrameChunk
        {
            std::vector<uint8_t> data;  ///< Complete AVI chunk bytes
        };

        /**
         * @brief Opens a new AVI clip file.
         *
         * Builds the file path from the current timestamp, generates
         * the AVI header, and initializes the SD write buffer.
         */
        void open_clip();

        /**
         * @brief Closes the current clip and flushes to SD.
         *
         * Patches the AVI header, replays all frame chunks, writes
         * the index, and flushes everything to the SD card in one shot.
         */
        void close_clip();

        /**
         * @brief Captures a single frame and buffers it for the current clip.
         *
         * Calls CameraController::capture_frame(), formats the JPEG
         * via AviWriter::write_frame(), copies the chunk to the
         * accumulator, and releases the frame.
         */
        void capture_and_write_frame();

        /**
         * @brief Creates a directory on the SD card.
         *
         * Uses PeripheralManager::create_dir() to create the parent
         * directory for a clip file. Safe to call if the directory
         * already exists.
         *
         * @param path Directory path to create.
         */
        void create_directory(const char* path);

        static constexpr const char* SUBSYSTEM = "VIDEO";

        // Dependencies (injected)
        CameraController& m_camera;
        PeripheralManager& m_peripheral;
        TimeService& m_time;
        LoggingService& m_log;

        // Owned components
        AviWriter m_avi_writer;
        SdWriteBuffer m_sd_buffer;

        /** Accumulated frame chunks for replay at clip close. */
        std::vector<FrameChunk> m_frame_chunks;

        // State
        RecordingState m_state{RecordingState::IDLE};
        bool m_stop_clip{false};
        hal::millis_t m_last_frame_time{0};
        hal::millis_t m_frame_interval_ms{100};
        hal::millis_t m_clip_start_time{0};
        uint32_t m_frames_recorded{0};
        uint32_t m_file_size{0};
        ConfigData::VideoConfig m_config{};
        uint16_t m_width{640};    ///< Validated capture width in pixels
        uint16_t m_height{480};   ///< Validated capture height in pixels

        // Generated file path for the current clip
        char m_file_path[64]{};
};

#endif // VIDEO_CONTROLLER_H
