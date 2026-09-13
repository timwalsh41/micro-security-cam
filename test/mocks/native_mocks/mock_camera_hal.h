#ifndef MOCK_CAMERA_HAL_H
#define MOCK_CAMERA_HAL_H

#include "hal/camera_hal.h"
#include <cstddef>
#include <cstdint>

/**
 * @file mock_camera_hal.h
 * @brief Configurable mock CameraHal for native unit tests.
 *
 * Provides full control over return values and call tracking
 * so that CameraController (and other consumers) can be tested
 * deterministically on the host machine.
 */
class MockCameraHal : public hal::CameraHal
{
    public:
        // ============================================================
        // Configurable return values (set before calling the method)
        // ============================================================

        /** Value returned by init(). */
        hal::CameraInitResult init_result{hal::CameraInitResult::SUCCESS};

        /** Value returned by capture(). */
        hal::CameraFrame capture_result{nullptr, 0};

        /** Value returned by is_initialized(). */
        bool initialized_state{true};

        /** Value returned by get_capabilities(). */
        hal::CameraCapabilities capabilities{3, 12, 1, 0, 800, 600};

        // ============================================================
        // Call tracking (inspect after calling the method)
        // ============================================================

        /** Number of times init() has been called. */
        int init_call_count{0};

        /** Number of times deinit() has been called. */
        int deinit_call_count{0};

        /** Number of times capture() has been called. */
        int capture_call_count{0};

        /** Number of times release() has been called. */
        int release_call_count{0};

        /** The CameraConfig passed to the most recent init() call. */
        hal::CameraConfig last_init_config{};

        /** The rotation value passed to the most recent init() call. */
        uint16_t last_requested_rotation{0};

        // ============================================================
        // Test data
        // ============================================================

        /** Pre-allocated buffer that capture_result can point to. */
        uint8_t frame_buffer[1024]{};

        // ============================================================
        // Interface implementation
        // ============================================================

        hal::CameraInitResult init(const hal::CameraConfig& config) override
        {
            init_call_count++;
            last_init_config = config;
            last_requested_rotation = config.rotation;
            return init_result;
        }

        void deinit() override
        {
            deinit_call_count++;
        }

        hal::CameraFrame capture() override
        {
            capture_call_count++;
            return capture_result;
        }

        void release() override
        {
            release_call_count++;
        }

        bool is_initialized() const override
        {
            return initialized_state;
        }

        hal::CameraCapabilities get_capabilities() const override
        {
            return capabilities;
        }
};

#endif // MOCK_CAMERA_HAL_H
