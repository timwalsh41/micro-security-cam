#include <unity.h>
#include "camera_controller.h"
#include "mock_camera_hal.h"
#include "mock_log_backend.h"
#include "time_service.h"
#include <cstdio>

// ============================================================
// IDLE state — pre-init
// ============================================================

/**
 * @brief Before init(), is_initialized() returns false.
 */
void test_not_initialized_before_init()
{
    MockCameraHal hal;
    CameraController ctrl(hal);

    TEST_ASSERT_FALSE(ctrl.is_initialized());
}

/**
 * @brief Before init(), capture_frame() returns NOT_INITIALIZED.
 */
void test_capture_before_init_returns_not_initialized()
{
    MockCameraHal hal;
    CameraController ctrl(hal);

    TEST_ASSERT_EQUAL(CameraCaptureResult::NOT_INITIALIZED,
                      ctrl.capture_frame());
}

/**
 * @brief Before init(), get_frame() returns empty frame.
 */
void test_get_frame_before_init_returns_null()
{
    MockCameraHal hal;
    CameraController ctrl(hal);

    hal::CameraFrame frame = ctrl.get_frame();
    TEST_ASSERT_NULL(frame.data);
    TEST_ASSERT_EQUAL_UINT32(0, frame.len);
}

// ============================================================
// init() — success / failure
// ============================================================

/**
 * @brief init() delegates to HAL init() and returns SUCCESS.
 */
void test_init_success()
{
    MockCameraHal hal;
    CameraController ctrl(hal);

    hal.init_result = hal::CameraInitResult::SUCCESS;
    TEST_ASSERT_EQUAL(hal::CameraInitResult::SUCCESS, ctrl.init());
    TEST_ASSERT_TRUE(ctrl.is_initialized());
    TEST_ASSERT_EQUAL_INT(1, hal.init_call_count);
}

/**
 * @brief init() failure propagates, is_initialized() remains false.
 */
void test_init_failure()
{
    MockCameraHal hal;
    CameraController ctrl(hal);

    hal.init_result = hal::CameraInitResult::HARDWARE_FAILURE;
    TEST_ASSERT_EQUAL(hal::CameraInitResult::HARDWARE_FAILURE, ctrl.init());
    TEST_ASSERT_FALSE(ctrl.is_initialized());
    TEST_ASSERT_EQUAL_INT(1, hal.init_call_count);
}

/**
 * @brief Default-constructed controller uses default CameraConfig.
 */
void test_init_passes_default_config()
{
    MockCameraHal hal;
    CameraController ctrl(hal);

    hal.init_result = hal::CameraInitResult::SUCCESS;
    ctrl.init();

    hal::CameraConfig expected;
    TEST_ASSERT_EQUAL_UINT8(expected.jpeg_quality,
                            hal.last_init_config.jpeg_quality);
    TEST_ASSERT_EQUAL_UINT8(expected.frame_size,
                            hal.last_init_config.frame_size);
    TEST_ASSERT_EQUAL_UINT8(expected.fb_count,
                            hal.last_init_config.fb_count);
    TEST_ASSERT_EQUAL_UINT8(expected.grab_mode,
                            hal.last_init_config.grab_mode);
}

// ============================================================
// init() with custom configuration
// ============================================================

/**
 * @brief Custom config is clamped against capabilities and passed through.
 */
void test_init_passes_custom_config()
{
    MockCameraHal hal;
    // Set permissive capabilities so custom values pass through clamping
    hal.capabilities = {hal::FrameSize::UXGA, 6, 4, 0, 1600, 1200};

    hal::CameraConfig config;
    config.jpeg_quality = 8;
    config.frame_size = hal::FrameSize::VGA;
    config.fb_count = 2;
    CameraController ctrl(hal, config);

    hal.init_result = hal::CameraInitResult::SUCCESS;
    ctrl.init();

    TEST_ASSERT_EQUAL_INT(1, hal.init_call_count);
    TEST_ASSERT_EQUAL_UINT8(8, hal.last_init_config.jpeg_quality);
    TEST_ASSERT_EQUAL(hal::FrameSize::VGA, hal.last_init_config.frame_size);
    TEST_ASSERT_EQUAL_UINT8(2, hal.last_init_config.fb_count);
}

