#ifndef CAMERA_CONTROLLER_H
#define CAMERA_CONTROLLER_H

#include <cstdint>
#include "hal/camera_hal.h"
#include "logging_service.h"

// Note: framesize_t is defined in esp_camera.h (ESP32 only).
// For native tests, a compatible enum is provided in the mock layer.

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
};

/**
 * @brief Resolved camera parameters after PSRAM guard application.
 *
 * Contains the validated HAL configuration and actual pixel dimensions
 * that downstream consumers (VideoController, AviWriter) should use.
 */
struct ResolvedCameraParams
{
    hal::CameraConfig hal_config;  ///< Fully resolved HAL config
    uint16_t width;                ///< Validated pixel width
    uint16_t height;               ///< Validated pixel height
    uint8_t framesize;             ///< ESP32 framesize_t enum value
};

/**
 * @brief Translates user-facing CameraImageConfig to validated HAL config.
 *
 * Maps resolution string to framesize_t + dimensions, applies PSRAM
 * guards, and returns validated parameters. The authoritative
 * resolution mapping — replaces VideoController::map_resolution().
 *
 * @param user_config User-facing camera settings from INI.
 * @param has_psram   Whether PSRAM is available on the board.
 * @param log         LoggingService for guard warnings.
 * @return Resolved camera parameters.
 */
ResolvedCameraParams build_camera_config(
    const CameraImageConfig& user_config,
    bool has_psram,
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
 * Configuration is determined by PSRAM availability, with defaults
 * suitable for the Seeed XIAO ESP32S3. Custom configuration can be
 * provided via the overloaded constructor.
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
         * @brief Initializes the camera with configuration suitable for this board.
         *
         * Queries PSRAM availability via the HAL and adjusts frame size,
         * quality, and buffer count accordingly. The configuration values
         * come from the CameraConfig provided at construction time.
         *
         * @return hal::CameraInitResult indicating success or failure.
         */
        hal::CameraInitResult init();

        /**
         * @brief Initializes the camera with pre-resolved parameters.
         *
         * @param params Resolved camera parameters from build_camera_config().
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

    private:
        hal::CameraHal& m_hal;
        hal::CameraConfig m_config;
        bool m_initialized{false};
        bool m_frame_acquired{false};
        hal::CameraFrame m_current_frame{nullptr, 0};
};

#endif // CAMERA_CONTROLLER_H
