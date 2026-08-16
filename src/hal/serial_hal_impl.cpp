#include "hal/serial_hal_impl.h"
#include <Arduino.h>

namespace hal
{

SerialHalImpl::SerialHalImpl(uint32_t baud)
    : m_write_mutex(xSemaphoreCreateMutex())
{
    Serial.begin(baud);
}

int SerialHalImpl::available()
{
    return Serial.available();
}

int SerialHalImpl::read()
{
    return Serial.read();
}

size_t SerialHalImpl::write(const uint8_t* data, size_t len)
{
    // Serialise writes so a full log line (including its trailing newline)
    // is emitted atomically — a preempting task cannot interleave bytes
    // into another task's line.
    ScopedMutexLock lock(m_write_mutex);

    // Serial.write() can return a partial write when the UART TX ring
    // buffer is full. Retry the unwritten remainder so no bytes are dropped.
    size_t written = 0;
    for (size_t attempt = 0; attempt < MAX_WRITE_RETRIES && written < len; attempt++)
    {
        size_t n = Serial.write(data + written, len - written);
        if (n == 0)
        {
            break;
        }
        written += n;
    }
    return written;
}

void SerialHalImpl::flush()
{
    Serial.flush();
}

} // namespace hal
