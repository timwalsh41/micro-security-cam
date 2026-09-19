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

    // Audio defaults
    m_audio_enabled = false;
    m_audio_data_size = 0;
    m_audio_sample_count = 0;
    m_audio_sample_rate = 16000;
    m_audio_bits_per_sample = 16;
    m_audio_channels = 1;

    // Header size is video-only by default; audio may be added before
    // the first frame via open_audio().
    m_header_size = AVI_HEADER_SIZE_VIDEO_ONLY;
    m_movi_list_size_offset = MOVI_LIST_SIZE_OFFSET;
    m_movi_data_offset = MOVI_DATA_OFFSET;
    m_audio_strl_offset = 0;
    m_audio_strl_size = 0;
    m_audio_strh_offset = 0;
    m_audio_strf_offset = 0;

    m_index_entries.clear();
    m_index_buffer.clear();

    build_header();
    m_open = true;
    return true;
}

bool AviWriter::open_audio(uint32_t sample_rate, uint16_t bits_per_sample,
                           uint16_t channels)
{
    if (!m_open || m_frame_count > 0)
    {
        return false;
    }

    m_audio_enabled = true;
    m_audio_sample_rate = sample_rate;
    m_audio_bits_per_sample = bits_per_sample;
    m_audio_channels = channels;
    m_audio_data_size = 0;
    m_audio_sample_count = 0;

    // Recompute header with audio stream
    m_header_size = AVI_HEADER_SIZE_DUAL_STREAM;
    m_movi_list_size_offset = DUAL_MOVI_LIST_SIZE_OFFSET;
    m_movi_data_offset = 4;

    build_header();
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

    // Write chunk ID: stream 0, compressed video = "00dc" FourCC
    write_four_cc(m_frame_chunk, 0, "00dc");

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
    uint32_t chunk_offset = static_cast<uint32_t>(m_movi_data_offset) + m_data_size;
    IndexEntry entry;
    entry.offset = chunk_offset;
    entry.size = static_cast<uint32_t>(jpeg_len);
    entry.is_audio = false;
    m_index_entries.push_back(entry);

    // Update counters
    m_data_size += static_cast<uint32_t>(aligned_size);
    m_frame_count++;

    return true;
}

