#include "sd_write_buffer.h"
#include <cstring>

// ============================================================
// Construction
// ============================================================

SdWriteBuffer::SdWriteBuffer(PeripheralManager& peripheral)
    : m_peripheral(peripheral)
{
}

// ============================================================
// Lifecycle
// ============================================================

bool SdWriteBuffer::open(const char* path)
{
    if (!path)
    {
        return false;
    }

    // Discard any previously buffered data
    m_high_point = 0;
    m_path = path;
    m_open = true;
    return true;
}

bool SdWriteBuffer::buffer_frame(const uint8_t* avi_chunk, size_t chunk_len)
{
    if (!m_open || !avi_chunk || chunk_len == 0)
    {
        return false;
    }

    // If the chunk exceeds RAMSIZE, flush the current buffer first
    // to ensure we have space, then write directly if buffer is empty
    if (chunk_len > RAMSIZE)
    {
        if (m_high_point > 0)
        {
            if (!flush_buffer())
            {
                return false;
            }
        }

        // Write the oversized chunk directly to SD (bypassing the buffer)
        SdWriteResult result = m_peripheral.write_file(m_path, avi_chunk, chunk_len, 5000);
        return (result == SdWriteResult::SUCCESS);
    }

    // If the chunk won't fit in the remaining buffer space, flush first
    if (m_high_point + chunk_len > RAMSIZE)
    {
        if (!flush_buffer())
        {
            return false;
        }
    }

    // Copy the chunk into the buffer
    memcpy(&m_buffer[m_high_point], avi_chunk, chunk_len);
    m_high_point += chunk_len;

    // Auto-flush when buffer reaches RAMSIZE
    if (m_high_point >= RAMSIZE)
    {
        if (!flush_buffer())
        {
            return false;
        }
    }

    return true;
}

bool SdWriteBuffer::flush()
{
    if (!m_open)
    {
        return false;
    }

    if (m_high_point == 0)
    {
        return true;    // Nothing to flush
    }

    return flush_buffer();
}

void SdWriteBuffer::reset()
{
    m_high_point = 0;
    m_open = false;
    m_path = nullptr;
}

// ============================================================
// State queries
// ============================================================

bool SdWriteBuffer::is_open() const
{
    return m_open;
}

size_t SdWriteBuffer::buffered_bytes() const
{
    return m_high_point;
}

// ============================================================
// Private methods
// ============================================================

bool SdWriteBuffer::flush_buffer()
{
    if (m_high_point == 0)
    {
        return true;
    }

    SdWriteResult result = m_peripheral.write_file(m_path, m_buffer, m_high_point, 5000);
    m_high_point = 0;

    return (result == SdWriteResult::SUCCESS);
}
