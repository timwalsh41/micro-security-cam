#ifndef SD_WRITE_BUFFER_H
#define SD_WRITE_BUFFER_H

#include <cstdint>
#include <cstddef>
#include "sd_controller.h"

/**
 * @file sd_write_buffer.h
 * @brief Buffered SD card writer for accumulating small writes.
 *
 * Collects incoming byte chunks into a RAM buffer and flushes them
 * to the SD card in larger, sector-aligned blocks. This reduces the
 * number of individual SD write transactions (each of which incurs
 * the full CS assertion/deassertion timing overhead) by batching
 * multiple frames into a single write.
 *
 * Lifecycle:
 *   open(path) → buffer_frame() × N → flush() → reset()
 *
 * The buffer operates in a single-threaded cooperative model: data
 * is accumulated via buffer_frame(), and when the internal buffer
 * reaches the RAMSIZE threshold, it is flushed synchronously to
 * the SD card via SdController::write_file(). The Arduino SD library
 * opens files in append mode (FILE_WRITE), so multiple flush()
 * calls to the same path correctly append data.
 */

/**
 * @class SdWriteBuffer
 * @brief Accumulates small byte chunks and flushes them to SD in
 *        sector-aligned blocks to minimize write transaction overhead.
 *
 * Buffer size is configurable at compile time via RAMSIZE (default 8KB).
 */
class SdWriteBuffer
{
    public:
        /** Internal buffer size in bytes (multiple of SD sector size). */
        static constexpr size_t RAMSIZE = 8192;

        /**
         * @brief Constructs an SdWriteBuffer with the given SD controller.
         * @param sd Reference to an SdController used for file I/O.
         */
        explicit SdWriteBuffer(SdController &sd);

        /**
         * @brief Sets the target file path for subsequent flushes.
         *
         * Must be called before buffer_frame() or flush(). Calling open()
         * while frames are buffered discards the buffered data.
         *
         * @param path File path to write to on the SD card; must remain
         *             valid until flush() or reset().
         * @return true if the buffer was successfully opened.
         */
        bool open(const char *path);

        /**
         * @brief Appends a pre-formatted AVI chunk to the internal buffer.
         *
         * The chunk should be a complete AVI frame chunk (including the
         * 8-byte chunk header: 0x00dc marker + size, followed by JPEG
         * data and any DWORD-alignment padding). When the buffer reaches
         * RAMSIZE, it is flushed synchronously to the SD card.
         *
         * @param avi_chunk Pointer to the formatted AVI chunk bytes.
         * @param chunk_len Length of the chunk in bytes.
         * @return true if the chunk was buffered (or flushed) successfully.
         */
        bool buffer_frame(const uint8_t *avi_chunk, size_t chunk_len);

        /**
         * @brief Flushes any remaining buffered data to the SD card.
         *
         * Writes the current buffer contents (less than RAMSIZE bytes)
         * to the file. Called at clip end or when recording stops.
         *
         * @return true if the flush succeeded, or if no data was buffered.
         */
        bool flush();

        /**
         * @brief Resets the buffer state.
         *
         * Discards any buffered data and resets the write position.
         * Does not change the file path set by open().
         */
        void reset();

        /**
         * @brief Returns whether the buffer has a target file path set.
         * @return true if open() was called and reset() has not cleared it.
         */
        bool is_open() const;

        /**
         * @brief Returns the number of bytes currently buffered.
         * @return Bytes waiting to be flushed (0 to RAMSIZE-1).
         */
        size_t buffered_bytes() const;

    private:
        /**
         * @brief Writes the current buffer contents to the SD card.
         *
         * Calls SdController::write_file() with the buffered data,
         * then resets the write position to 0.
         *
         * @return true if the write succeeded.
         */
        bool flush_buffer();

        SdController &m_sd;
        const char *m_path{nullptr};
        uint8_t m_buffer[RAMSIZE];
        size_t m_high_point{0};
        bool m_open{false};
};

#endif // SD_WRITE_BUFFER_H
