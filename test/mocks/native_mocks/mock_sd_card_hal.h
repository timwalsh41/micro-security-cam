#ifndef MOCK_SD_CARD_HAL_H
#define MOCK_SD_CARD_HAL_H

#include "hal/sd_card_hal.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

/**
 * @file mock_sd_card_hal.h
 * @brief Configurable mock SdCardHal for native unit tests.
 *
 * Provides full control over return values and call tracking
 * so that SdController (and other consumers) can be tested
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

        /** Value returned by create_dir(). */
        bool create_dir_result{true};

        /** Value returned by seek(). */
        bool seek_result{true};

        /** Value returned by is_initialized(). */
        bool initialized_state{true};

        /**
         * @brief Content returned by read().
         *
         * When read() is called, it copies min(file_content.size(), len)
         * bytes from this vector into the caller's buffer.
         */
        std::vector<uint8_t> file_content;

        // ============================================================
        // Call tracking (inspect after calling the method)
        // ============================================================

        /** Number of times init() has been called. */
        int init_call_count{0};

        /** Number of times open() has been called. */
        int open_call_count{0};

        /** Number of times write() has been called. */
        int write_call_count{0};

        /** Number of times read() has been called. */
        int read_call_count{0};

        /** Number of times close() has been called. */
        int close_call_count{0};

        /** Number of times create_dir() has been called. */
        int create_dir_call_count{0};

        /** Number of times seek() has been called. */
        int seek_call_count{0};

        /** The path argument from the most recent open() call. */
        const char* last_open_path{nullptr};

        /** The mode argument from the most recent open() call. */
        hal::FileOpenMode last_open_mode{hal::FileOpenMode::WRITE};

        /** The data pointer from the most recent write() call. */
        const uint8_t* last_write_data{nullptr};

        /** The length from the most recent write() call. */
        size_t last_write_len{0};

        /** The buffer pointer from the most recent read() call. */
        uint8_t* last_read_buffer{nullptr};

        /** The length from the most recent read() call. */
        size_t last_read_len{0};

        /** The path argument from the most recent create_dir() call. */
        const char* last_create_dir_path{nullptr};

        /** The position argument from the most recent seek() call. */
        uint32_t last_seek_pos{0};

        // ============================================================
        // Interface implementation
        // ============================================================

        hal::SdInitResult init() override
        {
            init_call_count++;
            return init_result;
        }

        bool open(const char* path, hal::FileOpenMode mode) override
        {
            open_call_count++;
            last_open_path = path;
            last_open_mode = mode;
            return open_result;
        }

        bool write(const uint8_t* data, size_t len) override
        {
            write_call_count++;
            last_write_data = data;
            last_write_len = len;
            return write_result;
        }

        size_t read(uint8_t* buffer, size_t len) override
        {
            read_call_count++;
            last_read_buffer = buffer;
            last_read_len = len;

            size_t available = file_content.size();
            size_t to_copy = (len < available) ? len : available;

            if (to_copy > 0)
            {
                std::copy(file_content.begin(),
                          file_content.begin() + to_copy,
                          buffer);
            }

            return to_copy;
        }

        void close() override
        {
            close_call_count++;
        }

        bool seek(uint32_t pos) override
        {
            seek_call_count++;
            last_seek_pos = pos;
            return seek_result;
        }

        bool create_dir(const char* path) override
        {
            create_dir_call_count++;
            last_create_dir_path = path;
            return create_dir_result;
        }

        bool is_initialized() const override
        {
            return initialized_state;
        }
};

#endif // MOCK_SD_CARD_HAL_H
