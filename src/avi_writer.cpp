#include "avi_writer.h"
#include <cstring>

// ============================================================
// Construction
// ============================================================

AviWriter::AviWriter()
{
}

// ============================================================
// Lifecycle
// ============================================================

bool AviWriter::open(const char *path, uint16_t fps, uint16_t width, uint16_t height)
{
    if (m_open)
    {
        return false;
    }

    m_path = path;
    m_fps = fps;
    m_width = width;
    m_height = height;
    m_frameCount = 0;
    m_dataSize = 0;

    m_indexEntries.clear();
    m_indexBuffer.clear();

    buildHeader();
    m_open = true;
    return true;
}

bool AviWriter::writeFrame(const uint8_t *jpegData, size_t jpegLen)
{
    if (!m_open || !jpegData || jpegLen == 0)
    {
        return false;
    }

    // Calculate DWORD-aligned total chunk size
    size_t rawSize = CHUNK_HEADER_SIZE + jpegLen;
    size_t alignedSize = dwordAlign(rawSize);
    size_t padding = alignedSize - rawSize;

    // Build frame chunk: marker(4) + size(4) + data + padding
    m_frameChunk.resize(alignedSize);

    // Write chunk marker: stream 0, compressed video = 0x00dc
    writeU32(m_frameChunk, 0, 0x00dc);

    // Write data size (JPEG only, not including header or padding)
    writeU32(m_frameChunk, 4, static_cast<uint32_t>(jpegLen));

    // Copy JPEG data
    memcpy(&m_frameChunk[CHUNK_HEADER_SIZE], jpegData, jpegLen);

    // Zero-fill padding
    if (padding > 0)
    {
        memset(&m_frameChunk[rawSize], 0, padding);
    }

    // Record index entry: offset is from start of movi data to chunk header
    uint32_t chunkOffset = MOVI_DATA_OFFSET + m_dataSize;
    IndexEntry entry;
    entry.offset = chunkOffset;
    entry.size = static_cast<uint32_t>(jpegLen);
    m_indexEntries.push_back(entry);

    // Update counters
    m_dataSize += static_cast<uint32_t>(alignedSize);
    m_frameCount++;

    return true;
}

bool AviWriter::close()
{
    if (!m_open)
    {
        return false;
    }

    patchHeader();
    buildIndex();
    m_open = false;
    return true;
}

// ============================================================
// State queries
// ============================================================

bool AviWriter::isOpen() const
{
    return m_open;
}

uint32_t AviWriter::frameCount() const
{
    return m_frameCount;
}

uint32_t AviWriter::fileSize() const
{
    if (m_header.empty())
    {
        return 0;
    }
    // Total = header + movi data + idx1 header + index data
    return AVI_HEADER_SIZE + m_dataSize + CHUNK_HEADER_SIZE +
           static_cast<uint32_t>(m_indexEntries.size() * INDEX_ENTRY_SIZE);
}

const char *AviWriter::path() const
{
    return m_path;
}

// ============================================================
// Byte accessors
// ============================================================

AviBytes AviWriter::header() const
{
    if (m_header.empty())
    {
        return {nullptr, 0};
    }
    return {m_header.data(), m_header.size()};
}

AviBytes AviWriter::lastFrameChunk() const
{
    if (m_frameChunk.empty())
    {
        return {nullptr, 0};
    }
    return {m_frameChunk.data(), m_frameChunk.size()};
}

AviBytes AviWriter::index() const
{
    if (m_indexBuffer.empty())
    {
        return {nullptr, 0};
    }
    return {m_indexBuffer.data(), m_indexBuffer.size()};
}

AviBytes AviWriter::patchedHeader() const
{
    if (m_header.empty())
    {
        return {nullptr, 0};
    }
    return {m_header.data(), m_header.size()};
}

// ============================================================
// Header construction
// ============================================================

