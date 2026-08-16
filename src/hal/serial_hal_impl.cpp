#include "hal/serial_hal_impl.h"
#include <Arduino.h>

namespace hal
{

SerialHalImpl::SerialHalImpl(uint32_t baud)
{
    Serial.begin(baud);
}

SemaphoreHandle_t SerialHalImpl::ensure_mutex()
{
    // The FreeRTOS heap is not usable when global constructors run, so the
    // mutex cannot be created in the constructor. Create it here on first
    // use, under a critical section so concurrent first writes on both
    // cores cannot race to create two mutexes.
    portENTER_CRITICAL(&m_init_spinlock);
    if (m_write_mutex == nullptr)
    {
        m_write_mutex = xSemaphoreCreateMutex();
    }
    portEXIT_CRITICAL(&m_init_spinlock);

    return m_write_mutex;
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
    SemaphoreHandle_t mutex = ensure_mutex();
    if (mutex == nullptr)
    {
        // Mutex creation failed; fall back to an unsynchronised write so
        // the bytes are still emitted rather than silently dropped. Log the
        // failure once, writing directly to the UART — the logging service
        // would otherwise recurse through this same write() path.
        if (!m_mutex_failure_logged)
        {
            m_mutex_failure_logged = true;
            Serial.printf("[%08lu] [SERHL] [ERROR] Failed to create write "
                          "mutex; serial writes unprotected\r\n",
                          static_cast<unsigned long>(millis()));
        }
        return Serial.write(data, len);
    }

    ScopedMutexLock lock(mutex);
    return Serial.write(data, len);
}

void SerialHalImpl::flush()
{
    Serial.flush();
}

} // namespace hal
