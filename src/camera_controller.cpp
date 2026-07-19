#include "camera_controller.h"
#include <cstring>
#include <strings.h>   // strcasecmp (POSIX)

// ============================================================
// Resolution mapping (build_camera_config)
// ============================================================

namespace
{

/**
 * @brief Resolution entry mapping string names to framesize values and pixel dimensions.
 */
struct ResolutionEntry
{
    const char* name;   ///< Resolution name (case-insensitive match)
    uint8_t framesize;  ///< ESP32 framesize_t enum value
    uint16_t width;     ///< Pixel width
    uint16_t height;    ///< Pixel height
};

/**
 * @brief Table of supported resolution mappings.
 *
 * Entries are ordered by framesize enum value for consistency.
 * The table is terminated by a sentinel entry with nullptr name.
 */
const ResolutionEntry RESOLUTION_TABLE[] =
{
    {"QQVGA",  0,  160,  120},  // FRAMESIZE_QQVGA
    {"QVGA",   1,  320,  240},  // FRAMESIZE_QVGA
    {"VGA",    2,  640,  480},  // FRAMESIZE_VGA
    {"SVGA",   3,  800,  600},  // FRAMESIZE_SVGA
    {"XGA",    4, 1024,  768},  // FRAMESIZE_XGA
    {"HD",     6, 1280,  720},  // FRAMESIZE_HD
    {"UXGA",   5, 1600, 1200},  // FRAMESIZE_UXGA
    {nullptr,   0,    0,    0},  // sentinel
};

/**
 * @brief Maximum framesize value supported without PSRAM (FRAMESIZE_SVGA = 3).
 */
const uint8_t MAX_FRAMESIZE_NO_PSRAM = 3;

/**
 * @brief Minimum JPEG quality (best allowed) without PSRAM.
 */
const uint8_t MIN_QUALITY_NO_PSRAM = 12;

} // anonymous namespace

// ============================================================
// build_camera_config
// ============================================================

ResolvedCameraParams build_camera_config(
    const CameraImageConfig& user_config,
    bool has_psram,
    LoggingService& log)
{
    // Default: VGA
    uint8_t framesize = 2;  // FRAMESIZE_VGA
    uint16_t width = 640;
    uint16_t height = 480;
    bool found = false;

    for (const ResolutionEntry* entry = RESOLUTION_TABLE; entry->name != nullptr; ++entry)
    {
        if (strcasecmp(user_config.resolution, entry->name) == 0)
        {
            framesize = entry->framesize;
            width = entry->width;
            height = entry->height;
            found = true;
            break;
        }
    }

    if (!found)
    {
        log.warn("Camera",
            "Unknown resolution '%s', defaulting to VGA",
            user_config.resolution);
    }

    uint8_t jpeg_quality = user_config.jpeg_quality;

    // PSRAM guards
    if (!has_psram)
    {
        if (framesize > MAX_FRAMESIZE_NO_PSRAM)
        {
            log.warn("Camera",
                "No PSRAM: capping resolution from %u to SVGA",
                static_cast<unsigned>(framesize));
            framesize = MAX_FRAMESIZE_NO_PSRAM;  // FRAMESIZE_SVGA
            width = 800;
            height = 600;
        }

        if (jpeg_quality < MIN_QUALITY_NO_PSRAM)
        {
            log.warn("Camera",
                "No PSRAM: relaxing JPEG quality from %u to %u",
                static_cast<unsigned>(jpeg_quality),
                static_cast<unsigned>(MIN_QUALITY_NO_PSRAM));
            jpeg_quality = MIN_QUALITY_NO_PSRAM;
        }
    }

    // Build HAL config
    hal::CameraConfig hal_config;
    hal_config.frame_size_with_psram = framesize;
    hal_config.jpeg_quality_with_psram = jpeg_quality;
    hal_config.frame_size_no_psram = (framesize <= MAX_FRAMESIZE_NO_PSRAM)
        ? framesize : MAX_FRAMESIZE_NO_PSRAM;
    hal_config.jpeg_quality_no_psram = (jpeg_quality >= MIN_QUALITY_NO_PSRAM)
        ? jpeg_quality : MIN_QUALITY_NO_PSRAM;

    ResolvedCameraParams params;
    params.hal_config = hal_config;
    params.width = width;
    params.height = height;
    params.framesize = framesize;

    return params;
}

// ============================================================
// Construction
// ============================================================

CameraController::CameraController(hal::CameraHal& hal)
    : m_hal(hal)
{
}

CameraController::CameraController(hal::CameraHal& hal,
                                   const hal::CameraConfig& config)
    : m_hal(hal)
    , m_config(config)
{
}

// ============================================================
// Public interface
// ============================================================

hal::CameraInitResult CameraController::init()
{
    hal::CameraInitResult result = m_hal.init(m_config);
    m_initialized = (result == hal::CameraInitResult::SUCCESS);
    return result;
}

hal::CameraInitResult CameraController::init(const ResolvedCameraParams& params)
{
    m_config = params.hal_config;
    hal::CameraInitResult result = m_hal.init(m_config);
    m_initialized = (result == hal::CameraInitResult::SUCCESS);
    return result;
}

void CameraController::deinit()
{
    if (m_frame_acquired)
    {
        m_hal.release();
        m_frame_acquired = false;
        m_current_frame = {nullptr, 0};
    }
    m_hal.deinit();
    m_initialized = false;
}

CameraCaptureResult CameraController::capture_frame()
{
    if (!m_initialized)
    {
        return CameraCaptureResult::NOT_INITIALIZED;
    }

    if (m_frame_acquired)
    {
        // Release previous frame before capturing a new one
        m_hal.release();
        m_frame_acquired = false;
    }

    m_current_frame = m_hal.capture();
    if (!m_current_frame.data)
    {
        return CameraCaptureResult::CAPTURE_FAILED;
    }

    m_frame_acquired = true;
    return CameraCaptureResult::SUCCESS;
}

hal::CameraFrame CameraController::get_frame() const
{
    if (!m_frame_acquired)
    {
        return {nullptr, 0};
    }
    return m_current_frame;
}

void CameraController::release_frame()
{
    if (m_frame_acquired)
    {
        m_hal.release();
        m_frame_acquired = false;
        m_current_frame = {nullptr, 0};
    }
}

bool CameraController::is_initialized() const
{
    return m_initialized;
}
