#ifndef STREAM_SERVICE_H
#define STREAM_SERVICE_H

#include "hal/stream_writer.h"
#include "logging_service.h"

#include <cstddef>
#include <cstdint>
#include <memory>

/**
 * @file stream_service.h
 * @brief Live MJPEG streaming service.
 *
 * Distributes captured JPEG frames to connected viewers as a
 * `multipart/x-mixed-replace` HTTP stream. The service is synchronous
 * and platform-agnostic: deliver_frame() writes the multipart framing
 * plus frame bytes directly to each active StreamWriter. On ESP32 the
 * writers are AsyncWebStreamWriter instances (non-blocking AsyncTCP
 * writes); native tests inject MockStreamWriter instances.
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
 * Each active viewer occupies one slot holding a StreamWriter. Frames
 * are delivered synchronously to every active slot. There are no
 * per-slot frame buffers or tasks — backpressure is handled by the
 * underlying transport's send queue (AsyncTCP) rather than by copying
 * frames into PSRAM.
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
         * @brief Registers a new viewer and writes the stream preamble.
         *
         * Writes the HTTP 200 response headers and the multipart
         * boundary opening for the new stream, then binds the writer
         * to a free slot.
         *
         * @param writer StreamWriter for the client connection.
         *               Ownership transfers to the service.
         * @return The slot index on success, or -1 if no slot is free.
         */
        int start_stream(std::unique_ptr<hal::StreamWriter> writer);

        /**
         * @brief Stops and closes the stream in the given slot.
         *
         * Writes the closing multipart boundary, closes the writer, and
         * frees the slot.
         *
         * @param slot Slot index (as returned by start_stream()).
         */
        void stop_stream(uint8_t slot);

        /**
         * @brief Stops and closes all active streams.
         *
         * Iterates every slot and closes any active stream via
         * stop_stream(). Invoked by `VideoController` whenever
         * streaming mode turns off (e.g. STOP_STREAM) and when the
         * capture loop is torn down, so connected viewers are closed
         * and their writers freed rather than left occupying slots.
         */
        void stop_all_streams();

        /**
         * @brief Delivers a JPEG frame to all active streams.
         * @param data Pointer to JPEG frame bytes.
         * @param len  Number of frame bytes.
         */
        void deliver_frame(const uint8_t* data, size_t len) override;

        /**
         * @brief Returns the number of active stream slots.
         */
        uint8_t active_slot_count() const;

    private:
        /** HTTP response preamble written once per stream. */
        static constexpr const char* PREAMBLE =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: multipart/x-mixed-replace; boundary="
            STREAM_BOUNDARY_TOKEN "\r\n"
            "\r\n";

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
            std::unique_ptr<hal::StreamWriter> writer;
            bool active{false};
        };

        /**
         * @brief Reclaims the writer in a slot without closing it.
         *
         * Used by the writer's disconnect callback; the connection has
         * already dropped, so close() must not be called (it could
         * re-fire the disconnect callback).
         *
         * @param slot Slot index to free.
         */
        void free_slot(uint8_t slot);

        LoggingService& m_log;
        uint8_t m_max_streams{0};
        StreamSlot m_slots[MAX_STREAMS]{};
};

#endif // STREAM_SERVICE_H
