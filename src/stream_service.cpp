#include "stream_service.h"

#include <cstdio>
#include <cstring>
#include <utility>

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

int StreamService::start_stream(std::unique_ptr<hal::StreamWriter> writer)
{
    if (!writer)
    {
        m_log.warn(SUBSYSTEM, "start_stream rejected: null writer");
        return -1;
    }

    // Find a free slot within the configured maximum.
    for (uint8_t i = 0; i < m_max_streams; i++)
    {
        if (!m_slots[i].active)
        {
            // Bind the disconnect callback so the slot is freed when the
            // client drops. The callback runs in the transport's context
            // and only frees this slot via free_slot().
            writer->on_disconnect([this, i]()
            {
                free_slot(i);
            });

            // Send the HTTP response headers + first boundary marker.
            writer->write(reinterpret_cast<const uint8_t*>(PREAMBLE),
                          strlen(PREAMBLE));

            // Transfer ownership into the slot.
            m_slots[i].writer = std::move(writer);
            m_slots[i].active = true;

            m_log.info(SUBSYSTEM, "Stream started on slot %u",
                       static_cast<unsigned int>(i));
            return static_cast<int>(i);
        }
    }

    // No free slot: the unique_ptr deletes the writer on return.
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

    // Take ownership out of the slot and deactivate it first so the
    // synchronous disconnect callback fired by close() below sees an
    // inactive slot and does not re-enter free_slot() on this writer.
    std::unique_ptr<hal::StreamWriter> writer = std::move(m_slots[slot].writer);
    m_slots[slot].active = false;

    // Terminate the multipart stream and close the connection.
    static constexpr const char* CLOSING = "--" STREAM_BOUNDARY_TOKEN "--\r\n";
    writer->write(reinterpret_cast<const uint8_t*>(CLOSING), strlen(CLOSING));
    writer->close();

    m_log.info(SUBSYSTEM, "Stream stopped on slot %u",
               static_cast<unsigned int>(slot));

    // writer is deleted here when the unique_ptr goes out of scope.
}

void StreamService::stop_all_streams()
{
    for (uint8_t i = 0; i < m_max_streams; i++)
    {
        if (m_slots[i].active)
        {
            stop_stream(i);
        }
    }
}

void StreamService::free_slot(uint8_t slot)
{
    if (slot >= MAX_STREAMS || !m_slots[slot].active)
    {
        return;
    }

    // The connection has already dropped, so close() must not be called
    // (it could re-fire the disconnect callback). Reclaim the writer.
    m_slots[slot].writer.reset();
    m_slots[slot].active = false;

    m_log.info(SUBSYSTEM, "Stream slot %u freed on disconnect",
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

        hal::StreamWriter* writer = m_slots[i].writer.get();

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
