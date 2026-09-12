#ifndef STREAM_SERVICE_H
#define STREAM_SERVICE_H

#include "logging_service.h"
#include "stream_frame_channel.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <memory>
#include <utility>
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
 * Synchronization: viewer slots are mutated from several task contexts
 * — start_stream() binding and on_stream_ended() (viewer disconnect via
 * the transport response end callback) run in the transport's task,
 * while deliver_frame() pushes and stop_all_streams() run in the frame
 * producer's and stop callers' contexts. A single slot mutex serializes
 * every slot mutation against delivery: a viewer slot can never be
 * bound, freed, or reclaimed while the frame producer is iterating the
 * registry, and a disconnecting viewer's slot cannot be reclaimed twice
 * (on_stream_ended is idempotent under the mutex). The slot mutex is
 * held only around slot bookkeeping; channel pushes happen outside it.
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
         * Pushes a well-formed placeholder JPEG part (the
         * PLACEHOLDER_JPEG payload framed like a delivered frame) as
         * the first channel bytes and binds the channel to a free slot.
         * The channel therefore always holds bytes when the transport
         * response attaches, which keeps the response's first
         * head-plus-content write within its send-buffer reservation;
         * with an empty channel the transport framework defers the HTTP
         * head into a later content write and overflows the socket
         * buffer, silently truncating the first chunk. The seeded part
         * is a legal multipart part, so strict multipart parsers accept
         * the stream opening (a standalone empty opening boundary is
         * what they reject). The HTTP 200 response headers are written
         * by the transport response attached to the request; subsequent
         * parts are pushed by deliver_frame() and the placeholder is
         * replaced on screen by the first real frame per the channel's
         * freshest-frame mailbox semantics.
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

        /**
         * Capacity of a formatted part header: the fixed PART_HEADER
         * text plus the largest 32-bit decimal Content-Length and the
         * blank-line terminator.
         */
        static constexpr size_t PART_HEADER_CAPACITY = 80;

        /**
         * Valid 1x1 grayscale JPEG pushed as the first part of every
         * stream. Framed with the standard part header and footer it
         * forms a well-formed multipart part (a bare opening boundary
         * is rejected by strict multipart parsers), and it guarantees
         * the channel holds bytes at the moment the transport response
         * attaches. Clients render it as a placeholder frame until the
         * first real frame replaces it.
         */
        static constexpr uint8_t PLACEHOLDER_JPEG[] = {
            0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46,
            0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
            0xFF, 0xDB, 0x00, 0x43, 0x00, 0x28, 0x1C, 0x1E, 0x23, 0x1E,
            0x19, 0x28, 0x23, 0x21, 0x23, 0x2D, 0x2B, 0x28, 0x30, 0x3C,
            0x64, 0x41, 0x3C, 0x37, 0x37, 0x3C, 0x7B, 0x58, 0x5D, 0x49,
            0x64, 0x91, 0x80, 0x99, 0x96, 0x8F, 0x80, 0x8C, 0x8A, 0xA0,
            0xB4, 0xE6, 0xC3, 0xA0, 0xAA, 0xDA, 0xAD, 0x8A, 0x8C, 0xC8,
            0xFF, 0xCB, 0xDA, 0xEE, 0xF5, 0xFF, 0xFF, 0xFF, 0x9B, 0xC1,
            0xFF, 0xFF, 0xFF, 0xFA, 0xFF, 0xE6, 0xFD, 0xFF, 0xF8, 0xFF,
            0xC0, 0x00, 0x0B, 0x08, 0x00, 0x01, 0x00, 0x01, 0x01, 0x01,
            0x11, 0x00, 0xFF, 0xC4, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0xFF, 0xC4, 0x00, 0x14, 0x10, 0x01,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xDA, 0x00, 0x08,
            0x01, 0x01, 0x00, 0x00, 0x3F, 0x00, 0x3F, 0xFF, 0xD9
        };

        /** Length of the placeholder JPEG payload in bytes. */
        static constexpr size_t PLACEHOLDER_JPEG_LEN = sizeof(PLACEHOLDER_JPEG);

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
         * @brief Serializes slot bookkeeping across task contexts.
         *
         * Guards slot binding/reclamation/deactivation so concurrent
         * viewer connects, disconnects, stops, and frame deliveries
         * cannot interleave on the slot array. Never held while pushing
         * into or closing a channel; start_stream(), stop_stream(),
         * stop_all_streams(), and on_stream_ended() copy channels out
         * under this mutex and operate on the copies outside it.
         */
        mutable std::mutex m_slot_mutex;

        /**
         * @brief Formats the fixed part header for a payload length.
         *
         * Writes PART_HEADER, the decimal @p payload_len, and the
         * blank-line terminator into @p buf.
         *
         * @param buf         Output buffer.
         * @param cap         Output buffer capacity.
         * @param payload_len Payload length to declare.
         * @return The number of bytes written, or 0 on overflow.
         */
        static size_t format_part_header(char* buf, size_t cap,
                                         size_t payload_len);

        /**
         * Scratch buffer used by deliver_frame() to assemble a complete
         * multipart part before pushing it into a channel. Only the
         * frame producer's task calls deliver_frame(), so a single
         * reused buffer avoids per-frame allocations.
         */
        std::vector<uint8_t> m_part_scratch;
};

#endif // STREAM_SERVICE_H
