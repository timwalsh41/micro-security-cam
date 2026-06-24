#include "camera_controller.h"

// ============================================================
// Construction
// ============================================================

CameraController::CameraController(hal::CameraHal &hal)
    : m_hal(hal)
{
}

CameraController::CameraController(hal::CameraHal &hal,
                                   const hal::CameraConfig &config)
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
