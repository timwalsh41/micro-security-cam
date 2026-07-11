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

bool AviWriter::open(const char* path, uint16_t fps, uint16_t width, uint16_t height)
{
    if (m_open)
    {
        return false;
    }

    m_path = path;
    m_fps = fps;
    m_width = width;
    m_height = height;
    m_frame_count = 0;
    m_data_size = 0;

    m_index_entries.clear();
    m_index_buffer.clear();

    build_header();
    m_open = true;
    return true;
}

bool AviWriter::write_frame(const uint8_t* jpeg_data, size_t jpeg_len)
{
    if (!m_open || !jpeg_data || jpeg_len == 0)
    {
        return false;
    }

    // Calculate DWORD-aligned total chunk size
    size_t raw_size = CHUNK_HEADER_SIZE + jpeg_len;
    size_t aligned_size = dword_align(raw_size);
    size_t padding = aligned_size - raw_size;

    // Build frame chunk: marker(4) + size(4) + data + padding
    m_frame_chunk.resize(aligned_size);

    // Write chunk marker: stream 0, compressed video = 0x00dc
    write_u32(m_frame_chunk, 0, 0x00dc);

    // Write data size (JPEG only, not including header or padding)
    write_u32(m_frame_chunk, 4, static_cast<uint32_t>(jpeg_len));

    // Copy JPEG data
    memcpy(&m_frame_chunk[CHUNK_HEADER_SIZE], jpeg_data, jpeg_len);

    // Zero-fill padding
    if (padding > 0)
    {
        memset(&m_frame_chunk[raw_size], 0, padding);
    }

    // Record index entry: offset is from start of movi data to chunk header
    uint32_t chunk_offset = MOVI_DATA_OFFSET + m_data_size;
    IndexEntry entry;
    entry.offset = chunk_offset;
    entry.size = static_cast<uint32_t>(jpeg_len);
    m_index_entries.push_back(entry);

    // Update counters
    m_data_size += static_cast<uint32_t>(aligned_size);
    m_frame_count++;

    return true;
}

bool AviWriter::close()
{
    if (!m_open)
    {
        return false;
    }

    patch_header();
    build_index();
    m_open = false;
    return true;
}

// ============================================================
// State queries
// ============================================================

bool AviWriter::is_open() const
{
    return m_open;
}

uint32_t AviWriter::frame_count() const
{
    return m_frame_count;
}

uint32_t AviWriter::file_size() const
{
    if (m_header.empty())
    {
        return 0;
    }
    // Total = header + movi data + idx1 header + index data
    return AVI_HEADER_SIZE + m_data_size + CHUNK_HEADER_SIZE +
           static_cast<uint32_t>(m_index_entries.size() * INDEX_ENTRY_SIZE);
}

const char* AviWriter::path() const
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

AviBytes AviWriter::last_frame_chunk() const
{
    if (m_frame_chunk.empty())
    {
        return {nullptr, 0};
    }
    return {m_frame_chunk.data(), m_frame_chunk.size()};
}

AviBytes AviWriter::index() const
{
    if (m_index_buffer.empty())
    {
        return {nullptr, 0};
    }
    return {m_index_buffer.data(), m_index_buffer.size()};
}

AviBytes AviWriter::patched_header() const
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