// ============================================================
// Clamping against capabilities
// ============================================================

/**
 * @brief init() clamps framesize to max_framesize capability.
 */
void test_init_clamps_framesize()
{
    MockCameraHal hal;
    // Conservative capabilities: max SVGA
    hal.capabilities = {hal::FrameSize::SVGA, 1, 4, 0, 800, 600};

    hal::CameraConfig config;
    config.frame_size = hal::FrameSize::UXGA;  // exceeds max_framesize = SVGA
    CameraController ctrl(hal, config);

    hal.init_result = hal::CameraInitResult::SUCCESS;
    ctrl.init();

    TEST_ASSERT_EQUAL(hal::FrameSize::SVGA, hal.last_init_config.frame_size);
}

/**
 * @brief init() clamps JPEG quality to min_jpeg_quality capability.
 */
void test_init_clamps_quality()
{
    MockCameraHal hal;
    // Conservative capabilities: min quality 12
    hal.capabilities = {hal::FrameSize::QVGA, 12, 2, 0, 1600, 1200};

    hal::CameraConfig config;
    config.jpeg_quality = 5;  // Better (lower) than min_jpeg_quality=12
    CameraController ctrl(hal, config);

    hal.init_result = hal::CameraInitResult::SUCCESS;
    ctrl.init();

    TEST_ASSERT_EQUAL_UINT8(12, hal.last_init_config.jpeg_quality);
}

/**
 * @brief init() clamps fb_count to max_fb_count capability.
 */
void test_init_clamps_fb_count()
{
    MockCameraHal hal;
    hal.capabilities = {hal::FrameSize::UXGA, 1, 1, 0, 1600, 1200};

    hal::CameraConfig config;
    config.fb_count = 4;  // exceeds max_fb_count=1
    CameraController ctrl(hal, config);

    hal.init_result = hal::CameraInitResult::SUCCESS;
    ctrl.init();

    TEST_ASSERT_EQUAL_UINT8(1, hal.last_init_config.fb_count);
}

// ============================================================
// get_params()
// ============================================================

/**
 * @brief get_params() returns default values before init().
 */
void test_get_params_before_init()
{
    MockCameraHal hal;
    CameraController ctrl(hal);

    const ResolvedCameraParams& params = ctrl.get_params();
    TEST_ASSERT_EQUAL_UINT16(640, params.width);
    TEST_ASSERT_EQUAL_UINT16(480, params.height);
}

/**
 * @brief get_params() returns clamped dimensions after init().
 */
void test_get_params_after_init()
{
    MockCameraHal hal;
    // Conservative: SVGA (800x600) max
    hal.capabilities = {hal::FrameSize::SVGA, 12, 1, 0, 800, 600};

    hal::CameraConfig config;
    config.frame_size = hal::FrameSize::UXGA;  // 1600x1200 — will be clamped to SVGA
    CameraController ctrl(hal, config);

    hal.init_result = hal::CameraInitResult::SUCCESS;
    ctrl.init();

    const ResolvedCameraParams& params = ctrl.get_params();
    TEST_ASSERT_EQUAL(hal::FrameSize::SVGA, params.hal_config.frame_size);
    TEST_ASSERT_EQUAL_UINT16(800, params.width);
    TEST_ASSERT_EQUAL_UINT16(600, params.height);
}

// ============================================================
// deinit()
// ============================================================

/**
 * @brief deinit() delegates to HAL deinit() and clears initialized state.
 */
void test_deinit_delegates_to_hal()
{
    MockCameraHal hal;
    CameraController ctrl(hal);

    hal.init_result = hal::CameraInitResult::SUCCESS;
    ctrl.init();
    TEST_ASSERT_TRUE(ctrl.is_initialized());

    ctrl.deinit();
    TEST_ASSERT_FALSE(ctrl.is_initialized());
    TEST_ASSERT_EQUAL_INT(1, hal.deinit_call_count);
}

