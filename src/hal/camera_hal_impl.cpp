#include "hal/camera_hal_impl.h"
#include <Arduino.h>
#include "camera_pins.h"
#include "esp_camera.h"

namespace hal
{

CameraInitResult CameraHalImpl::init(const CameraConfig& cam_config)
{
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.frame_size = static_cast<framesize_t>(cam_config.frame_size);
    config.pixel_format = PIXFORMAT_JPEG;
    config.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
    config.jpeg_quality = cam_config.jpeg_quality;
    config.fb_count = cam_config.fb_count;

    // Resolve grab mode: sentinel 0xFF uses the hardware-recommended default
    if (cam_config.grab_mode == 0xFF)
    {
        config.grab_mode = static_cast<camera_grab_mode_t>(
            get_capabilities().recommended_grab_mode);
    }
    else
    {
        config.grab_mode = static_cast<camera_grab_mode_t>(cam_config.grab_mode);
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK)
    {
        return CameraInitResult::HARDWARE_FAILURE;
    }

    m_initialized = true;
    return CameraInitResult::SUCCESS;
}

void CameraHalImpl::deinit()
{
    if (m_initialized)
    {
        esp_camera_deinit();
        m_initialized = false;
        m_frame_acquired = false;
    }
}

CameraFrame CameraHalImpl::capture()
{
    if (!m_initialized)
    {
        return {nullptr, 0};
    }

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb)
    {
        return {nullptr, 0};
    }

    m_fb = fb;
    m_frame_acquired = true;
    return {fb->buf, fb->len};
}

void CameraHalImpl::release()
{
    if (m_frame_acquired && m_fb)
    {
        esp_camera_fb_return(m_fb);
        m_fb = nullptr;
        m_frame_acquired = false;
    }
}

bool CameraHalImpl::is_initialized() const
{
    return m_initialized;
}

CameraCapabilities CameraHalImpl::get_capabilities() const
{
    if (psramFound())
    {
        // PSRAM present: UXGA, quality 10, 2 buffers, LATEST grab
        return {5, 10, 2, 1, 1600, 1200};
    }
    else
    {
        // No PSRAM: SVGA, quality 12, 1 buffer, WHEN_EMPTY grab
        return {3, 12, 1, 0, 800, 600};
    }
}

} // namespace hal
