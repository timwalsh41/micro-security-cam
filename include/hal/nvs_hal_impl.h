#ifndef HAL_NVS_HAL_IMPL_H
#define HAL_NVS_HAL_IMPL_H

#include "hal/nvs_hal.h"
#include <Preferences.h>

/**
 * @file nvs_hal_impl.h
 * @brief Concrete NvsHal using the ESP32 Preferences (NVS) library.
 *
 * Wraps Preferences::begin(), getBytes(), and putBytes().
 */

namespace hal
{

/**
 * @class NvsHalImpl
 * @brief Concrete NvsHal using the ESP32 Preferences (NVS) library.
 */
class NvsHalImpl : public NvsHal
{
    public:
        NvsHalImpl() = default;

        bool begin(const char* namespace_name) override;

        size_t read_blob(const char* key,
                         uint8_t* buffer, size_t buffer_size) override;

        bool write_blob(const char* key,
                        const uint8_t* data, size_t len) override;

        bool erase_key(const char* key) override;

    private:
        Preferences m_prefs;
        bool m_open{false};
};

} // namespace hal

#endif // HAL_NVS_HAL_IMPL_H