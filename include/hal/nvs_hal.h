#ifndef HAL_NVS_HAL_H
#define HAL_NVS_HAL_H

#include <cstddef>
#include <cstdint>

/**
 * @file nvs_hal.h
 * @brief Abstract interface for non-volatile key-value storage.
 *
 * Provides a mockable abstraction over the platform's non-volatile
 * storage (NVS). Supports opaque byte blobs addressed by a key within
 * a namespace. Concrete implementations (ESP32) and mocks (native)
 * derive from this interface.
 */

namespace hal
{

/**
 * @class NvsHal
 * @brief Abstract interface for non-volatile key-value storage.
 *
 * Provides blob read/write operations within a named namespace.
 * Implementations manage the storage lifecycle; consumers only
 * read and write blobs.
 */
class NvsHal
{
    public:
        virtual ~NvsHal() = default;

        /**
         * @brief Opens (or creates) the named storage namespace.
         *
         * Must be called before any read/write operation.
         * Repeat calls are permitted and re-open the namespace.
         *
         * @param namespace_name Name of the namespace (platform-defined
         *                       length limits apply).
         * @return true if the namespace was opened or created.
         */
        virtual bool begin(const char* namespace_name) = 0;

        /**
         * @brief Reads a blob from the namespace.
         *
         * @param key         Key of the blob to read.
         * @param buffer      Buffer to read data into.
         * @param buffer_size Maximum number of bytes the buffer can hold.
         * @return Number of bytes copied into the buffer (0 if the key
         *         does not exist or the read failed).
         */
        virtual size_t read_blob(const char* key,
                                 uint8_t* buffer, size_t buffer_size) = 0;

        /**
         * @brief Writes a blob to the namespace.
         *
         * @param key  Key to write under (created or overwritten).
         * @param data Pointer to the data buffer.
         * @param len  Number of bytes to write.
         * @return true if all bytes were committed.
         */
        virtual bool write_blob(const char* key,
                                const uint8_t* data, size_t len) = 0;

        /**
         * @brief Erases the blob stored under a key.
         *
         * Erasing a key that does not exist is reported as success.
         *
         * @param key Key of the blob to erase.
         * @return true if the key was erased (or did not exist).
         */
        virtual bool erase_key(const char* key) = 0;
};

} // namespace hal

#endif // HAL_NVS_HAL_H