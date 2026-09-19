#ifndef AVI_CLIP_SESSION_H
#define AVI_CLIP_SESSION_H

#include "avi_writer.h"
#include "config_service.h"
#include "logging_service.h"
#include "sd_controller.h"
#include "time_service.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

/**
 * @file avi_clip_session.h
 * @brief Owner of the AVI/clip recording session state.
 *
 * Owns the AVI writer, the current clip's file paths, the per-clip
 * counters, and the clip close/rotation request flag. Extracted from
 * VideoController so the clip state lives behind a small interface with
 * an explicit contract instead of being mutated from two task contexts
 * under an unwritten locking discipline.
 *
 * Locking contract: all methods run under the owning controller's
 * executor token — the capture task holds it for each frame activation,
 * and the command-side serve path (which also executes requests) claims
 * it under m_command_mutex. The token, not task affinity, makes clip
 * open/finalize/rotation-reopen mutually exclusive against command-task
 * lifecycle mutations by construction, since commands can only change
 * the registry at executor activation boundaries (the serve path itself
 * runs with t_executor_context false; only the capture task's
 * activation sets it). The close/rotation request flag and the counters stay
 * atomic: the close request is also set by stop paths, and the counters
 * are read cross-task for status queries.
 *
 * AVI write strategy: per-frame SD writes. Each frame is formatted by
 * AviWriter and immediately written to SD. At clip close the index is
 * appended, then the file is overwritten at position 0 with the final
 * patched header (updated sizes).
 *
 * File naming:
 *   /YYYYMMDD/YYYYMMDD_HHMMSS_<WxH>_<FPS>fps.avi
 */

/**
 * @class AviClipSession
 * @brief Opens, appends to, finalizes, and rotates AVI clips.
 */
class AviClipSession
{
    public:
        /**
         * @brief Constructs the session with injected dependencies.
         *
         * @param sd  SdController for clip file I/O.
         * @param time TimeService for clip timestamps.
         * @param log  LoggingService for diagnostics.
         */
        AviClipSession(SdController& sd, TimeService& time, LoggingService& log);

        /**
         * @brief Configures the session.
         *
         * Must be called before open(). Stores the config and validated
         * dimensions used for file naming.
         *
         * @param config Video recording configuration.
         * @param width  Validated pixel width.
         * @param height Validated pixel height.
         * @return true if initialization succeeded.
         */
        bool init(const ConfigData::VideoConfig& config,
                  uint16_t width, uint16_t height);

        /**
         * @brief Opens a new AVI clip file.
         *
         * Builds the file path from the current timestamp, creates the
         * parent directory, uniquifies the path against existing files,
         * writes a placeholder AVI header, and resets the per-clip state.
         * On success the clip awaits finalization (close_pending()).
         *
         * @return true if a clip is open after the call.
         */
        bool open();

        /**
         * @brief Finalizes the current clip.
         *
         * Appends the index, then overwrites the placeholder header at
         * position 0 with the patched header containing final sizes.
         * Resets the per-clip state and clears both the close request
         * and the close-pending latch.
         *
         */
        void close();

        /**
         * @brief Formats one frame as an AVI chunk and writes it to SD.
         *
         * Updates the per-clip counters and requests clip closure when a
         * rotation limit (duration or file size) is reached. Called from
         * the lock-free frame pump path.
         *
         * @param data JPEG frame bytes.
         * @param len  Number of frame bytes.
         */
        void append_frame(const uint8_t* data, size_t len);

        /**
         * @brief Appends an audio chunk to the current AVI clip.
         *
         * Formats one audio chunk as an AVI chunk and writes it to SD.
         * Called from the capture task at frame activation boundaries
         * after draining the PSRAM ring buffer.
         *
         * @param data PCM audio bytes.
         * @param len  Number of audio bytes.
         */
        void append_audio_chunk(const uint8_t* data, size_t len);

        /**
         * @brief Requests clip closure (explicit stop or rotation).
         *
         * Lock-free; safe from any task context.
         */
        void request_close();

        /**
         * @brief Returns whether clip closure has been requested.
         *
         * Lock-free; sampled by the frame pump.
         */
        bool is_close_requested() const;

        /**
         * @brief Consumes the close request (exchange with false).
         *
         * @return true if a close request was pending.
         */
        bool consume_close_request();

        /**
         * @brief Clears an unconsumed close request (fresh clip start).
         *
         */
        void clear_close_request();

        /**
         * @brief Returns whether a clip is currently open.
         */
        bool is_open() const;

        /**
         * @brief Returns whether an opened clip still awaits
         *        finalization.
         */
        bool close_pending() const;

        /**
         * @brief Returns the number of frames recorded in the current
         *        clip.
         */
        uint32_t frames_recorded() const;

        /**
         * @brief Returns the accumulated file size of the current clip
         *        (bytes).
         */
        uint32_t file_size() const;

        /**
         * @brief Returns the number of audio samples recorded in the
         *        current clip.
         */
        uint32_t audio_samples_recorded() const;

        /**
         * @brief Returns whether audio is enabled for the current clip.
         */
        bool is_audio_enabled() const;

        /**
         * @brief Returns the generated file path of the current clip.
         */
        const char* file_path() const;

        /**
         * @brief Returns the file path of the currently open clip
         *        (persistent across close/open within a rotation).
         */
        const char* clip_path() const;

    private:
        /**
         * @brief Builds a unique SD path from a candidate path.
         *
         * Inserts a serial suffix (_1, _2, ...) before the extension
         * while the candidate exists on the SD card.
         */
        bool make_unique_path(const char* base_path, char* out,
                              size_t out_size);

        /**
         * @brief Writes a single buffer to SD synchronously (append to
         *        the current clip file).
         */
        bool write_to_sd(const uint8_t* data, size_t len);

        /**
         * @brief Writes data at a specific file offset via SD.
         */
        bool write_at_offset(const char* path, uint32_t offset,
                             const uint8_t* data, size_t len);

        static constexpr const char* SUBSYSTEM = "CLIP";

        SdController& m_sd;
        TimeService& m_time;
        LoggingService& m_log;

        AviWriter m_avi_writer;
        ConfigData::VideoConfig m_config{};
        uint16_t m_width{640};    ///< Validated capture width in pixels
        uint16_t m_height{480};   ///< Validated capture height in pixels

        // Close/rotation request flag: written lock-free by the frame
        // pump (rotation limits) and stop paths, consumed by the closure
        // handler in the owning task's executor context.
        std::atomic<bool> m_close_requested{false};
        // True while an unfinalized clip is open; only touched in the
        // owning task's executor context (set by open(), cleared by
        // close()).
        bool m_close_pending{false};
        // Per-clip counters: written by the frame pump, read for status
        // queries from any task context.
        std::atomic<uint32_t> m_frames_recorded{0};
        std::atomic<uint32_t> m_file_size{0};

        /** Generated file path for the current clip. */
        char m_file_path[64]{};

        /** File path of the currently open clip (persistent across close/open). */
        char m_clip_path[64]{};

        /** Per-clip audio sample counter. */
        std::atomic<uint32_t> m_audio_samples_recorded{0};
};

#endif // AVI_CLIP_SESSION_H
