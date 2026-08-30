#ifndef STREAM_SERVICE_H
#define STREAM_SERVICE_H

#include "logging_service.h"
#include "stream_frame_channel.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

/**
 * @file stream_service.h
 * @brief Live MJPEG streaming service.
 *
 * Distributes captured JPEG frames to connected viewers as a
 * `multipart/x-mixed-replace` HTTP stream. Each viewer occupies one
 * slot holding a StreamFrameChannel: deliver_frame() formats the
 * multipart part (header, JPEG bytes, footer) and pushes it into the
 * per-viewer channel; the transport response attached at connect time
 * pulls the bytes from the channel as the client acknowledges data, so
 * flow control and framing integrity are the transport framework's
 * responsibility and frame data is never truncated by a send-buffer
 * limit.
 *
 * The class exposes the full API, while VideoController depends only on
 * the minimal IStreamService interface (mirroring ICaptureScheduler),
 * keeping VideoController decoupled and natively testable.
 */

/**
 * @class IStreamService
 * @brief Minimal frame-sink interface.
 *
 * Implementations receive captured JPEG frames for distribution to
 * connected stream clients.
 */
class IStreamService
{
    public:
        virtual ~IStreamService() = default;

        /**
         * @brief Delivers a captured JPEG frame to all active streams.
         *
         * @param data Pointer to JPEG frame bytes.
         * @param len  Number of frame bytes.
         */
        virtual void deliver_frame(const uint8_t* data, size_t len) = 0;

        /**
         * @brief Stops and closes all active streams.
         *
         * Writes the closing multipart boundary and closes each active
         * stream so connected clients terminate cleanly. Invoked by
         * `VideoController` whenever streaming mode turns off and when
         * the capture pipeline is torn down, so viewers never outlive
         * frame delivery.
         */
        virtual void stop_all_streams() = 0;
};

/**
 * @brief MJPEG multipart boundary token (macro so it can be used in
 *        string-literal concatenation for the framing constants below).
 */
#define STREAM_BOUNDARY_TOKEN "CAM_BOUNDARY"

/**
 * @class StreamService
 * @brief Manages concurrent MJPEG stream slots.
 *
 * Each active viewer occupies one slot holding a StreamFrameChannel.
 * deliver_frame() pushes a complete pre-formatted multipart part into
 * each active channel; the transport response pulls the bytes as the
 * client acknowledges data. A push while the previous part is still
 * pending is dropped (live-preview semantics), so producer latency
 * stays flat and no per-slot queue grows unboundedly.
 */
class StreamService : public IStreamService
{
    public:
        /**
         * @brief Maximum number of concurrent stream slots.
         */
        static constexpr uint8_t MAX_STREAMS = 8;

        /**
         * @brief Constructs a StreamService.
         * @param log Reference to LoggingService for diagnostics.
         */
        explicit StreamService(LoggingService& log);

        /**
         * @brief Configures the maximum number of concurrent streams.
         *
         * Must be called before start_stream(). Values above
         * MAX_STREAMS are clamped.
         *
         * @param max_streams Maximum concurrent viewers.
         */
        void init(uint8_t max_streams);

        /**
         * @brief Registers a new viewer and seeds its multipart stream.
         *
         * Seeds the multipart boundary opening as the first bytes of
         * the channel (the HTTP 200 response headers are written by the
         * transport response attached to the request) and binds the
         * channel to a free slot.
         *
         * @param channel Per-viewer frame channel; ownership is shared
         *                with the transport response attached to the
         *                viewer's request.
         * @return The slot index on success, or -1 if no slot is free
         *         or the channel is null.
         */
        int start_stream(std::shared_ptr<StreamFrameChannel> channel);

        /**
         * @brief Stops the stream in the given slot.
         *
         * Pushes the closing multipart boundary (best effort) and marks
         * the channel closed, then frees the slot. The transport
         * response drains the pending bytes and completes; its end
         * callback later observes no matching slot (idempotent).
         *
         * @param slot Slot index (as returned by start_stream()).
         */
        void stop_stream(uint8_t slot);

        /**
         * @brief Stops all active streams.
         *
         * Iterates every slot and stops any active stream via
         * stop_stream(). Invoked by `VideoController` whenever
         * streaming mode turns off (e.g. STOP_STREAM) and when the
         * capture loop is torn down, so connected viewers are closed
         * and their slots freed rather than left occupying capacity.
         */
        void stop_all_streams() override;

        /**
         * @brief Delivers a JPEG frame to all active streams.
         *
         * Formats the complete multipart part and pushes it into each
         * active viewer's channel. If a viewer is still draining its
         * previous part, the frame is dropped for that viewer (live
         * preview semantics; the transport pulls at client speed).
         *
         * @param data Pointer to JPEG frame bytes.
         * @param len  Number of frame bytes.
         */
        void deliver_frame(const uint8_t* data, size_t len) override;

        /**
         * @brief Frees the slot whose streaming response ended.
         *
         * Invoked via the end callback fired by the transport response
         * when it is destroyed — on client disconnect or after the
         * response completes. Idempotent: a slot already freed by
         * stop_stream() (or an unknown channel) is a no-op.
         *
         * @param channel Channel of the ended response.
         */
        void on_stream_ended(StreamFrameChannel* channel);

        /**
         * @brief Returns the number of active stream slots.
         */
        uint8_t active_slot_count() const;

    private:
        /** Multipart opening written once per stream, before the first part. */
        static constexpr const char* OPENING_BOUNDARY =
            "--" STREAM_BOUNDARY_TOKEN "\r\n";

        /** Best-effort multipart closing token. */
        static constexpr const char* CLOSING_BOUNDARY =
            "--" STREAM_BOUNDARY_TOKEN "--\r\n";

        /** Fixed per-frame part header (Content-Length is appended). */
        static constexpr const char* PART_HEADER =
            "--" STREAM_BOUNDARY_TOKEN "\r\n"
            "Content-Type: image/jpeg\r\n"
            "Content-Length: ";

        /** Suffix closing a frame part. */
        static constexpr const char* PART_FOOTER = "\r\n";

        static constexpr const char* SUBSYSTEM = "STREAM";

        /** A single viewer slot. */
        struct StreamSlot
        {
            std::shared_ptr<StreamFrameChannel> channel;
            bool active{false};
        };

        LoggingService& m_log;
        uint8_t m_max_streams{0};
        StreamSlot m_slots[MAX_STREAMS]{};

        /**
         * Scratch buffer used by deliver_frame() to assemble a complete
         * multipart part before pushing it into a channel. Only the
         * frame producer's task calls deliver_frame(), so a single
         * reused buffer avoids per-frame allocations.
         */
        std::vector<uint8_t> m_part_scratch;
};

#endif // STREAM_SERVICE_H
