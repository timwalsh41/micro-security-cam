#include "camera_controller.h"
#include <cstring>
#include <strings.h>   // strcasecmp (POSIX)

// ============================================================
// Resolution mapping (build_camera_config)
// ============================================================

namespace
{

/**
 * @brief Resolution entry mapping string names to HAL frame sizes and
 *        pixel dimensions.
 */
struct ResolutionEntry
{
    const char* name;          ///< Resolution name (case-insensitive match)
    hal::FrameSize framesize;  ///< HAL frame size selection
    uint16_t width;            ///< Pixel width
    uint16_t height;           ///< Pixel height
};

/**
 * @brief Table of supported resolution mappings.
 *
 * Entries are ordered by frame size (ascending pixel dimensions) for
 * consistency. The table is terminated by a sentinel entry with
 * nullptr name.
 */
const ResolutionEntry RESOLUTION_TABLE[] =
{
    {"QQVGA",  hal::FrameSize::QQVGA,  160,  120},
    {"QVGA",   hal::FrameSize::QVGA,   320,  240},
    {"VGA",    hal::FrameSize::VGA,    640,  480},
    {"SVGA",   hal::FrameSize::SVGA,   800,  600},
    {"XGA",    hal::FrameSize::XGA,   1024,  768},
    {"HD",     hal::FrameSize::HD,    1280,  720},
    {"UXGA",   hal::FrameSize::UXGA,  1600, 1200},
    {nullptr,  hal::FrameSize::VGA,     0,    0},  // sentinel
};

/**
 * @brief Resolves a HAL frame size to pixel dimensions.
 *
 * @param framesize Frame size to look up.
 * @param width     [out] Pixel width.
 * @param height    [out] Pixel height.
 * @return true if the frame size was found in the resolution table.
 */
bool resolve_dimensions(hal::FrameSize framesize, uint16_t& width, uint16_t& height)
{
    for (const ResolutionEntry* entry = RESOLUTION_TABLE; entry->name != nullptr; ++entry)
    {
        if (entry->framesize == framesize)
        {
            width = entry->width;
            height = entry->height;
            return true;
        }
    }
    return false;
}

} // anonymous namespace

// ============================================================
// build_camera_config
// ============================================================

ResolvedCameraParams build_camera_config(
    const CameraImageConfig& user_config,
    LoggingService& log)
{
    // Default: VGA
    hal::FrameSize framesize = hal::FrameSize::VGA;
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

    // Build single-valued HAL config (clamping happens in CameraController::init())
    hal::CameraConfig hal_config;
    hal_config.frame_size = framesize;
    hal_config.jpeg_quality = jpeg_quality;
    hal_config.fb_count = user_config.fb_count;
    hal_config.rotation = user_config.rotation;

    ResolvedCameraParams params;
    params.hal_config = hal_config;
    params.width = width;
    params.height = height;

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
    // Clamp construction-time config against HAL capabilities
    hal::CameraCapabilities caps = m_hal.get_capabilities();

    if (m_config.frame_size > caps.max_framesize)
    {
        m_config.frame_size = caps.max_framesize;
    }
    if (m_config.jpeg_quality < caps.min_jpeg_quality)
    {
        m_config.jpeg_quality = caps.min_jpeg_quality;
    }
    if (m_config.fb_count > caps.max_fb_count)
    {
        m_config.fb_count = caps.max_fb_count;
    }
    // Sensor-register rotation only supports 0/180; any other value is
    // rejected and falls back to no rotation.
    if (m_config.rotation != 0 && m_config.rotation != 180)
    {
        m_config.rotation = 0;
    }

    hal::CameraInitResult result = m_hal.init(m_config);
    m_initialized = (result == hal::CameraInitResult::SUCCESS);

    if (m_initialized)
    {
        m_actual_params.hal_config = m_config;
        resolve_dimensions(m_config.frame_size,
                           m_actual_params.width,
                           m_actual_params.height);
    }

    return result;
}

hal::CameraInitResult CameraController::init(const ResolvedCameraParams& params)
{
    m_config = params.hal_config;

    // Clamp desired config against HAL capabilities
    hal::CameraCapabilities caps = m_hal.get_capabilities();

    if (m_config.frame_size > caps.max_framesize)
    {
        m_config.frame_size = caps.max_framesize;
    }
    if (m_config.jpeg_quality < caps.min_jpeg_quality)
    {
        m_config.jpeg_quality = caps.min_jpeg_quality;
    }
    if (m_config.fb_count > caps.max_fb_count)
    {
        m_config.fb_count = caps.max_fb_count;
    }
    // Sensor-register rotation only supports 0/180; any other value is
    // rejected and falls back to no rotation.
    if (m_config.rotation != 0 && m_config.rotation != 180)
    {
        m_config.rotation = 0;
    }

    hal::CameraInitResult result = m_hal.init(m_config);
    m_initialized = (result == hal::CameraInitResult::SUCCESS);

    if (m_initialized)
    {
        m_actual_params.hal_config = m_config;
        if (!resolve_dimensions(m_config.frame_size,
                                m_actual_params.width,
                                m_actual_params.height))
        {
            // Fallback: use the pre-resolved dimensions from params
            m_actual_params.width = params.width;
            m_actual_params.height = params.height;
        }
    }

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

const ResolvedCameraParams& CameraController::get_params() const
{
    return m_actual_params;
}
