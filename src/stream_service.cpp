#include "stream_service.h"

#include <cstdio>
#include <cstring>

// C++11 requires an out-of-class definition for constexpr static data
// members that are odr-used; C++17 makes the in-class initializer
// sufficient.
#if __cplusplus < 201703L
constexpr uint8_t StreamService::PLACEHOLDER_JPEG[];
#endif

StreamService::StreamService(LoggingService& log)
    : m_log(log)
{
}

void StreamService::init(uint8_t max_streams)
{
    if (max_streams > MAX_STREAMS)
    {
        max_streams = MAX_STREAMS;
    }
    m_max_streams = max_streams;
    m_log.info(SUBSYSTEM, "Stream service initialized (max %u streams)",
               static_cast<unsigned int>(m_max_streams));
}

int StreamService::start_stream(std::shared_ptr<StreamFrameChannel> channel)
{
    if (!channel)
    {
        m_log.warn(SUBSYSTEM, "start_stream rejected: null channel");
        return -1;
    }

    // Seed the stream with a small, well-formed placeholder JPEG part
    // so the channel holds bytes before the slot bind. The transport
    // response attached to the viewer's request then finds data ready
    // at its first fill, which keeps the response's head-plus-content
    // write inside its send-buffer reservation; with an empty channel
    // the head is deferred into a later content write and overflows
    // the socket buffer, silently truncating the first chunk. The
    // placeholder is framed like any delivered frame, so strict
    // multipart parsers accept the stream opening (a standalone empty
    // opening boundary is what they reject) and render it until the
    // first real frame replaces it.
    uint8_t part[PART_HEADER_CAPACITY + PLACEHOLDER_JPEG_LEN + 2];
    size_t header_len =
        format_part_header(reinterpret_cast<char*>(part),
                           PART_HEADER_CAPACITY, PLACEHOLDER_JPEG_LEN);
    if (header_len > 0)
    {
        std::memcpy(part + header_len, PLACEHOLDER_JPEG,
                    PLACEHOLDER_JPEG_LEN);
        part[header_len + PLACEHOLDER_JPEG_LEN] = PART_FOOTER[0];
        part[header_len + PLACEHOLDER_JPEG_LEN + 1] = PART_FOOTER[1];

        // Push outside the slot mutex: the channel is not bound to any
        // slot yet, so no producer can race this push.
        size_t part_len = header_len + PLACEHOLDER_JPEG_LEN + 2;
        channel->push_part(part, part_len);
    }
    else
    {
        m_log.warn(SUBSYSTEM, "Placeholder part header overflow");
    }

    std::lock_guard<std::mutex> lock(m_slot_mutex);
    // Find a free slot within the configured maximum.
    for (uint8_t i = 0; i < m_max_streams; i++)
    {
        if (!m_slots[i].active)
        {
            // Bind the channel into the slot. The transport response
            // attached to the viewer's request shares ownership and
            // pulls the bytes under framework flow control.
            m_slots[i].channel = channel;
            m_slots[i].active = true;

            m_log.info(SUBSYSTEM, "Stream started on slot %u",
                       static_cast<unsigned int>(i));
            return static_cast<int>(i);
        }
    }

    m_log.warn(SUBSYSTEM, "start_stream rejected: no free slot (max %u)",
               static_cast<unsigned int>(m_max_streams));
    return -1;
}

void StreamService::stop_stream(uint8_t slot)
{
    std::shared_ptr<StreamFrameChannel> channel;
    {
        std::lock_guard<std::mutex> lock(m_slot_mutex);
        if (slot >= MAX_STREAMS || !m_slots[slot].active)
        {
            return;
        }

        // Reclaim the slot under the mutex so it is immediately
        // reusable and cannot be freed concurrently by a disconnect.
        channel = m_slots[slot].channel;
        m_slots[slot].channel.reset();
        m_slots[slot].active = false;
    }

    // End the stream outside the slot mutex: the closing token is
    // pushed best effort (dropped when a frame part is still pending)
    // and the channel is marked closed. The transport response drains
    // the pending bytes, completes the multipart stream, and its end
    // callback later finds no matching slot (idempotent).
    if (channel)
    {
        channel->push_part(reinterpret_cast<const uint8_t*>(CLOSING_BOUNDARY),
                           strlen(CLOSING_BOUNDARY));
        channel->close();
    }

    m_log.info(SUBSYSTEM, "Stream stopped on slot %u",
               static_cast<unsigned int>(slot));
}