bool AviWriter::write_audio_chunk(const uint8_t* audio_data, size_t audio_len)
{
    if (!m_open || !m_audio_enabled || !audio_data || audio_len == 0)
    {
        return false;
    }

    // Calculate DWORD-aligned total chunk size
    size_t raw_size = CHUNK_HEADER_SIZE + audio_len;
    size_t aligned_size = dword_align(raw_size);
    size_t padding = aligned_size - raw_size;

    // Build audio chunk: marker(4) + size(4) + data + padding
    m_audio_chunk.resize(aligned_size);

    // Write chunk ID: stream 1, audio = "01wb" FourCC
    write_four_cc(m_audio_chunk, 0, "01wb");

    // Write data size (audio only, not including header or padding)
    write_u32(m_audio_chunk, 4, static_cast<uint32_t>(audio_len));

    // Copy audio data
    memcpy(&m_audio_chunk[CHUNK_HEADER_SIZE], audio_data, audio_len);

    // Zero-fill padding
    if (padding > 0)
    {
        memset(&m_audio_chunk[raw_size], 0, padding);
    }

    // Record index entry: offset is from start of movi data to chunk header
    // Audio chunks are interleaved with video, so the offset is based on
    // m_data_size (which includes both video and audio data).
    uint32_t chunk_offset = static_cast<uint32_t>(m_movi_data_offset) + m_data_size;
    IndexEntry entry;
    entry.offset = chunk_offset;
    entry.size = static_cast<uint32_t>(audio_len);
    entry.is_audio = true;
    m_index_entries.push_back(entry);

    // Update counters
    m_data_size += static_cast<uint32_t>(aligned_size);
    m_audio_data_size += static_cast<uint32_t>(audio_len);
    // audio_len is in bytes; divide by bytes_per_sample to get sample count
    m_audio_sample_count += audio_len / (m_audio_bits_per_sample / 8);

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
    return static_cast<uint32_t>(m_header_size) + m_data_size +
           CHUNK_HEADER_SIZE +
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

AviBytes AviWriter::last_audio_chunk() const
{
    if (m_audio_chunk.empty())
    {
        return {nullptr, 0};
    }
    return {m_audio_chunk.data(), m_audio_chunk.size()};
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
    m_header.resize(m_header_size, 0);

    // ---- RIFF header ----
    write_four_cc(m_header, 0, "RIFF");
    // RIFF size patched at close()
    write_four_cc(m_header, 8, "AVI ");

    // ---- LIST hdrl ----
    write_four_cc(m_header, 12, "LIST");
    // hdrl list size patched at close()
    write_u32(m_header, HDRL_LIST_SIZE_OFFSET, 0);  // Placeholder
    write_four_cc(m_header, 20, "hdrl");

    // ---- avih (Main AVI Header) ----
    write_four_cc(m_header, 24, "avih");
    write_u32(m_header, 28, 56);  // data size
    write_u32(m_header, 32, 1000000u / m_fps);  // dwMicroSecPerFrame
    // dwMaxBytesPerSec = 0 (offset 36)
    // dwPaddingGranularity = 0 (offset 40)
    write_u32(m_header, 44, 0x910);  // dwFlags = AVIF_HASINDEX | AVIF_WASCAPKEYFRAME | AVIF_TRUSTCKTYPE
    // dwTotalFrames patched at close() (offset 48)
    // dwInitialFrames = 0 (offset 52)
    write_u32(m_header, 56, m_audio_enabled ? 2 : 1);  // dwStreams
    // dwSuggestedBufferSize = 0 (offset 60)
    write_u32(m_header, 64, m_width);
    write_u32(m_header, 68, m_height);
    // dwReserved[0..3] = 0 (offsets 72-84)

    // ---- LIST strl (video stream) ----
    write_four_cc(m_header, 88, "LIST");
    write_u32(m_header, STRL_LIST_SIZE_OFFSET, 116);  // Video strl size
    write_four_cc(m_header, 96, "strl");

    // ---- strh (Video Stream Header) ----
    write_four_cc(m_header, 100, "strh");
    write_u32(m_header, 104, 56);  // data size
    write_four_cc(m_header, 108, "vids");  // stream type: video
    write_four_cc(m_header, 112, "MJPG");  // handler: Motion JPEG
    // dwFlags = 0 (offset 116)
    // wPriority = 0 (offset 120)
    // wLanguage = 0 (offset 122)
    // dwInitialFrames = 0 (offset 124)
    write_u32(m_header, 128, 1);  // dwScale
    write_u32(m_header, 132, m_fps);  // dwRate
    // dwStart = 0 (offset 136)
    // dwLength patched at close (offset 140)
    // dwSuggestedBufferSize = 0 (offset 144)
    write_u32(m_header, 148, 0xFFFFFFFF);  // dwQuality = default (-1)
    // dwSampleSize = 0 (offset 152)

    // ---- strf (Video Stream Format: BITMAPINFOHEADER) ----
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

    // If audio is enabled, build the audio stream header
    if (m_audio_enabled)
    {
        build_audio_stream_header();
    }

    // ---- LIST movi ----
    // LIST FourCC is at movi_list_size_offset - 4
    write_four_cc(m_header, m_movi_list_size_offset - 4, "LIST");
    // movi list size patched at close()
    write_u32(m_header, m_movi_list_size_offset, 0);  // Placeholder
    write_four_cc(m_header, m_movi_list_size_offset + 4, "movi");
}

void AviWriter::build_audio_stream_header()
{
    // Audio strl list
    write_four_cc(m_header, AUDIO_STRL_LIST_OFFSET, "LIST");
    write_u32(m_header, AUDIO_STRL_LIST_SIZE_OFFSET, 116);  // Audio strl size
    write_four_cc(m_header, AUDIO_STRL_LIST_OFFSET + 8, "strl");

    // Audio strh (Stream Header)
    write_four_cc(m_header, AUDIO_STRH_OFFSET, "strh");
    write_u32(m_header, AUDIO_STRH_OFFSET + 4, 56);  // data size
    write_four_cc(m_header, AUDIO_STRH_DATA_OFFSET, "auds");  // stream type: audio
    write_four_cc(m_header, AUDIO_STRH_DATA_OFFSET + 4, "  \0");  // handler: no handler (PCM)
    // dwFlags = 0 (offset AUDIO_STRH_DATA_OFFSET + 8)
    // wPriority = 0 (offset AUDIO_STRH_DATA_OFFSET + 12)
    // wLanguage = 0 (offset AUDIO_STRH_DATA_OFFSET + 14)
    // dwInitialFrames = 0 (offset AUDIO_STRH_DATA_OFFSET + 16)
    write_u32(m_header, AUDIO_STRH_DATA_OFFSET + 20, 1);  // dwScale
    write_u32(m_header, AUDIO_STRH_DATA_OFFSET + 24, m_audio_sample_rate);  // dwRate
    // dwStart = 0 (offset AUDIO_STRH_DATA_OFFSET + 28)
    // dwLength patched at close() (offset AUDIO_STRH_DATA_OFFSET + 32)
    // dwSuggestedBufferSize = 0 (offset AUDIO_STRH_DATA_OFFSET + 36)
    write_u32(m_header, AUDIO_STRH_DATA_OFFSET + 40, 0xFFFFFFFF);  // dwQuality = default (-1)
    write_u32(m_header, AUDIO_STRH_DATA_OFFSET + 44,
              static_cast<uint32_t>(m_audio_bits_per_sample * m_audio_channels / 8));  // dwSampleSize

    // Audio strf (Stream Format: WAVEFORMATEX)
    write_four_cc(m_header, AUDIO_STRF_OFFSET, "strf");
    write_u32(m_header, AUDIO_STRF_DATA_OFFSET, 16);  // data size (WAVEFORMATEX)
    write_u16(m_header, AUDIO_STRF_DATA_OFFSET + 4, 1);  // wFormatTag = PCM
    write_u16(m_header, AUDIO_STRF_DATA_OFFSET + 6, m_audio_channels);  // nChannels
    write_u32(m_header, AUDIO_STRF_DATA_OFFSET + 8, m_audio_sample_rate);  // nSamplesPerSec
    write_u32(m_header, AUDIO_STRF_DATA_OFFSET + 12,
              m_audio_sample_rate * m_audio_bits_per_sample * m_audio_channels / 8);  // nAvgBytesPerSec
    write_u16(m_header, AUDIO_STRF_DATA_OFFSET + 16,
              m_audio_bits_per_sample * m_audio_channels / 8);  // nBlockAlign
    write_u16(m_header, AUDIO_STRF_DATA_OFFSET + 18, m_audio_bits_per_sample);  // wBitsPerSample
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

    // video strh: dwLength (frame count)
    write_u32(m_header, STRH_FRAME_COUNT_OFFSET, m_frame_count);

    // LIST hdrl size
    if (m_audio_enabled)
    {
        // hdrl = avih(64) + video_strl(124) + audio_strl(124) = 312
        write_u32(m_header, HDRL_LIST_SIZE_OFFSET, 304);
    }
    else
    {
        // hdrl = avih(64) + video_strl(124) = 192
        write_u32(m_header, HDRL_LIST_SIZE_OFFSET, 192);
    }

    // LIST movi size = 4 ("movi") + total frame data with padding
    write_u32(m_header, m_movi_list_size_offset, MOVI_DATA_OFFSET + m_data_size);

    // Audio strh: dwLength (sample count) - patched at close
    // dwLength is at offset 32 from the start of the strh data block
    if (m_audio_enabled)
    {
        write_u32(m_header, AUDIO_STRH_DATA_OFFSET + 32, m_audio_sample_count);
    }
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

        // Use "01wb" for audio chunks, "00dc" for video chunks
        if (entry.is_audio)
        {
            write_four_cc(m_index_buffer, offset, "01wb");
        }
        else
        {
            write_four_cc(m_index_buffer, offset, "00dc");
        }
        write_u32(m_index_buffer, offset + 4, 0x10);       // dwFlags = AVIIF_KEYFRAME
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