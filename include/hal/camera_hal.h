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
 * @brief Abstract hardware capability limits for camera operation.
 *
 * Returned by CameraHal::get_capabilities(). Describes what the
 * hardware can support regardless of the underlying reason
 * (PSRAM availability, DRAM limits, board revision, etc.).
 */
/**
 * @brief Default capabilities for systems without PSRAM.
 */
struct CameraCapabilities
{
    uint8_t max_framesize;          ///< Framesize enum (e.g. FRAMESIZE_SVGA = 3)
    uint8_t min_jpeg_quality;       ///< Minimum JPEG quality (0-63, lower = better)
    uint8_t max_fb_count;           ///< Maximum frame buffers
    uint8_t recommended_grab_mode;  ///< 0=WHEN_EMPTY, 1=LATEST
    uint16_t max_width;             ///< Max pixel width
    uint16_t max_height;            ///< Max pixel height
};

/** PSRAM-present capabilities: UXGA, quality 10, 2 bufs, LATEST grab. */
constexpr CameraCapabilities PSRAM_CAPABILITIES{5, 10, 2, 1, 1600, 1200};

/** No-PSRAM capabilities: SVGA, quality 12, 1 buf, WHEN_EMPTY grab. */
constexpr CameraCapabilities NO_PSRAM_CAPABILITIES{3, 12, 1, 0, 800, 600};

/**
 * @brief Configuration parameters for camera initialization.
 *
 * Single-valued — expresses the caller's intent without PSRAM-specific
 * fields. The HAL applies hardware-specific adjustments internally;
 * the CameraController clamps these values against CameraCapabilities
 * before passing them to init().
 */
struct CameraConfig
{
    /** JPEG quality (0-63, lower = higher quality). */
    uint8_t jpeg_quality{12};

    /** Frame size enum value (e.g. FRAMESIZE_SVGA = 3). */
    uint8_t frame_size{3};

    /** Number of frame buffers. */
    uint8_t fb_count{1};

    /**
     * @brief Camera grab mode override.
     *
     * Controls how the camera driver delivers frames:
     *   - 0 (CAMERA_GRAB_WHEN_EMPTY): Return frame when available, may return stale frame.
     *   - 1 (CAMERA_GRAB_LATEST): Discard buffered frames, return only the freshest frame.
     *   - 0xFF (default): Auto-detect — uses the HAL's recommended mode.
     *
     * Video recording should use CAMERA_GRAB_LATEST (1) to prevent stale frame delivery
     * during SD write stalls.
     */
    uint8_t grab_mode{0xFF};

    /**
     * @brief Image rotation in degrees.
     *
     * Supported values are 0 (no rotation) and 180 (applied via sensor
     * hmirror + vflip registers after esp_camera_init()). Any other
     * value is rejected before reaching the HAL.
     */
    uint16_t rotation{0};
};

/**
 * @class CameraHal
 * @brief Abstract interface for camera hardware operations.
 *
 * Provides a mockable abstraction over the ESP32 camera library
 * (esp_camera.h). Exposes hardware capability limits via
 * get_capabilities() so the service layer can clamp configuration
 * without knowing about PSRAM or other board-specific details.
 *
 * The CameraController owns the capture lifecycle; therefore
 * capture() and release() MUST be called in pairs.
 */
class CameraHal
{
    public:
        virtual ~CameraHal() = default;

        /**
         * @brief Initializes the camera hardware.
         *
         * Performs hardware initialization, configures pins, and
         * allocates frame buffers. The config values are assumed to
         * have been clamped against get_capabilities() by the caller.
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
         * @brief Queries the hardware capability limits for this board.
         *
         * Returns abstract capabilities (max resolution, min quality,
         * max buffers, recommended grab mode) derived from board
         * characteristics such as PSRAM availability. The CameraController
         * uses these to clamp user configuration before initialization.
         *
         * @return CameraCapabilities struct with hardware limits.
         */
        virtual CameraCapabilities get_capabilities() const = 0;
};

} // namespace hal

#endif // HAL_CAMERA_HAL_H
