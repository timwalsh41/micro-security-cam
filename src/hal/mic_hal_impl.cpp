#include "hal/mic_hal_impl.h"

#ifndef UNIT_TESTING

#include <driver/i2s.h>
#include <esp_log.h>
#include <cstring>

namespace hal
{

static constexpr const char* TAG = "MIC_HAL";

MicInitResult MicHalImpl::init(const MicConfig& config)
{
    i2s_config_t i2s_config = {};
    i2s_config.mode = static_cast<i2s_mode_t>(
        I2S_MODE_MASTER | I2S_MODE_RX |
        (config.use_pdm ? I2S_MODE_PDM : 0));
    i2s_config.sample_rate = config.sample_rate;
    i2s_config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    i2s_config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    i2s_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    i2s_config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    i2s_config.dma_buf_count = config.dma_buf_count;
    i2s_config.dma_buf_len = config.dma_buf_len;
    i2s_config.use_apll = false;
    i2s_config.tx_desc_auto_clear = false;

    i2s_pin_config_t pin_config = {};
    pin_config.bck_io_num = I2S_PIN_NO_CHANGE;
    pin_config.ws_io_num = config.ws_pin;
    pin_config.data_out_num = I2S_PIN_NO_CHANGE;
    pin_config.data_in_num = config.sd_pin;
    pin_config.mck_io_num = I2S_PIN_NO_CHANGE;

    esp_err_t err = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, nullptr);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "i2s_driver_install failed: %d", err);
        return MicInitResult::HARDWARE_FAILURE;
    }

    err = i2s_set_pin(I2S_NUM_0, &pin_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "i2s_set_pin failed: %d", err);
        i2s_driver_uninstall(I2S_NUM_0);
        return MicInitResult::HARDWARE_FAILURE;
    }

    i2s_zero_dma_buffer(I2S_NUM_0);
    m_initialized = true;
    return MicInitResult::SUCCESS;
}

void MicHalImpl::start()
{
    if (m_initialized)
    {
        i2s_start(I2S_NUM_0);
    }
}

void MicHalImpl::stop()
{
    if (m_initialized)
    {
        i2s_stop(I2S_NUM_0);
    }
}

void MicHalImpl::deinit()
{
    if (m_initialized)
    {
        i2s_driver_uninstall(I2S_NUM_0);
        m_initialized = false;
    }
}

size_t MicHalImpl::read_samples(int16_t* buffer, size_t count)
{
    if (!m_initialized || buffer == nullptr)
    {
        return 0;
    }

    size_t bytes_to_read = count * sizeof(int16_t);
    size_t bytes_read = 0;

    esp_err_t err = i2s_read(I2S_NUM_0, buffer, bytes_to_read,
                             &bytes_read, portMAX_DELAY);
    if (err != ESP_OK)
    {
        return 0;
    }

    return bytes_read / sizeof(int16_t);
}

bool MicHalImpl::is_initialized() const
{
    return m_initialized;
}

} // namespace hal

#endif // UNIT_TESTING