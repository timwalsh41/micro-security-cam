#ifndef AVI_WRITER_H
#define AVI_WRITER_H

#include <cstdint>
#include <cstddef>
#include <vector>

/**
 * @file avi_writer.h
 * @brief AVI container writer for MJPEG video streams.
 *
 * A format-only utility class that builds AVI file structures in memory.
 * Does not perform any I/O — callers extract the formatted bytes through
 * accessor methods and handle file writing through their own I/O layer.
 *
 * AVI file layout produced:
 * @code
 *   RIFF header
 *   └─ LIST hdrl
 *      ├─ avih (main AVI header)
 *      └─ LIST strl
 *         ├─ strh (video stream header: MJPEG, FPS)
 *         └─ strf (BITMAPINFOHEADER)
 *   └─ LIST movi
 *      └─ Frame chunks (0x00dc + size + JPEG data + padding)
 *   └─ idx1 (frame index: chunk ID, flags, offset, size per frame)
 * @endcode
 *
 * Lifecycle:
 *   open() → writeFrame() × N → close()
 *
 * After open(), call header() to get the initial AVI header bytes.
 * After each writeFrame(), call lastFrameChunk() to get the formatted
 * chunk bytes. After close(), call patchedHeader() for the header with
 * final stats, and index() for the frame index.
 *
 * All byte accessors return pointers to internal buffers that remain
 * valid until the next mutating call (writeFrame, close, or destruction).
 */

/**
 * @brief A non-owning view into a byte buffer.
 */
struct AviBytes
{
    const uint8_t *data;    ///< Pointer to byte data
    size_t len;             ///< Number of bytes
};

/**
 * @class AviWriter
 * @brief Formats AVI byte streams for MJPEG video recording.
 *
 * Builds AVI file structures (header, frame chunks, index) in memory.
 * The VideoController feeds these bytes to SdWriteBuffer for actual I/O.
 */
class AviWriter
{
    public:
        AviWriter();

        /**
         * @brief Initializes the AVI writer for a new recording.
         *
         * Generates the 310-byte AVI header template with the given
         * recording parameters. The path is stored as metadata only —
         * no file I/O is performed.
         *
         * @param path   File path (stored as metadata, not used for I/O).
         * @param fps    Frames per second.
         * @param width  Frame width in pixels.
         * @param height Frame height in pixels.
         * @return true if the writer was successfully initialized.
         */
        bool open(const char *path, uint16_t fps, uint16_t width, uint16_t height);

        /**
         * @brief Formats a JPEG frame as an AVI chunk.
         *
         * Builds a chunk with the 0x00dc marker, frame data size, JPEG
         * data, and DWORD-alignment padding. The formatted bytes are
         * available via lastFrameChunk() until the next writeFrame() call.
         *
         * @param jpegData Pointer to JPEG frame data.
         * @param jpegLen  Length of JPEG data in bytes.
         * @return true if the frame was formatted successfully.
         */
        bool writeFrame(const uint8_t *jpegData, size_t jpegLen);

        /**
         * @brief Finalizes the AVI with actual frame count and file size.
         *
         * Patches the header with final statistics (RIFF size, frame
         * count, movi list size) and builds the frame index. After
         * close(), the patched header and index are available via
         * patchedHeader() and index() accessors.
         *
         * @return true if the AVI was successfully finalized.
         */
        bool close();

        /**
         * @brief Returns whether the writer is in an open state.
         * @return true if open() was called and close() has not been called.
         */
        bool isOpen() const;

        /**
         * @brief Returns the number of frames written.
         * @return Frame count (only valid after at least one writeFrame() call).
         */
        uint32_t frameCount() const;

        /**
         * @brief Returns the total file size in bytes.
         *
         * Includes the header, all frame chunks (with padding), the
         * idx1 chunk header, and the index entries. Only valid after
         * close().
         *
         * @return Total file size in bytes.
         */
        uint32_t fileSize() const;

        /**
         * @brief Returns the file path passed to open().
         * @return Path string, or nullptr if not open.
         */
        const char *path() const;

        /**
         * @brief Returns the initial AVI header bytes.
         *
         * Valid after open() and before close(). After close(), use
         * patchedHeader() for the finalized version.
         *
         * @return AviBytes pointing to the header buffer.
         */
        AviBytes header() const;

        /**
         * @brief Returns the last formatted frame chunk bytes.
         *
         * Valid after writeFrame() and until the next writeFrame() or
         * close() call. Includes the 8-byte chunk header (0x00dc + size),
         * JPEG data, and padding.
         *
         * @return AviBytes pointing to the frame chunk buffer.
         */
        AviBytes lastFrameChunk() const;

        /**
         * @brief Returns the frame index bytes.
         *
         * Valid after close(). Contains one 16-byte entry per frame:
         * chunk ID, flags, offset, and size.
         *
         * @return AviBytes pointing to the index buffer.
         */
        AviBytes index() const;

        /**
         * @brief Returns the patched AVI header bytes.
         *
         * Valid after close(). Contains the header with final RIFF size,
         * frame count, and movi list size patched in. Use this when
         * seeking back to position 0 to overwrite the initial header.
         *
         * @return AviBytes pointing to the patched header buffer.
         */
        AviBytes patchedHeader() const;

    private:
        static constexpr size_t AVI_HEADER_SIZE = 224;
        static constexpr uint8_t CHUNK_HEADER_SIZE = 8;
        static constexpr uint32_t INDEX_ENTRY_SIZE = 16;
        static constexpr uint32_t MOVI_DATA_OFFSET = 4;

        // Header field offsets
        static constexpr size_t RIFF_SIZE_OFFSET = 4;
        static constexpr size_t HDRL_LIST_SIZE_OFFSET = 16;
        static constexpr size_t AVIH_FRAME_COUNT_OFFSET = 48;
        static constexpr size_t STRL_LIST_SIZE_OFFSET = 92;
        static constexpr size_t STRH_FRAME_COUNT_OFFSET = 136;
        static constexpr size_t MOVI_LIST_SIZE_OFFSET = 216;

        void buildHeader();
        void patchHeader();
        void buildIndex();
        size_t dwordAlign(size_t size) const;

        /**
         * @brief Writes a 32-bit little-endian value into a byte buffer.
         */
        void writeU32(std::vector<uint8_t> &buf, size_t offset, uint32_t value) const;

        /**
         * @brief Writes a 16-bit little-endian value into a byte buffer.
         */
        void writeU16(std::vector<uint8_t> &buf, size_t offset, uint16_t value) const;

        /**
         * @brief Writes a FourCC (4-byte identifier) into a byte buffer.
         */
        void writeFourCC(std::vector<uint8_t> &buf, size_t offset,
                         const char fourcc[4]) const;

        bool m_open{false};
        const char *m_path{nullptr};
        uint16_t m_fps{0};
        uint16_t m_width{0};
        uint16_t m_height{0};

        uint32_t m_frameCount{0};
        uint32_t m_dataSize{0};

        std::vector<uint8_t> m_header;
        std::vector<uint8_t> m_frameChunk;
        std::vector<uint8_t> m_indexBuffer;

        struct IndexEntry
        {
            uint32_t offset;    ///< Offset from start of movi data to chunk header
            uint32_t size;      ///< JPEG data size (without chunk header or padding)
        };
        std::vector<IndexEntry> m_indexEntries;
};

#endif // AVI_WRITER_H