void AviWriter::buildHeader()
{
    m_header.resize(AVI_HEADER_SIZE, 0);

    // ---- RIFF header ----
    writeFourCC(m_header, 0, "RIFF");
    // RIFF size patched at close()
    writeFourCC(m_header, 8, "AVI ");

    // ---- LIST hdrl ----
    writeFourCC(m_header, 12, "LIST");
    writeU32(m_header, HDRL_LIST_SIZE_OFFSET, 192);
    writeFourCC(m_header, 20, "hdrl");

    // ---- avih (Main AVI Header) ----
    writeFourCC(m_header, 24, "avih");
    writeU32(m_header, 28, 56);  // data size
    writeU32(m_header, 32, 1000000u / m_fps);  // dwMicroSecPerFrame
    // dwMaxBytesPerSec = 0 (offset 36)
    // dwPaddingGranularity = 0 (offset 40)
    writeU32(m_header, 44, 0x10);  // dwFlags = AVIF_HASINDEX
    // dwTotalFrames patched at close() (offset 48)
    // dwInitialFrames = 0 (offset 52)
    writeU32(m_header, 56, 1);  // dwStreams = 1
    // dwSuggestedBufferSize = 0 (offset 60)
    writeU32(m_header, 64, m_width);
    writeU32(m_header, 68, m_height);
    // dwReserved[0..3] = 0 (offsets 72-84)

    // ---- LIST strl (video stream) ----
    writeFourCC(m_header, 88, "LIST");
    writeU32(m_header, STRL_LIST_SIZE_OFFSET, 116);
    writeFourCC(m_header, 96, "strl");

    // ---- strh (Stream Header) ----
    writeFourCC(m_header, 100, "strh");
    writeU32(m_header, 104, 56);  // data size
    writeFourCC(m_header, 108, "vids");  // stream type: video
    writeFourCC(m_header, 112, "MJPG");  // handler: Motion JPEG
    // dwFlags = 0 (offset 116)
    // wPriority = 0, wInitialFrames = 0 (offset 120)
    writeU32(m_header, 124, 1);  // dwScale
    writeU32(m_header, 128, m_fps);  // dwRate = fps * scale
    // dwStart = 0 (offset 132)
    // dwLength patched at close() (offset 136)
    // dwSuggestedBufferSize = 0 (offset 140)
    writeU32(m_header, 148, 0xFFFFFFFF);  // dwQuality = default (-1)
    // dwSampleSize = 0 (offset 148)

    // ---- strf (Stream Format: BITMAPINFOHEADER) ----
    writeFourCC(m_header, 152, "strf");
    writeU32(m_header, 156, 40);  // data size
    writeU32(m_header, 160, 40);  // biSize
    writeU32(m_header, 164, m_width);
    writeU32(m_header, 168, m_height);
    writeU16(m_header, 172, 1);   // biPlanes
    writeU16(m_header, 174, 24);  // biBitCount
    writeFourCC(m_header, 176, "MJPG");  // biCompression
    // biSizeImage = 0 (offset 182)
    // biXPelsPerMeter = 0 (offset 186)
    // biYPelsPerMeter = 0 (offset 190)
    // biClrUsed = 0 (offset 194)
    // biClrImportant = 0 (offset 198)

    // ---- LIST movi ----
    writeFourCC(m_header, 212, "LIST");
    // movi list size patched at close() (offset 216)
    writeFourCC(m_header, 220, "movi");
}

// ============================================================
// Header patching
// ============================================================

void AviWriter::patchHeader()
{
    // RIFF total size = total file size - 8
    uint32_t totalSize = fileSize();
    writeU32(m_header, RIFF_SIZE_OFFSET, totalSize - 8);

    // avih: dwTotalFrames
    writeU32(m_header, AVIH_FRAME_COUNT_OFFSET, m_frameCount);

    // strh: dwLength (frame count)
    writeU32(m_header, STRH_FRAME_COUNT_OFFSET, m_frameCount);

    // LIST movi size = 4 ("movi") + total frame data with padding
    writeU32(m_header, MOVI_LIST_SIZE_OFFSET, MOVI_DATA_OFFSET + m_dataSize);
}

// ============================================================
// Index construction
// ============================================================

void AviWriter::buildIndex()
{
    size_t indexSize = m_indexEntries.size() * INDEX_ENTRY_SIZE;
    m_indexBuffer.resize(CHUNK_HEADER_SIZE + indexSize);

    // idx1 chunk header
    writeFourCC(m_indexBuffer, 0, "idx1");
    writeU32(m_indexBuffer, 4, static_cast<uint32_t>(indexSize));

    // Index entries
    for (size_t i = 0; i < m_indexEntries.size(); i++)
    {
        size_t offset = CHUNK_HEADER_SIZE + (i * INDEX_ENTRY_SIZE);
        const IndexEntry &entry = m_indexEntries[i];

        writeU32(m_indexBuffer, offset, 0x00dc);          // ckid
        writeU32(m_indexBuffer, offset + 4, 0);           // dwFlags
        writeU32(m_indexBuffer, offset + 8, entry.offset); // dwOffset
        writeU32(m_indexBuffer, offset + 12, entry.size);  // dwSize
    }
}

// ============================================================
// Utility methods
// ============================================================

size_t AviWriter::dwordAlign(size_t size) const
{
    return (size + 3) & ~static_cast<size_t>(3);
}

void AviWriter::writeU32(std::vector<uint8_t> &buf, size_t offset, uint32_t value) const
{
    if (offset + 4 <= buf.size())
    {
        memcpy(&buf[offset], &value, sizeof(uint32_t));
    }
}

void AviWriter::writeU16(std::vector<uint8_t> &buf, size_t offset, uint16_t value) const
{
    if (offset + 2 <= buf.size())
    {
        memcpy(&buf[offset], &value, sizeof(uint16_t));
    }
}

void AviWriter::writeFourCC(std::vector<uint8_t> &buf, size_t offset,
                            const char fourcc[4]) const
{
    if (offset + 4 <= buf.size())
    {
        memcpy(&buf[offset], fourcc, 4);
    }
}
