#ifndef MOCK_SERIAL_HAL_H
#define MOCK_SERIAL_HAL_H

#include "hal/serial_hal.h"
#include <cstring>
#include <vector>

/**
 * @file mock_serial_hal.h
 * @brief Configurable mock SerialHal for native unit tests.
 *
 * Supports injection of simulated serial input and captures
 * all output (for asserting command responses).
 */
class MockSerialHal : public hal::SerialHal
{
    public:
        // ============================================================
        // Configurable return values
        // ============================================================

        /** Number of bytes available to read. */
        int available_result{0};

        // ============================================================
        // Simulated input buffer (inject serial data here)
        // ============================================================

        /** Simulated serial receive buffer. Test code writes data here. */
        std::vector<uint8_t> input_buffer;

        /** Current read position in input_buffer. */
        size_t input_pos{0};

        // ============================================================
        // Captured output (inspect after each operation)
        // ============================================================

        /** All bytes written via write(). */
        std::vector<uint8_t> output_buffer;

        // ============================================================
        // Call tracking
        // ============================================================

        int flush_call_count{0};

        // ============================================================
        // Interface implementation
        // ============================================================

        int available() override
        {
            if (input_pos >= input_buffer.size())
            {
                return 0;
            }
            return static_cast<int>(input_buffer.size() - input_pos);
        }

        int read() override
        {
            if (input_pos >= input_buffer.size())
            {
                return -1;
            }
            return input_buffer[input_pos++];
        }

        size_t write(const uint8_t* data, size_t len) override
        {
            output_buffer.insert(output_buffer.end(), data, data + len);
            return len;
        }

        void flush() override
        {
            flush_call_count++;
        }

        // ============================================================
        // Test helpers
        // ============================================================

        /**
         * @brief Injects a string into the simulated serial receive buffer.
         * @param s Null-terminated string to enqueue.
         */
        void inject_input(const char* s)
        {
            size_t len = strlen(s);
            for (size_t i = 0; i < len; i++)
            {
                input_buffer.push_back(static_cast<uint8_t>(s[i]));
            }
        }

        /**
         * @brief Injects raw bytes into the simulated serial receive buffer.
         * @param data Pointer to the bytes.
         * @param len  Number of bytes.
         */
        void inject_bytes(const uint8_t* data, size_t len)
        {
            input_buffer.insert(input_buffer.end(), data, data + len);
        }

        /**
         * @brief Returns the captured output as a null-terminated string.
         * @note Modifies output_buffer by appending a null terminator.
         */
        const char* output_as_string()
        {
            output_buffer.push_back('\0');
            return reinterpret_cast<const char*>(output_buffer.data());
        }

        /**
         * @brief Clears both input and output buffers.
         */
        void clear_buffers()
        {
            input_buffer.clear();
            output_buffer.clear();
            input_pos = 0;
        }
};

#endif // MOCK_SERIAL_HAL_H