/**
 * @brief deinit() is safe to call when not initialized.
 */
void test_deinit_when_not_initialized()
{
    MockCameraHal hal;
    CameraController ctrl(hal);

    ctrl.deinit();
    TEST_ASSERT_FALSE(ctrl.is_initialized());
    TEST_ASSERT_EQUAL_INT(1, hal.deinit_call_count);
}

/**
 * @brief deinit() releases any acquired frame before deinitializing.
 */
void test_deinit_releases_acquired_frame()
{
    MockCameraHal hal;
    CameraController ctrl(hal);

    hal.init_result = hal::CameraInitResult::SUCCESS;
    ctrl.init();

    // Capture a frame
    hal.capture_result = {hal.frame_buffer, 64};
    ctrl.capture_frame();

    // Deinit should release the frame first
    ctrl.deinit();
    TEST_ASSERT_EQUAL_INT(1, hal.release_call_count);
    TEST_ASSERT_EQUAL_INT(1, hal.deinit_call_count);
    TEST_ASSERT_FALSE(ctrl.is_initialized());
}

// ============================================================
// capture_frame() / get_frame() / release_frame()
// ============================================================

/**
 * @brief Successful capture returns SUCCESS and frame data is accessible.
 */
void test_capture_success()
{
    MockCameraHal hal;
    CameraController ctrl(hal);

    hal.init_result = hal::CameraInitResult::SUCCESS;
    ctrl.init();

    hal.capture_result = {hal.frame_buffer, 256};
    TEST_ASSERT_EQUAL(CameraCaptureResult::SUCCESS, ctrl.capture_frame());
    TEST_ASSERT_EQUAL_INT(1, hal.capture_call_count);

    hal::CameraFrame frame = ctrl.get_frame();
    TEST_ASSERT_EQUAL_PTR(hal.frame_buffer, frame.data);
    TEST_ASSERT_EQUAL_UINT32(256, frame.len);
}

/**
 * @brief Capture failure returns CAPTURE_FAILED and get_frame() returns null.
 */
void test_capture_failure()
{
    MockCameraHal hal;
    CameraController ctrl(hal);

    hal.init_result = hal::CameraInitResult::SUCCESS;
    ctrl.init();

    // HAL returns null frame
    hal.capture_result = {nullptr, 0};
    TEST_ASSERT_EQUAL(CameraCaptureResult::CAPTURE_FAILED, ctrl.capture_frame());

    hal::CameraFrame frame = ctrl.get_frame();
    TEST_ASSERT_NULL(frame.data);
    TEST_ASSERT_EQUAL_UINT32(0, frame.len);
}

/**
 * @brief release_frame() delegates to HAL release() and clears state.
 */
void test_release_frame_delegates_to_hal()
{
    MockCameraHal hal;
    CameraController ctrl(hal);

    hal.init_result = hal::CameraInitResult::SUCCESS;
    ctrl.init();

    // Capture a frame first
    hal.capture_result = {hal.frame_buffer, 128};
    ctrl.capture_frame();

    // Release should call HAL release
    ctrl.release_frame();
    TEST_ASSERT_EQUAL_INT(1, hal.release_call_count);

    // Subsequent get_frame returns null
    hal::CameraFrame empty = ctrl.get_frame();
    TEST_ASSERT_NULL(empty.data);
    TEST_ASSERT_EQUAL_UINT32(0, empty.len);
}

/**
 * @brief release_frame() with no active capture is a safe no-op.
 */
void test_release_frame_without_capture()
{
    MockCameraHal hal;
    CameraController ctrl(hal);

    hal.init_result = hal::CameraInitResult::SUCCESS;
    ctrl.init();

    // Release without capture — should not call HAL release
    ctrl.release_frame();
    TEST_ASSERT_EQUAL_INT(0, hal.release_call_count);
}

