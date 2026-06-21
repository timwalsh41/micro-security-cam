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
 * @class SdCardHal
 * @brief Abstract interface for SD card hardware operations.
 *
 * Provides a mockable abstraction over the SD card library (SD.h).
 * The SdWriteController owns the chip-select (CS) pin timing protocol;
 * therefore file operations (open/write/close) MUST only be called
 * when the CS pin is asserted by the controller.
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
     * @brief Opens a file for writing.
     *
     * Must only be called when the chip-select pin is asserted.
     *
     * @param path The file path to open.
     * @return true if the file was opened successfully, false otherwise.
     */
    virtual bool open(const char *path) = 0;

    /**
     * @brief Writes data to the currently open file.
     *
     * Must only be called when the chip-select pin is asserted.
     *
     * @param data Pointer to the data buffer to write.
     * @param len Number of bytes to write.
     * @return true if all bytes were written successfully, false otherwise.
     */
    virtual bool write(const uint8_t *data, size_t len) = 0;

    /**
     * @brief Closes the currently open file.
     *
     * Must only be called when the chip-select pin is asserted.
     * Safe to call even if no file is currently open.
     */
    virtual void close() = 0;

    /**
     * @brief Checks whether the SD card has been successfully initialized.
     *
     * @return true if the SD card is initialized and ready, false otherwise.
     */
    virtual bool is_initialized() const = 0;
};

} // namespace hal

#endif // HAL_SD_CARD_HAL_H