void AviWriter::build_header()
{
    m_header.resize(AVI_HEADER_SIZE, 0);

    // ---- RIFF header ----
    write_four_cc(m_header, 0, "RIFF");
    // RIFF size patched at close()
    write_four_cc(m_header, 8, "AVI ");

    // ---- LIST hdrl ----
    write_four_cc(m_header, 12, "LIST");
    write_u32(m_header, HDRL_LIST_SIZE_OFFSET, 192);
    write_four_cc(m_header, 20, "hdrl");

    // ---- avih (Main AVI Header) ----
    write_four_cc(m_header, 24, "avih");
    write_u32(m_header, 28, 56);  // data size
    write_u32(m_header, 32, 1000000u / m_fps);  // dwMicroSecPerFrame
    // dwMaxBytesPerSec = 0 (offset 36)
    // dwPaddingGranularity = 0 (offset 40)
    write_u32(m_header, 44, 0x10);  // dwFlags = AVIF_HASINDEX
    // dwTotalFrames patched at close() (offset 48)
    // dwInitialFrames = 0 (offset 52)
    write_u32(m_header, 56, 1);  // dwStreams = 1
    // dwSuggestedBufferSize = 0 (offset 60)
    write_u32(m_header, 64, m_width);
    write_u32(m_header, 68, m_height);
    // dwReserved[0..3] = 0 (offsets 72-84)

    // ---- LIST strl (video stream) ----
    write_four_cc(m_header, 88, "LIST");
    write_u32(m_header, STRL_LIST_SIZE_OFFSET, 116);
    write_four_cc(m_header, 96, "strl");

    // ---- strh (Stream Header) ----
    write_four_cc(m_header, 100, "strh");
    write_u32(m_header, 104, 56);  // data size
    write_four_cc(m_header, 108, "vids");  // stream type: video
    write_four_cc(m_header, 112, "MJPG");  // handler: Motion JPEG
    // dwFlags = 0 (offset 116)
    // wPriority = 0 (offset 120)
    // wInitialFrames = 0 (offset 122)
    write_u32(m_header, 124, 1);  // dwScale
    write_u32(m_header, 128, m_fps);  // dwRate = fps * scale
    // dwStart = 0 (offset 132)
    // dwLength patched at close() (offset 136)
    // dwSuggestedBufferSize = 0 (offset 140)
    write_u32(m_header, 144, 0xFFFFFFFF);  // dwQuality = default (-1)
    // dwSampleSize = 0 (offset 148)

    // ---- strf (Stream Format: BITMAPINFOHEADER) ----
    write_four_cc(m_header, 164, "strf");
    write_u32(m_header, 168, 40);  // data size
    write_u32(m_header, 172, 40);  // biSize
    write_u32(m_header, 176, m_width);
    write_u32(m_header, 180, m_height);
    write_u16(m_header, 184, 1);   // biPlanes
    write_u16(m_header, 186, 24);  // biBitCount
    write_four_cc(m_header, 188, "MJPG");  // biCompression
    // biSizeImage = 0 (offset 192)
    // biXPelsPerMeter = 0 (offset 196)
    // biYPelsPerMeter = 0 (offset 200)
    // biClrUsed = 0 (offset 204)
    // biClrImportant = 0 (offset 208)

    // ---- LIST movi ----
    write_four_cc(m_header, 212, "LIST");
    // movi list size patched at close() (offset 216)
    write_four_cc(m_header, 220, "movi");
}

// ============================================================
// Header patching
// ============================================================

void AviWriter::patch_header()
{
    // RIFF total size = total file size - 8
    uint32_t total_size = file_size();
    write_u32(m_header, RIFF_SIZE_OFFSET, total_size - 8);

    // avih: dwTotalFrames
    write_u32(m_header, AVIH_FRAME_COUNT_OFFSET, m_frame_count);

    // strh: dwLength (frame count)
    write_u32(m_header, STRH_FRAME_COUNT_OFFSET, m_frame_count);

    // LIST movi size = 4 ("movi") + total frame data with padding
    write_u32(m_header, MOVI_LIST_SIZE_OFFSET, MOVI_DATA_OFFSET + m_data_size);
}

// ============================================================
// Index construction
// ============================================================

void AviWriter::build_index()
{
    size_t index_size = m_index_entries.size() * INDEX_ENTRY_SIZE;
    m_index_buffer.resize(CHUNK_HEADER_SIZE + index_size);

    // idx1 chunk header
    write_four_cc(m_index_buffer, 0, "idx1");
    write_u32(m_index_buffer, 4, static_cast<uint32_t>(index_size));

    // Index entries
    for (size_t i = 0; i < m_index_entries.size(); i++)
    {
        size_t offset = CHUNK_HEADER_SIZE + (i * INDEX_ENTRY_SIZE);
        const IndexEntry& entry = m_index_entries[i];

        write_u32(m_index_buffer, offset, 0x00dc);          // ckid
        write_u32(m_index_buffer, offset + 4, 0);           // dwFlags
        write_u32(m_index_buffer, offset + 8, entry.offset); // dwOffset
        write_u32(m_index_buffer, offset + 12, entry.size);  // dwSize
    }
}

// ============================================================
// Utility methods
// ============================================================

size_t AviWriter::dword_align(size_t size) const
{
    return (size + 3) & ~static_cast<size_t>(3);
}

void AviWriter::write_u32(std::vector<uint8_t>& buf, size_t offset, uint32_t value) const
{
    if (offset + 4 <= buf.size())
    {
        memcpy(&buf[offset], &value, sizeof(uint32_t));
    }
}

void AviWriter::write_u16(std::vector<uint8_t>& buf, size_t offset, uint16_t value) const
{
    if (offset + 2 <= buf.size())
    {
        memcpy(&buf[offset], &value, sizeof(uint16_t));
    }
}

void AviWriter::write_four_cc(std::vector<uint8_t>& buf, size_t offset,
                            const char fourcc[4]) const
{
    if (offset + 4 <= buf.size())
    {
        memcpy(&buf[offset], fourcc, 4);
    }
}