/**
 * @brief Double capture releases the previous frame automatically.
 */
void test_double_capture_releases_previous()
{
    MockCameraHal hal;
    CameraController ctrl(hal);

    hal.init_result = hal::CameraInitResult::SUCCESS;
    ctrl.init();

    // First capture
    hal.capture_result = {hal.frame_buffer, 100};
    TEST_ASSERT_EQUAL(CameraCaptureResult::SUCCESS, ctrl.capture_frame());
    TEST_ASSERT_EQUAL_INT(0, hal.release_call_count);

    // Second capture — should auto-release the first
    TEST_ASSERT_EQUAL(CameraCaptureResult::SUCCESS, ctrl.capture_frame());
    TEST_ASSERT_EQUAL_INT(1, hal.release_call_count);
    TEST_ASSERT_EQUAL_INT(2, hal.capture_call_count);

    // Frame should be from second capture
    hal::CameraFrame frame = ctrl.get_frame();
    TEST_ASSERT_EQUAL_PTR(hal.frame_buffer, frame.data);
    TEST_ASSERT_EQUAL_UINT32(100, frame.len);
}

// ============================================================
// grab_mode configuration
// ============================================================

/**
 * @brief Default grab_mode (0xFF sentinel) is passed through to HAL.
 */
void test_init_default_grab_mode_is_auto_sentinel()
{
    MockCameraHal hal;
    CameraController ctrl(hal);

    hal.init_result = hal::CameraInitResult::SUCCESS;
    ctrl.init();

    TEST_ASSERT_EQUAL_UINT8(0xFF, hal.last_init_config.grab_mode);
}

/**
 * @brief Explicit grab_mode = CAMERA_GRAB_WHEN_EMPTY (0) is passed through.
 */
void test_init_explicit_grab_mode_when_empty()
{
    MockCameraHal hal;
    hal::CameraConfig config;
    config.grab_mode = 0;  // CAMERA_GRAB_WHEN_EMPTY
    CameraController ctrl(hal, config);

    hal.init_result = hal::CameraInitResult::SUCCESS;
    ctrl.init();

    TEST_ASSERT_EQUAL_UINT8(0, hal.last_init_config.grab_mode);
}

/**
 * @brief Explicit grab_mode = CAMERA_GRAB_LATEST (1) is passed through.
 */
void test_init_explicit_grab_mode_latest()
{
    MockCameraHal hal;
    hal::CameraConfig config;
    config.grab_mode = 1;  // CAMERA_GRAB_LATEST
    CameraController ctrl(hal, config);

    hal.init_result = hal::CameraInitResult::SUCCESS;
    ctrl.init();

    TEST_ASSERT_EQUAL_UINT8(1, hal.last_init_config.grab_mode);
}

// ============================================================
// rotation configuration
// ============================================================

/**
 * @brief build_camera_config carries rotation into the HAL config.
 */
void test_build_camera_config_carries_rotation()
{
    MockLogBackend log_backend;
    RealTimeService time;
    LoggingService log{log_backend, time};

    CameraImageConfig user_config;
    user_config.rotation = 180;

    auto params = build_camera_config(user_config, log);
    TEST_ASSERT_EQUAL_UINT16(180, params.hal_config.rotation);
}

/**
 * @brief init() passes rotation 180 through to the HAL unmodified.
 */
void test_init_rotation_180_passed_to_hal()
{
    MockCameraHal hal;
    hal::CameraConfig config;
    config.rotation = 180;
    CameraController ctrl(hal, config);

    hal.init_result = hal::CameraInitResult::SUCCESS;
    ctrl.init();

    TEST_ASSERT_EQUAL_UINT16(180, hal.last_requested_rotation);
}

/**
 * @brief init() passes rotation 0 through to the HAL unmodified.
 */
