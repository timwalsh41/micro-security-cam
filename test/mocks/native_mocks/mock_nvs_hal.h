#ifndef MOCK_NVS_HAL_H
#define MOCK_NVS_HAL_H

#include "hal/nvs_hal.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

/**
 * @file mock_nvs_hal.h
 * @brief Configurable mock NvsHal for native unit tests.
 *
 * Simulates a non-volatile key-value store in memory with configurable
 * return values and call tracking so consumers of NvsHal can be tested
 * deterministically on the host machine.
 */
class MockNvsHal : public hal::NvsHal
{
    public:
        // ============================================================
        // Configurable return values (set before calling the method)
        // ============================================================

        /** Value returned by begin(). */
        bool begin_result{true};

        /** Value returned by write_blob(). */
        bool write_result{true};

        // ============================================================
        // In-memory blob store simulating NVS contents
        // ============================================================

        std::map<std::string, std::vector<uint8_t>> blobs;

        // ============================================================
        // Call tracking (inspect after calling the method)
        // ============================================================

        /** Number of times begin() has been called. */
        int begin_call_count{0};

        /** Number of times read_blob() has been called. */
        int read_call_count{0};

        /** Number of times write_blob() has been called. */
        int write_call_count{0};

        /** Number of times erase_key() has been called. */
        int erase_call_count{0};

        /** The key argument from the most recent erase_key() call. */
        std::string last_erase_key;

        /** The key argument from the most recent write_blob() call. */
        std::string last_write_key;

        /** The data argument from the most recent write_blob() call. */
        std::vector<uint8_t> last_write_data;

        /** The key argument from the most recent read_blob() call. */
        std::string last_read_key;

        // ============================================================
        // Interface implementation
        // ============================================================

        bool begin(const char* namespace_name) override
        {
            begin_call_count++;
            last_namespace = namespace_name != nullptr ? namespace_name : "";
            return begin_result;
        }

        size_t read_blob(const char* key,
                         uint8_t* buffer, size_t buffer_size) override
        {
            read_call_count++;
            last_read_key = key != nullptr ? key : "";

            if (!begin_result || key == nullptr || buffer == nullptr)
            {
                return 0;
            }

            auto it = blobs.find(key);
            if (it == blobs.end())
            {
                return 0;
            }

            size_t copy_len = it->second.size() < buffer_size
                                  ? it->second.size() : buffer_size;
            std::copy(it->second.begin(), it->second.begin() + copy_len,
                      buffer);
            return copy_len;
        }

        bool write_blob(const char* key,
                        const uint8_t* data, size_t len) override
        {
            write_call_count++;
            last_write_key = key != nullptr ? key : "";
            last_write_data.assign(data, data + len);

            if (!begin_result || !write_result || key == nullptr)
            {
                return false;
            }

            blobs[key] = std::vector<uint8_t>(data, data + len);
            return true;
        }

        bool erase_key(const char* key) override
        {
            erase_call_count++;
            last_erase_key = key != nullptr ? key : "";

            if (!begin_result || key == nullptr)
            {
                return false;
            }

            blobs.erase(std::string(key));
            return true;
        }

        /**
         * @brief Seeds the in-memory store with a blob.
         *
         * Convenience for tests that need pre-existing NVS content
         * without simulating a full write cycle.
         *
         * @param key    Key to seed.
         * @param content Initial blob content.
         */
        void seed_blob(const char* key, const std::vector<uint8_t>& content)
        {
            blobs[key] = content;
        }

        /** Namespace name from the most recent begin() call. */
        std::string last_namespace;
};

#endif // MOCK_NVS_HAL_H