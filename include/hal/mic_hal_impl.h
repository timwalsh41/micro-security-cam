#ifndef HAL_MIC_HAL_IMPL_H
#define HAL_MIC_HAL_IMPL_H

#include "hal/mic_hal.h"

#include <driver/i2s.h>

namespace hal
{

/**
 * @class MicHalImpl
 * @brief ESP32 I2S PDM RX implementation of the MicHal interface.
 */
class MicHalImpl : public MicHal
{
    public:
        MicInitResult init(const MicConfig& config) override;
        void start() override;
        void stop() override;
        void deinit() override;
        size_t read_samples(int16_t* buffer, size_t count) override;
        bool is_initialized() const override;

    private:
        bool m_initialized{false};
};

} // namespace hal

#endif // HAL_MIC_HAL_IMPL_H