void test_init_rotation_0_passed_to_hal()
{
    MockCameraHal hal;
    hal::CameraConfig config;
    config.rotation = 0;
    CameraController ctrl(hal, config);

    hal.init_result = hal::CameraInitResult::SUCCESS;
    ctrl.init();

    TEST_ASSERT_EQUAL_UINT16(0, hal.last_requested_rotation);
}

/**
 * @brief Unsupported rotation (90) is clamped to 0 before reaching the HAL.
 */
void test_init_unsupported_rotation_90_clamped_to_zero()
{
    MockCameraHal hal;
    hal::CameraConfig config;
    config.rotation = 90;
    CameraController ctrl(hal, config);

    hal.init_result = hal::CameraInitResult::SUCCESS;
    ctrl.init();

    TEST_ASSERT_EQUAL_UINT16(0, hal.last_requested_rotation);
}

/**
 * @brief Unsupported rotation (270) is clamped to 0 before reaching the HAL.
 */
void test_init_unsupported_rotation_270_clamped_to_zero()
{
    MockCameraHal hal;
    hal::CameraConfig config;
    config.rotation = 270;
    CameraController ctrl(hal, config);

    hal.init_result = hal::CameraInitResult::SUCCESS;
    ctrl.init();

    TEST_ASSERT_EQUAL_UINT16(0, hal.last_requested_rotation);
}

// ============================================================
// Resolution mapping regression (hal::FrameSize contract)
// ============================================================

/**
 * @brief Each config string maps to the correct hal::FrameSize and dimensions.
 */
void test_build_camera_config_framesize_mapping()
{
    MockLogBackend log_backend;
    RealTimeService time;
    LoggingService log{log_backend, time};

    struct Mapping
    {
        const char* name;
        hal::FrameSize framesize;
        uint16_t width;
        uint16_t height;
    };

    const Mapping MAPPINGS[] =
    {
        {"QQVGA",  hal::FrameSize::QQVGA,  160,  120},
        {"QVGA",   hal::FrameSize::QVGA,   320,  240},
        {"VGA",    hal::FrameSize::VGA,    640,  480},
        {"SVGA",   hal::FrameSize::SVGA,   800,  600},
        {"XGA",    hal::FrameSize::XGA,   1024,  768},
        {"HD",     hal::FrameSize::HD,    1280,  720},
        {"UXGA",   hal::FrameSize::UXGA,  1600, 1200},
    };

    for (const Mapping& m : MAPPINGS)
    {
        CameraImageConfig user_config;
        user_config.resolution = m.name;
        user_config.jpeg_quality = 10;

        auto params = build_camera_config(user_config, log);
        char msg[64];
        snprintf(msg, sizeof(msg), "resolution %s", m.name);
        TEST_ASSERT_EQUAL_MESSAGE(m.framesize, params.hal_config.frame_size, msg);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(m.width, params.width, msg);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(m.height, params.height, msg);
    }
}

/**
 * @brief Unknown resolution defaults to VGA (hal::FrameSize::VGA, 640x480).
 */
void test_build_camera_config_unknown_defaults_to_vga_framesize()
{
    MockLogBackend log_backend;
    RealTimeService time;
    LoggingService log{log_backend, time};

    CameraImageConfig user_config;
    user_config.resolution = "UNKNOWN";

    auto params = build_camera_config(user_config, log);
    TEST_ASSERT_EQUAL(hal::FrameSize::VGA, params.hal_config.frame_size);
    TEST_ASSERT_EQUAL_UINT16(640, params.width);
    TEST_ASSERT_EQUAL_UINT16(480, params.height);
}

/**
 * @brief With PSRAM capabilities (max_framesize = UXGA), an HD config
 *        must NOT be clamped (HD <= UXGA).
 */