void StreamService::stop_all_streams()
{
    // Snapshot the active slots under the mutex so the scan cannot race
    // concurrent slot mutations; stop_stream() revalidates each slot
    // under the mutex before reclaiming it.
    uint8_t active[MAX_STREAMS];
    uint8_t count = 0;
    {
        std::lock_guard<std::mutex> lock(m_slot_mutex);
        for (uint8_t i = 0; i < m_max_streams; i++)
        {
            if (m_slots[i].active)
            {
                active[count++] = i;
            }
        }
    }
    for (uint8_t i = 0; i < count; i++)
    {
        stop_stream(active[i]);
    }
}

void StreamService::on_stream_ended(StreamFrameChannel* channel)
{
    if (channel == nullptr)
    {
        return;
    }

    // Runs in the transport's task context (response destruction). The
    // mutex makes the slot reclamation mutually exclusive against
    // start_stream() binding, stop_stream() reclamation, and the frame
    // producer iterating the slots.
    std::lock_guard<std::mutex> lock(m_slot_mutex);
    for (uint8_t i = 0; i < m_max_streams; i++)
    {
        if (m_slots[i].active && m_slots[i].channel.get() == channel)
        {
            // The transport destroyed the response (client disconnect
            // or completed stream); reclaim the slot. The response held
            // its own channel reference and is already gone.
            m_slots[i].channel.reset();
            m_slots[i].active = false;

            m_log.info(SUBSYSTEM, "Stream slot %u freed on disconnect",
                       static_cast<unsigned int>(i));
            return;
        }
    }
}

void StreamService::deliver_frame(const uint8_t* data, size_t len)
{
    if (data == nullptr || len == 0)
    {
        return;
    }

    // Build the per-frame part header with the actual Content-Length.
    char header[PART_HEADER_CAPACITY];
    size_t header_len = format_part_header(header, sizeof(header), len);
    if (header_len == 0)
    {
        m_log.warn(SUBSYSTEM, "Frame header overflow");
        return;
    }

    // Snapshot the active channels under the slot mutex: slots can be
    // bound or reclaimed concurrently (viewer connect/disconnect), but
    // the snapshot keeps delivery consistent with the registry state at
    // this instant and keeps the channel pushes outside the mutex. The
    // shared_ptr copies keep snapshot channels alive even if their slot
    // is reclaimed mid-push.
    std::vector<std::shared_ptr<StreamFrameChannel>> channels;
    {
        std::lock_guard<std::mutex> lock(m_slot_mutex);
        for (uint8_t i = 0; i < m_max_streams; i++)
        {
            if (m_slots[i].active && m_slots[i].channel)
            {
                channels.push_back(m_slots[i].channel);
            }
        }
    }

    // Assemble the complete part (header + JPEG + footer) once per
    // frame: every viewer receives identical bytes and push_part()
    // copies anyway.
    m_part_scratch.clear();
    m_part_scratch.reserve(header_len + len + strlen(PART_FOOTER));
    m_part_scratch.insert(m_part_scratch.end(),
                          reinterpret_cast<uint8_t*>(header),
                          reinterpret_cast<uint8_t*>(header) + header_len);
    m_part_scratch.insert(m_part_scratch.end(), data, data + len);
    m_part_scratch.insert(m_part_scratch.end(),
                          reinterpret_cast<const uint8_t*>(PART_FOOTER),
                          reinterpret_cast<const uint8_t*>(PART_FOOTER) +
                              strlen(PART_FOOTER));

    for (auto& channel : channels)
    {
        // Push the part into the viewer's channel. A viewer still
        // draining its previous part drops this frame — the transport
        // pulls at the client's speed, so the part can never be
        // truncated mid-frame.
        channel->push_part(m_part_scratch.data(), m_part_scratch.size());
    }
}

uint8_t StreamService::active_slot_count() const
{
    std::lock_guard<std::mutex> lock(m_slot_mutex);
    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_STREAMS; i++)
    {
        if (m_slots[i].active)
        {
            count++;
        }
    }
    return count;
}

size_t StreamService::format_part_header(char* buf, size_t cap,
                                         size_t payload_len)
{
    int n = snprintf(buf, cap, "%s%u\r\n\r\n",
                     PART_HEADER, static_cast<unsigned int>(payload_len));
    if (n < 0 || static_cast<size_t>(n) >= cap)
    {
        return 0;
    }
    return static_cast<size_t>(n);
}
