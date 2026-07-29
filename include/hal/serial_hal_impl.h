#ifndef HAL_SERIAL_HAL_IMPL_H
#define HAL_SERIAL_HAL_IMPL_H

#include "hal/serial_hal.h"

namespace hal
{

/**
 * @class SerialHalImpl
 * @brief Concrete implementation of the Serial HAL interface.
 *
 * Wraps the Arduino HardwareSerial (Serial) object, providing
 * UART communication for logging and serial command transport.
 */
class SerialHalImpl : public SerialHal
{
    public:
        /**
         * @brief Constructs the serial HAL and opens the port.
         * @param baud Communication speed in bits per second.
         */
        SerialHalImpl(uint32_t baud);

        int available() override;
        int read() override;
        size_t write(const uint8_t* data, size_t len) override;
        void flush() override;
};

} // namespace hal

#endif // HAL_SERIAL_HAL_IMPL_H