void test_psram_capabilities_do_not_clamp_hd()
{
    MockCameraHal hal;
    hal.capabilities = hal::PSRAM_CAPABILITIES;

    hal::CameraConfig config;
    config.frame_size = hal::FrameSize::HD;
    CameraController ctrl(hal, config);

    hal.init_result = hal::CameraInitResult::SUCCESS;
    ctrl.init();

    TEST_ASSERT_EQUAL(hal::FrameSize::HD, hal.last_init_config.frame_size);

    const ResolvedCameraParams& params = ctrl.get_params();
    TEST_ASSERT_EQUAL(hal::FrameSize::HD, params.hal_config.frame_size);
    TEST_ASSERT_EQUAL_UINT16(1280, params.width);
    TEST_ASSERT_EQUAL_UINT16(720, params.height);
}

// ============================================================
// Full lifecycle
// ============================================================

/**
 * @brief Complete lifecycle: init → capture → use data → release → deinit.
 */
void test_full_lifecycle()
{
    MockCameraHal hal;
    CameraController ctrl(hal);

    // Init
    hal.init_result = hal::CameraInitResult::SUCCESS;
    TEST_ASSERT_EQUAL(hal::CameraInitResult::SUCCESS, ctrl.init());
    TEST_ASSERT_TRUE(ctrl.is_initialized());

    // Capture
    hal.capture_result = {hal.frame_buffer, 512};
    TEST_ASSERT_EQUAL(CameraCaptureResult::SUCCESS, ctrl.capture_frame());

    // Use data
    hal::CameraFrame frame = ctrl.get_frame();
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL_UINT32(512, frame.len);

    // Release
    ctrl.release_frame();
    TEST_ASSERT_EQUAL_INT(1, hal.release_call_count);

    // Deinit
    ctrl.deinit();
    TEST_ASSERT_FALSE(ctrl.is_initialized());
    TEST_ASSERT_EQUAL_INT(1, hal.deinit_call_count);
}

// ============================================================
// Unity boilerplate
// ============================================================

void setUp() {}
void tearDown() {}

int main(int argc, char **argv)
{
    UNITY_BEGIN();

    // Pre-init state
    RUN_TEST(test_not_initialized_before_init);
    RUN_TEST(test_capture_before_init_returns_not_initialized);
    RUN_TEST(test_get_frame_before_init_returns_null);

    // init()
    RUN_TEST(test_init_success);
    RUN_TEST(test_init_failure);
    RUN_TEST(test_init_passes_default_config);
    RUN_TEST(test_init_passes_custom_config);

    // Clamping
    RUN_TEST(test_init_clamps_framesize);
    RUN_TEST(test_init_clamps_quality);
    RUN_TEST(test_init_clamps_fb_count);

    // get_params()
    RUN_TEST(test_get_params_before_init);
    RUN_TEST(test_get_params_after_init);

    // deinit()
    RUN_TEST(test_deinit_delegates_to_hal);
    RUN_TEST(test_deinit_when_not_initialized);
    RUN_TEST(test_deinit_releases_acquired_frame);

    // capture / get / release
    RUN_TEST(test_capture_success);
    RUN_TEST(test_capture_failure);
    RUN_TEST(test_release_frame_delegates_to_hal);
    RUN_TEST(test_release_frame_without_capture);
    RUN_TEST(test_double_capture_releases_previous);

    // grab_mode configuration
    RUN_TEST(test_init_default_grab_mode_is_auto_sentinel);
    RUN_TEST(test_init_explicit_grab_mode_when_empty);
    RUN_TEST(test_init_explicit_grab_mode_latest);

    // rotation configuration
    RUN_TEST(test_build_camera_config_carries_rotation);
    RUN_TEST(test_init_rotation_180_passed_to_hal);
    RUN_TEST(test_init_rotation_0_passed_to_hal);
    RUN_TEST(test_init_unsupported_rotation_90_clamped_to_zero);
    RUN_TEST(test_init_unsupported_rotation_270_clamped_to_zero);

    // resolution mapping regression
    RUN_TEST(test_build_camera_config_framesize_mapping);
    RUN_TEST(test_build_camera_config_unknown_defaults_to_vga_framesize);
    RUN_TEST(test_psram_capabilities_do_not_clamp_hd);

    // Full lifecycle
    RUN_TEST(test_full_lifecycle);

    return UNITY_END();
}
