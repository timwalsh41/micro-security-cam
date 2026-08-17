#include "stream_service.h"

#include <cstdio>
#include <cstring>

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

int StreamService::start_stream(hal::StreamWriter* writer)
{
    if (writer == nullptr)
    {
        m_log.warn(SUBSYSTEM, "start_stream rejected: null writer");
        return -1;
    }

    // Find a free slot within the configured maximum.
    for (uint8_t i = 0; i < m_max_streams; i++)
    {
        if (!m_slots[i].active)
        {
            m_slots[i].writer = writer;
            m_slots[i].active = true;

            // Send the HTTP response headers + first boundary marker.
            writer->write(reinterpret_cast<const uint8_t*>(PREAMBLE),
                          strlen(PREAMBLE));

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
    if (slot >= MAX_STREAMS || !m_slots[slot].active)
    {
        return;
    }

    // Terminate the multipart stream and close the connection.
    static constexpr const char* CLOSING = "--" STREAM_BOUNDARY_TOKEN "--\r\n";
    m_slots[slot].writer->write(
        reinterpret_cast<const uint8_t*>(CLOSING), strlen(CLOSING));
    m_slots[slot].writer->close();
    m_slots[slot].writer = nullptr;
    m_slots[slot].active = false;

    m_log.info(SUBSYSTEM, "Stream stopped on slot %u",
               static_cast<unsigned int>(slot));
}

void StreamService::deliver_frame(const uint8_t* data, size_t len)
{
    if (data == nullptr || len == 0)
    {
        return;
    }

    for (uint8_t i = 0; i < m_max_streams; i++)
    {
        if (!m_slots[i].active)
        {
            continue;
        }

        hal::StreamWriter* writer = m_slots[i].writer;

        // Build the per-frame part header with the actual Content-Length.
        // PART_HEADER is ~64 bytes; add room for the decimal length
        // (up to 10 digits) plus the trailing CRLFCRLF.
        char header[96];
        int n = snprintf(header, sizeof(header), "%s%u\r\n\r\n",
                         PART_HEADER, static_cast<unsigned int>(len));
        if (n < 0 || static_cast<size_t>(n) >= sizeof(header))
        {
            m_log.warn(SUBSYSTEM, "Frame header overflow on slot %u", i);
            continue;
        }

        writer->write(reinterpret_cast<const uint8_t*>(header),
                      static_cast<size_t>(n));
        writer->write(data, len);
        writer->write(reinterpret_cast<const uint8_t*>(PART_FOOTER),
                      strlen(PART_FOOTER));
    }
}

uint8_t StreamService::active_slot_count() const
{
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
