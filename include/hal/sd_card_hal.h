#ifndef HAL_SD_CARD_HAL_H
#define HAL_SD_CARD_HAL_H

#include <cstddef>
#include <cstdint>

/**
 * @file sd_card_hal.h
 * @brief Abstract interface for SD card hardware operations.
 *
 * Defines the contract for SD card initialization and file I/O.
 * Hardware-specific implementations (ESP32) and mocks (native)
 * derive from this interface.
 */

namespace hal
{

/**
 * @brief Result of SD card initialization.
 */
enum class SdInitResult : uint8_t
{
    SUCCESS,            ///< SD card initialized successfully
    HW_INIT_FAILURE,    ///< Hardware/SD library initialization failed
    NO_SD_CARD          ///< No SD card detected
};

/**
 * @brief File open mode for SD card operations.
 */
enum class FileOpenMode : uint8_t
{
    READ,               ///< Open file for reading
    WRITE,              ///< Open file for writing (truncates if exists)
    APPEND              ///< Open file for writing (preserves existing content)
};

/**
 * @class SdCardHal
 * @brief Abstract interface for SD card hardware operations.
 *
 * Provides a mockable abstraction over the SD card library (SD.h).
 */
class SdCardHal
{
    public:
        virtual ~SdCardHal() = default;

        /**
         * @brief Initializes the SD card subsystem.
         *
         * Performs hardware initialization and detects the card type.
         *
         * @return SdInitResult indicating success or the failure reason.
         */
        virtual SdInitResult init() = 0;

        /**
         * @brief Opens a file with the specified mode.
         *
         * Must only be called when the chip-select pin is asserted.
         *
         * @param path The file path to open.
         * @param mode The open mode (READ or WRITE).
         * @return true if the file was opened successfully, false otherwise.
         */
        virtual bool open(const char* path, FileOpenMode mode) = 0;

        /**
         * @brief Writes data to the currently open file.
         *
         * Must only be called when the chip-select pin is asserted.
         * The file must have been opened in WRITE mode.
         *
         * @param data Pointer to the data buffer to write.
         * @param len Number of bytes to write.
         * @return true if all bytes were written successfully, false otherwise.
         */
        virtual bool write(const uint8_t* data, size_t len) = 0;

        /**
         * @brief Reads data from the currently open file.
         *
         * Must only be called when the chip-select pin is asserted.
         * The file must have been opened in READ mode.
         *
         * @param buffer Pointer to the buffer to read into.
         * @param len Maximum number of bytes to read.
         * @return Number of bytes actually read (0 on failure or EOF).
         */
        virtual size_t read(uint8_t* buffer, size_t len) = 0;

        /**
         * @brief Closes the currently open file.
         *
         * Must only be called when the chip-select pin is asserted.
         * Safe to call even if no file is currently open.
         */
        virtual void close() = 0;

        /**
         * @brief Seeks to a byte position within the currently open file.
         *
         * Must only be called when the chip-select pin is asserted.
         * The file must have been opened in WRITE or READ mode.
         *
         * @param pos The byte offset from the beginning of the file.
         * @return true if the seek succeeded, false otherwise.
         */
        virtual bool seek(uint32_t pos) = 0;

        /**
         * @brief Creates a directory on the SD card.
         *
         * Must only be called when the chip-select pin is asserted.
         * If the directory already exists, this is a no-op that returns true.
         *
         * @param path The directory path to create.
         * @return true if the directory was created (or already existed), false otherwise.
         */
        virtual bool create_dir(const char* path) = 0;

        /**
         * @brief Checks whether the SD card has been successfully initialized, e.g. hardware
         *        configured and SD card present.
         *
         * @return true if the SD card is initialized and ready, false otherwise.
         */
        virtual bool is_initialized() const = 0;
};

} // namespace hal

#endif // HAL_SD_CARD_HAL_H
