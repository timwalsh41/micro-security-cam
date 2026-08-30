#include "stream_frame_channel.h"

#include <algorithm>

// ============================================================
// Producer side
// ============================================================

bool StreamFrameChannel::push_part(const uint8_t* data, size_t len)
{
    if (data == nullptr || len == 0)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_closed)
    {
        return false;
    }
    if (m_offset < m_part.size())
    {
        // The consumer is still draining the previous part: drop this
        // one rather than queueing (live preview wants the freshest
        // frame, and a bounded mailbox keeps producer latency flat).
        return false;
    }
    m_part.assign(data, data + len);
    m_offset = 0;
    return true;
}

void StreamFrameChannel::close()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_closed = true;
}

// ============================================================
// Consumer side
// ============================================================

size_t StreamFrameChannel::pull(uint8_t* out, size_t max_len)
{
    if (out == nullptr || max_len == 0)
    {
        return 0;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_offset >= m_part.size())
    {
        return 0;
    }

    const size_t remaining = m_part.size() - m_offset;
    const size_t to_copy = std::min(max_len, remaining);
    std::copy(m_part.begin() + static_cast<long>(m_offset),
              m_part.begin() + static_cast<long>(m_offset + to_copy),
              out);
    m_offset += to_copy;

    if (m_offset >= m_part.size())
    {
        // Fully consumed: release the part memory (capacity retained).
        m_part.clear();
        m_offset = 0;
    }
    return to_copy;
}

bool StreamFrameChannel::has_pending_part() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_offset < m_part.size();
}

bool StreamFrameChannel::is_closed() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_closed;
}
