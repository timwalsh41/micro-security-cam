#include "hal/serial_log_backend.h"
#include <Arduino.h>

namespace hal
{

SerialLogBackend::SerialLogBackend()
{
    Serial.begin(115200);
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

    // use atomic Serial.write call to target proper output channel
    Serial.write(buf, len + 2);
}

} // namespace hal
