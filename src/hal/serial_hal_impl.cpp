#include "hal/serial_hal_impl.h"
#include <Arduino.h>

namespace hal
{

SerialHalImpl::SerialHalImpl(uint32_t baud)
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
    return Serial.write(data, len);
}

void SerialHalImpl::flush()
{
    Serial.flush();
}

} // namespace hal
