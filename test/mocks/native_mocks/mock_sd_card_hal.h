#ifndef MOCK_SD_CARD_HAL_H
#define MOCK_SD_CARD_HAL_H

#include "hal/sd_card_hal.h"
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <thread>
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
         * @brief Seed content used to initialize unopened files in the store.
         *
         * Backward-compatible knob: when open() is called for a path that is
         * not yet in the file store, this content is copied into the store
         * entry, so existing tests that set file_content directly keep
         * working. read() then returns bytes from the store at the current
         * open position.
         */
        std::vector<uint8_t> file_content;

        /**
         * @brief In-memory file store simulating SD card contents.
         *
         * open() applies real open-mode semantics: WRITE truncates,
         * APPEND preserves and positions at end-of-file, READ_WRITE
         * preserves and positions at 0, READ positions at 0.
         */
        std::map<std::string, std::vector<uint8_t>> files;

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

        /** The path argument from the most recent create_dir() call.
         *  Copied, since callers may pass a stack-local buffer. */
        char last_create_dir_path[128]{};

        /** The position argument from the most recent seek() call. */
        uint32_t last_seek_pos{0};

        /** Number of times exists() has been called. */
        int exists_call_count{0};

        /** The path argument from the most recent exists() call. */
        const char* last_exists_path{nullptr};

        // ============================================================
        // Deterministic park gate (write)
        // ============================================================

        /**
         * @brief Inert-by-default gate to park a caller inside write().
         *
         * Same contract as the create_dir gate below: parks the Nth
         * write() call (1-based, 0 keeps the gate inert) until
         * gate_write_release is set by another thread. One-shot.
         */
        std::atomic<int> gate_write_at_count{0};

        /** Number of write() calls observed (1-based). */
        std::atomic<int> gate_write_hits{0};

        /** Set true by another thread to release the parked caller. */
        std::atomic<bool> gate_write_release{false};

        // ============================================================
        // Deterministic park gate (create_dir)
        // ============================================================

        /**
         * @brief Inert-by-default gate to park a caller inside create_dir().
         *
         * Lets tests deterministically park a task at the Nth create_dir()
         * call (e.g. mid clip rotation, inside a now-locked critical
         * section) while other threads issue concurrent lifecycle
         * commands. Because a parked caller may hold a lock the main
         * thread needs, the gate MUST be released by a watchdog: a thread
         * other than the one blocked on the parked caller's critical
         * section sets gate_create_dir_release to true.
         *
         * Usage: set gate_create_dir_at_count to N (1-based, absolute
         * call count since fixture creation; 0 keeps the gate inert) and
         * leave gate_create_dir_release false; a watchdog releases it.
         * The gate is one-shot: it parks only the Nth call.
         */
        std::atomic<int> gate_create_dir_at_count{0};

        /** Number of create_dir() calls observed (1-based). */
        std::atomic<int> gate_create_dir_hits{0};

        /** Set true by the watchdog to release the parked caller. */
        std::atomic<bool> gate_create_dir_release{false};

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

            if (!open_result)
            {
                m_open_path.clear();
                m_file_open_state = false;
                return false;
            }

            m_open_path = path;
            m_file_open_state = true;
            std::vector<uint8_t>& content = files[m_open_path];

            switch (mode)
            {
                case hal::FileOpenMode::WRITE:
                    // WRITE truncates existing content
                    content.clear();
                    m_open_pos = 0;
                    break;

                case hal::FileOpenMode::APPEND:
                    // Preserve content; position at end for appending
                    seed_if_first_open(content);
                    m_open_pos = content.size();
                    break;

                case hal::FileOpenMode::READ_WRITE:
                    // Preserve content; position at start for in-place writes
                    seed_if_first_open(content);
                    m_open_pos = 0;
                    break;

                case hal::FileOpenMode::READ:
                    // Preserve content; position at start for reading
                    seed_if_first_open(content);
                    m_open_pos = 0;
                    break;
            }
            return true;
        }

        bool write(const uint8_t* data, size_t len) override
        {
            write_call_count++;
            last_write_data = data;
            last_write_len = len;

            // Park gate: spin at the Nth call until released.
            if (gate_write_at_count.load(std::memory_order_relaxed) > 0)
            {
                const int hit = gate_write_hits.fetch_add(1,
                                               std::memory_order_acq_rel) + 1;
                if (hit == gate_write_at_count.load(
                               std::memory_order_relaxed))
                {
                    while (!gate_write_release.load(
                               std::memory_order_acquire))
                    {
                        std::this_thread::yield();
                    }
                }
            }

            if (!write_result || !m_file_open_state)
            {
                return false;
            }

            std::vector<uint8_t>& content = files[m_open_path];
            if (m_open_pos + len > content.size())
            {
                content.resize(m_open_pos + len);
            }
            std::copy(data, data + len, content.begin() + m_open_pos);
            m_open_pos += len;
            return true;
        }

        size_t read(uint8_t* buffer, size_t len) override
        {
            read_call_count++;
            last_read_buffer = buffer;
            last_read_len = len;

            if (!m_file_open_state)
            {
                return 0;
            }

            std::vector<uint8_t>& content = files[m_open_path];
            if (m_open_pos >= content.size())
            {
                return 0;
            }

            size_t available = content.size() - m_open_pos;
            size_t to_copy = (len < available) ? len : available;

            std::copy(content.begin() + m_open_pos,
                      content.begin() + m_open_pos + to_copy,
                      buffer);
            m_open_pos += to_copy;

            return to_copy;
        }

        void close() override
        {
            close_call_count++;
            m_open_path.clear();
            m_file_open_state = false;
        }

        bool seek(uint32_t pos) override
        {
            seek_call_count++;
            last_seek_pos = pos;
            if (!seek_result || !m_file_open_state)
            {
                return false;
            }
            m_open_pos = pos;
            return true;
        }

        bool create_dir(const char* path) override
        {
            create_dir_call_count++;
            if (path != nullptr)
            {
                strncpy(last_create_dir_path, path,
                        sizeof(last_create_dir_path) - 1);
            }

            // Park gate: spin at the Nth call until the watchdog releases.
            if (gate_create_dir_at_count.load(std::memory_order_relaxed) > 0)
            {
                const int hit = gate_create_dir_hits.fetch_add(1,
                                                   std::memory_order_acq_rel) + 1;
                if (hit == gate_create_dir_at_count.load(
                               std::memory_order_relaxed))
                {
                    while (!gate_create_dir_release.load(
                               std::memory_order_acquire))
                    {
                        std::this_thread::yield();
                    }
                }
            }

            return create_dir_result;
        }

        bool is_initialized() const override
        {
            return initialized_state;
        }

        bool exists(const char* path) override
        {
            exists_call_count++;
            last_exists_path = path;
            return files.find(path) != files.end();
        }

        /**
         * @brief Seeds the in-memory store with a file's content.
         *
         * Convenience for tests that need a pre-existing file on the
         * "card" without simulating a full write cycle.
         *
         * @param path    File path to create.
         * @param content Initial file content.
         */
        void seed_file(const char* path, const std::vector<uint8_t>& content)
        {
            files[path] = content;
        }

    private:
        /**
         * @brief Initializes a store entry from the file_content knob.
         *
         * Backward-compatible behavior: a file opened for READ, APPEND, or
         * READ_WRITE that has not been written through the mock yet is
         * seeded from the file_content member so existing tests that set
         * file_content directly keep working.
         *
         * @param content The store entry to seed (if still empty).
         */
        void seed_if_first_open(std::vector<uint8_t>& content)
        {
            if (content.empty() && !file_content.empty())
            {
                content = file_content;
            }
        }

        std::string m_open_path;
        bool m_file_open_state{false};
        size_t m_open_pos{0};
};

#endif // MOCK_SD_CARD_HAL_H
