#ifndef MOCK_SD_CARD_HAL_H
#define MOCK_SD_CARD_HAL_H

#include "hal/sd_card_hal.h"
#include <cstddef>
#include <cstdint>

/**
 * @file mock_sd_card_hal.h
 * @brief Configurable mock SdCardHal for native unit tests.
 *
 * Provides full control over return values and call tracking
 * so that SdWriteController (and other consumers) can be tested
 * deterministically on the host machine.
 */
class MockSdCardHal : public hal::SdCardHal
{
    public:
        // ============================================================
        // Configurable return values (set before calling the method)
        // ============================================================

        /** Value returned by init(). */
        hal::SdInitResult init_result{hal::SdInitResult::SUCCESS};

        /** Value returned by open(). */
        bool open_result{true};

        /** Value returned by write(). */
        bool write_result{true};

        /** Value returned by is_initialized(). */
        bool initialized_state{true};

        // ============================================================
        // Call tracking (inspect after calling the method)
        // ============================================================

        /** Number of times init() has been called. */
        int init_call_count{0};

        /** Number of times open() has been called. */
        int open_call_count{0};

        /** Number of times write() has been called. */
        int write_call_count{0};

        /** Number of times close() has been called. */
        int close_call_count{0};

        /** The path argument from the most recent open() call. */
        const char *last_open_path{nullptr};

        /** The data pointer from the most recent write() call. */
        const uint8_t *last_write_data{nullptr};

        /** The length from the most recent write() call. */
        size_t last_write_len{0};

        // ============================================================
        // Interface implementation
        // ============================================================

        hal::SdInitResult init() override
        {
            init_call_count++;
            return init_result;
        }

        bool open(const char *path) override
        {
            open_call_count++;
            last_open_path = path;
            return open_result;
        }

        bool write(const uint8_t *data, size_t len) override
        {
            write_call_count++;
            last_write_data = data;
            last_write_len = len;
            return write_result;
        }

        void close() override
        {
            close_call_count++;
        }

        bool is_initialized() const override
        {
            return initialized_state;
        }
};

#endif // MOCK_SD_CARD_HAL_H
