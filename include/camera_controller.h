#ifndef CAMERA_CONTROLLER_H
#define CAMERA_CONTROLLER_H

#include <cstdint>
#include "hal/camera_hal.h"
#include "logging_service.h"

/**
 * @brief User-facing camera image configuration.
 *
 * Represents the user's intent (resolution + quality) from INI config
 * without PSRAM-specific details. Consumed by build_camera_config().
 */
struct CameraImageConfig
{
    const char* resolution{"VGA"};  ///< Resolution string from INI
    uint8_t jpeg_quality{10};       ///< JPEG quality (0-63, lower = better)
    uint8_t fb_count{1};            ///< Requested frame buffer count
    uint16_t rotation{0};          ///< Image rotation in degrees (0 or 180)
};

/**
 * @brief Camera parameters after resolution mapping (pre-clamping).
 *
 * Contains the desired HAL configuration and pixel dimensions
 * that downstream consumers (VideoController, AviWriter) should use.
 * The final clamped values after init() are available via
 * CameraController::get_params().
 *
 * The pixel dimensions are kept alongside hal_config.frame_size because
 * AVI container headers and clip filenames need concrete width/height,
 * and those consumers must not depend on the resolution-mapping table
 * to derive them from the enum.
 */
struct ResolvedCameraParams
{
    hal::CameraConfig hal_config;  ///< Desired HAL config (pre-clamping)
    uint16_t width;                ///< Pixel width from resolution mapping
    uint16_t height;               ///< Pixel height from resolution mapping
};

/**
 * @brief Translates user-facing CameraImageConfig to desired camera parameters.
 *
 * Maps resolution string to hal::FrameSize enum + pixel dimensions.
 * This is a pure mapping function — hardware capability clamping
 * (PSRAM guards, etc.) is handled by CameraController::init()
 * via HAL-provided CameraCapabilities.
 *
 * @param user_config User-facing camera settings from INI.
 * @param log         LoggingService for warnings.
 * @return Resolved camera parameters (desired, not yet clamped).
 */
ResolvedCameraParams build_camera_config(
    const CameraImageConfig& user_config,
    LoggingService& log);

/**
 * @file camera_controller.h
 * @brief Camera lifecycle and capture management.
 *
 * CameraController manages camera initialization with board-appropriate
 * configuration, and provides a controlled frame capture/release lifecycle.
 * It owns the configuration decisions (PSRAM-based tuning) while delegating
 * hardware operations to an injected CameraHal.
 */

/**
 * @brief Result of a camera capture operation.
 */
enum class CameraCaptureResult : uint8_t
{
    SUCCESS,            ///< Frame captured successfully
    NOT_INITIALIZED,    ///< Camera has not been initialized
    CAPTURE_FAILED      ///< Hardware capture failed
};

/**
 * @class CameraController
 * @brief Manages camera initialization, configuration, and frame capture.
 *
 * Provides a controlled lifecycle:
 *   init() → capture_frame() → get_frame() → release_frame()
 *
 * Configuration values are clamped against hardware capabilities
 * (queried from CameraHal::get_capabilities()) during init() so
 * the caller does not need to know about PSRAM or board limits.
 * The actual applied parameters are available via get_params()
 * after a successful init().
 */
class CameraController
{
    public:
        /**
         * @brief Constructs a CameraController with default configuration.
         * @param hal Reference to a CameraHal used for hardware I/O.
         */
        explicit CameraController(hal::CameraHal& hal);

        /**
         * @brief Constructs a CameraController with custom configuration.
         * @param hal    Reference to a CameraHal used for hardware I/O.
         * @param config Custom camera configuration parameters.
         */
        CameraController(hal::CameraHal& hal, const hal::CameraConfig& config);

        /**
         * @brief Initializes the camera, clamping config against HAL capabilities.
         *
         * Queries CameraHal::get_capabilities() and clamps frame size,
         * quality, and buffer count to within hardware limits before
         * initializing. The configuration values come from the
         * CameraConfig provided at construction time.
         *
         * @return hal::CameraInitResult indicating success or failure.
         */
        hal::CameraInitResult init();

        /**
         * @brief Initializes the camera, clamping params against HAL capabilities.
         *
         * @param params Desired parameters from build_camera_config().
         * @return hal::CameraInitResult indicating success or failure.
         */
        hal::CameraInitResult init(const ResolvedCameraParams& params);

        /**
         * @brief Deinitializes the camera hardware and releases resources.
         *
         * Safe to call even if the camera is not initialized.
         */
        void deinit();

        /**
         * @brief Captures a photo frame.
         *
         * After a successful capture, the frame data is available via
         * get_frame() until release_frame() is called.
         *
         * @return CameraCaptureResult indicating success or failure.
         */
        CameraCaptureResult capture_frame();

        /**
         * @brief Returns the most recently captured frame.
         *
         * Valid until release_frame() is called. If no frame has been
         * captured, returns a CameraFrame with data == nullptr.
         *
         * @return The captured CameraFrame.
         */
        hal::CameraFrame get_frame() const;

        /**
         * @brief Releases the frame buffer back to the camera hardware.
         *
         * Must be called after the frame data is no longer needed.
         * Safe to call even when no frame is currently acquired.
         */
        void release_frame();

        /**
         * @brief Whether the camera was successfully initialized.
         * @return true if initialized, false otherwise.
         */
        bool is_initialized() const;

        /**
         * @brief Returns the parameters that were actually applied during init().
         *
         * Contains the clamped HAL config and resolved pixel dimensions.
         * Only valid after a successful init() call. Before init(),
         * returns default (SVGA) values.
         *
         * @return Const reference to the actual applied parameters.
         */
        const ResolvedCameraParams& get_params() const;

    private:
        hal::CameraHal& m_hal;
        hal::CameraConfig m_config;
        bool m_initialized{false};
        bool m_frame_acquired{false};
        hal::CameraFrame m_current_frame{nullptr, 0};
        ResolvedCameraParams m_actual_params{hal::CameraConfig{}, 800, 600};
};

#endif // CAMERA_CONTROLLER_H
