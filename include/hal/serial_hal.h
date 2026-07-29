#ifndef HAL_SERIAL_HAL_H
#define HAL_SERIAL_HAL_H

#include <cstdint>
#include <cstddef>

/**
 * @file serial_hal.h
 * @brief Abstract interface for UART/serial hardware operations.
 *
 * Provides a mockable abstraction over a serial interface. Concrete
 * platform implementation and native test mock derive from this
 * interface.
 */

namespace hal
{

/**
 * @class SerialHal
 * @brief Abstract interface for serial port operations.
 *
 * Wraps the hardware serial interface behind a test-friendly
 * abstraction.
 *
 * The serial port is initialised in the constructor of the concrete
 * implementation — lifecycle management belongs to the composition root,
 * not to consumers of this interface.
 */
class SerialHal
{
    public:
        virtual ~SerialHal() = default;

        /**
         * @brief Returns the number of bytes available to read.
         * @return Number of bytes in the receive buffer.
         */
        virtual int available() = 0;

        /**
         * @brief Reads a single byte from the serial port.
         * @return The byte read, or -1 if no data is available.
         */
        virtual int read() = 0;

        /**
         * @brief Writes a buffer of bytes to the serial port.
         * @param data Pointer to the data buffer.
         * @param len  Number of bytes to write.
         * @return Number of bytes written.
         */
        virtual size_t write(const uint8_t* data, size_t len) = 0;

        /**
         * @brief Waits for the transmit buffer to empty.
         */
        virtual void flush() = 0;
};

} // namespace hal

#endif // HAL_SERIAL_HAL_H
