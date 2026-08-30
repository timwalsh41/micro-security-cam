#ifndef STREAM_FRAME_CHANNEL_H
#define STREAM_FRAME_CHANNEL_H

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

/**
 * @file stream_frame_channel.h
 * @brief Per-viewer byte channel feeding a live multipart stream.
 *
 * A channel is the single frame handoff point between the frame
 * producer (which pushes complete, pre-formatted multipart parts) and
 * the transport response machinery (which pulls bytes as the client
 * acknowledges data). Pushes never block: the producer always makes
 * progress at frame cadence. When a consumer falls behind, the oldest
 * unconsumed part is kept and newer parts are dropped — the correct
 * semantics for live preview, where only the freshest frame matters.
 *
 * All methods are thread-safe (one producer task, one consumer task).
 * Flow control is entirely on the pull side: the consumer decides how
 * many bytes to take per call, so frame data can never be truncated by
 * a transport send-buffer limit.
 */

/**
 * @class StreamFrameChannel
 * @brief Bounded single-part mailbox for streaming multipart bytes.
 */
class StreamFrameChannel
{
    public:
        /**
         * @brief Pushes one complete, pre-formatted multipart part.
         *
         * The part is copied. If the previous part has not been fully
         * consumed yet, or the channel is closed, the new part is
         * dropped and false is returned (the producer must keep
         * going regardless).
         *
         * @param data Pointer to the part bytes.
         * @param len  Number of part bytes.
         * @return true if the part was buffered for the consumer.
         */
        bool push_part(const uint8_t* data, size_t len);

        /**
         * @brief Pulls up to @p max_len bytes of the pending part.
         *
         * @param out     Output buffer.
         * @param max_len Output buffer capacity.
         * @return Bytes written to @p out (0 when no part is pending).
         */
        size_t pull(uint8_t* out, size_t max_len);

        /**
         * @brief Returns whether a part (or part remainder) awaits the
         *        consumer.
         */
        bool has_pending_part() const;

        /**
         * @brief Marks the channel closed.
         *
         * A pending part still drains to the consumer; once empty, the
         * stream is finished (the consumer observes an empty, closed
         * channel). Further pushes are rejected.
         */
        void close();

        /**
         * @brief Returns whether close() has been called.
         */
        bool is_closed() const;

    private:
        mutable std::mutex m_mutex;
        std::vector<uint8_t> m_part;  ///< Pending part (reused capacity)
        size_t m_offset{0};           ///< Consumer position in m_part
        bool m_closed{false};
};

#endif // STREAM_FRAME_CHANNEL_H
