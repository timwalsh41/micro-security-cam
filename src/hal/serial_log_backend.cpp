#include "hal/serial_log_backend.h"
#include <cstring>

namespace hal
{

SerialLogBackend::SerialLogBackend(SerialHal& serial)
: m_serial(serial)
{
}

void SerialLogBackend::write(const char* line)
{
    // Compose line + \r\n into a single buffer for atomic UART write.
    // This prevents interleaving with ESP-IDF UART0 writes from other cores.
    size_t len = strnlen(line, MAX_LOG_LINE_LENGTH);
    char buf[MAX_LOG_LINE_LENGTH + 2];     // line + \r\n
    memcpy(buf, line, len);
    buf[len]     = '\r';
    buf[len + 1] = '\n';

    // Use atomic write call via the injected SerialHal.
    m_serial.write(reinterpret_cast<const uint8_t*>(buf), len + 2);
}

} // namespace hal
