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
    Serial.println(line);
}

} // namespace hal
