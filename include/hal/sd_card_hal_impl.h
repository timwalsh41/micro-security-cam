#ifndef HAL_SD_CARD_HAL_IMPL_H
#define HAL_SD_CARD_HAL_IMPL_H

#include "hal/sd_card_hal.h"
#include "SD.h"

/**
 * @file sd_card_hal_impl.h
 * @brief Concrete SdCardHal using the Arduino SD library.
 *
 * Wraps SD.begin(), SD.open(), File::write(), and File::close().
 */

namespace hal
{

/**
 * @class SdCardHalImpl
 * @brief Concrete SdCardHal using the Arduino SD library.
 *
 * Wraps SD.begin(), SD.open(), File::write(), and File::close().
 */
class SdCardHalImpl : public SdCardHal
{
public:
    /**
     * @brief Constructs SdCardHalImpl with the given chip-select pin.
     *
     * @param cs_pin The GPIO pin used for SD card chip-select.
     */
    explicit SdCardHalImpl(uint8_t cs_pin);

    SdInitResult init() override;

    bool open(const char *path, hal::FileOpenMode mode) override;
    bool write(const uint8_t *data, size_t len) override;
    size_t read(uint8_t *buffer, size_t len) override;
    void close() override;
    bool create_dir(const char *path) override;
    bool is_initialized() const override;

private:
    uint8_t m_cs_pin;
    bool m_initialized{false};
    bool m_file_open{false};
    File m_file;
};

} // namespace hal

#endif // HAL_SD_CARD_HAL_IMPL_H
