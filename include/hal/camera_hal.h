#ifndef HAL_CAMERA_HAL_H
#define HAL_CAMERA_HAL_H

#include <cstddef>
#include <cstdint>

/**
 * @file camera_hal.h
 * @brief Abstract interface for camera hardware operations.
 *
 * Defines the contract for camera initialization, frame capture,
 * and resource management. Hardware-specific implementations
 * and mocks (native) derive from this interface.
 */

namespace hal
{

/**
 * @brief Result of camera initialization.
 */
enum class CameraInitResult : uint8_t
{
    SUCCESS,            ///< Camera initialized successfully
    HARDWARE_FAILURE    ///< Camera hardware/esp_camera_init() failed
};

/**
 * @brief Represents a captured frame buffer.
 *
 * The data pointer is valid until release() is called on the
 * CameraHal that produced it. The caller must not free the buffer.
 */
struct CameraFrame
{
    const uint8_t* data;    ///< Pointer to frame buffer (valid until release())
    size_t len;             ///< Length in bytes
};

/**
 * @brief Configuration parameters for camera initialization.
 *
 * Provides sensible defaults for the Seeed XIAO ESP32S3 board.
 * Consumers can override specific fields as needed.
 */
struct CameraConfig
{
    /** JPEG quality (0-63, lower = higher quality) when PSRAM is absent. */
    uint8_t jpeg_quality_no_psram{12};

    /** JPEG quality (0-63, lower = higher quality) when PSRAM is present. */
    uint8_t jpeg_quality_with_psram{10};

    /** Frame size enum value when PSRAM is not available (FRAMESIZE_SVGA = 9). */
    uint8_t frame_size_no_psram{9};

    /** Number of frame buffers when PSRAM is present. */
    uint8_t fb_count_with_psram{2};

    /** Number of frame buffers when PSRAM is absent. */
    uint8_t fb_count_no_psram{1};

    /**
     * @brief Camera grab mode override.
     *
     * Controls how the camera driver delivers frames:
     *   - 0 (CAMERA_GRAB_WHEN_EMPTY): Return frame when available, may return stale frame.
     *   - 1 (CAMERA_GRAB_LATEST): Discard buffered frames, return only the freshest frame.
     *   - 0xFF (default): Auto-detect based on PSRAM availability. When PSRAM is present,
     *     uses CAMERA_GRAB_LATEST; otherwise uses CAMERA_GRAB_WHEN_EMPTY.
     *
     * Video recording should use CAMERA_GRAB_LATEST (1) to prevent stale frame delivery
     * during SD write stalls.
     */
    uint8_t grab_mode{0xFF};
};

/**
 * @class CameraHal
 * @brief Abstract interface for camera hardware operations.
 *
 * Provides a mockable abstraction over the ESP32 camera library
 * (esp_camera.h). The CameraController owns the capture lifecycle;
 * therefore capture() and release() MUST be called in pairs.
 */
class CameraHal
{
    public:
        virtual ~CameraHal() = default;

        /**
         * @brief Initializes the camera hardware.
         *
         * Performs hardware initialization, configures pins, and
         * allocates frame buffers. Uses the provided configuration
         * to set JPEG quality, frame size, and buffer count based
         * on PSRAM availability.
         *
         * @param config Camera configuration parameters.
         * @return CameraInitResult indicating success or failure.
         */
        virtual CameraInitResult init(const CameraConfig& config) = 0;

        /**
         * @brief Deinitializes the camera hardware and releases resources.
         */
        virtual void deinit() = 0;

        /**
         * @brief Captures a frame from the camera.
         *
         * Acquires a frame buffer from the camera hardware. The returned
         * CameraFrame is valid until release() is called.
         *
         * @return CameraFrame with data and length. If capture fails,
         *         data will be nullptr and len will be 0.
         */
        virtual CameraFrame capture() = 0;

        /**
         * @brief Releases the most recently captured frame buffer.
         *
         * Safe to call even if no frame is currently acquired.
         */
        virtual void release() = 0;

        /**
         * @brief Checks whether the camera has been successfully initialized.
         *
         * @return true if the camera is initialized and ready, false otherwise.
         */
        virtual bool is_initialized() const = 0;

        /**
         * @brief Checks whether PSRAM is available on the board.
         *
         * Used by the CameraController to select appropriate camera
         * configuration parameters (frame size, quality, buffer count).
         *
         * @return true if PSRAM is present, false otherwise.
         */
        virtual bool has_psram() const = 0;
};

} // namespace hal

#endif // HAL_CAMERA_HAL_H
