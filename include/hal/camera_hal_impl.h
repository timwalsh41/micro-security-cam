#ifndef HAL_CAMERA_HAL_IMPL_H
#define HAL_CAMERA_HAL_IMPL_H

#include "hal/camera_hal.h"
#include "esp_camera.h"

/**
 * @file camera_hal_impl.h
 * @brief Concrete CameraHal using the ESP32 camera library.
 *
 * Wraps esp_camera_init(), esp_camera_fb_get(), and esp_camera_fb_return().
 * Pin mappings are selected at compile time via camera_pins.h defines.
 */

namespace hal
{

/**
 * @class CameraHalImpl
 * @brief Concrete CameraHal using the ESP32 camera library.
 *
 * Wraps the esp_camera API for initialization, frame capture,
 * and frame buffer release.
 */
class CameraHalImpl : public CameraHal
{
    public:
        /**
         * @brief Constructs CameraHalImpl with default pin configuration.
         *
         * Pin mappings are determined by the CAMERA_MODEL_* define
         * selected at compile time.
         */
        CameraHalImpl() = default;

        CameraInitResult init(const hal::CameraConfig& config) override;
        void deinit() override;
        CameraFrame capture() override;
        void release() override;
        bool is_initialized() const override;
        bool has_psram() const override;

    private:
        bool m_initialized{false};
        bool m_frame_acquired{false};
        camera_fb_t* m_fb{nullptr};  ///< Retained frame buffer pointer for release()
};

} // namespace hal

#endif // HAL_CAMERA_HAL_IMPL_H
