#include <unity.h>
#include "camera_controller.h"
#include "mock_camera_hal.h"

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
    TEST_ASSERT_EQUAL_UINT8(expected.jpeg_quality_no_psram,
                            hal.last_init_config.jpeg_quality_no_psram);
    TEST_ASSERT_EQUAL_UINT8(expected.jpeg_quality_with_psram,
                            hal.last_init_config.jpeg_quality_with_psram);
    TEST_ASSERT_EQUAL_UINT8(expected.fb_count_with_psram,
                            hal.last_init_config.fb_count_with_psram);
    TEST_ASSERT_EQUAL_UINT8(expected.fb_count_no_psram,
                            hal.last_init_config.fb_count_no_psram);
    TEST_ASSERT_EQUAL_UINT8(expected.frame_size_no_psram,
                            hal.last_init_config.frame_size_no_psram);
    TEST_ASSERT_EQUAL_UINT8(expected.grab_mode,
                            hal.last_init_config.grab_mode);
}

// ============================================================
// init() with custom configuration
// ============================================================

/**
 * @brief Custom config is passed through to the HAL.
 */
void test_init_passes_custom_config()
{
    MockCameraHal hal;
    hal::CameraConfig config;
    config.jpeg_quality_no_psram = 8;
    config.jpeg_quality_with_psram = 6;
    config.fb_count_with_psram = 4;
    config.fb_count_no_psram = 2;
    config.frame_size_no_psram = 8;  // FRAMESIZE_VGA
    CameraController ctrl(hal, config);

    hal.init_result = hal::CameraInitResult::SUCCESS;
    ctrl.init();

    TEST_ASSERT_EQUAL_INT(1, hal.init_call_count);
    TEST_ASSERT_EQUAL_UINT8(8, hal.last_init_config.jpeg_quality_no_psram);
    TEST_ASSERT_EQUAL_UINT8(6, hal.last_init_config.jpeg_quality_with_psram);
    TEST_ASSERT_EQUAL_UINT8(4, hal.last_init_config.fb_count_with_psram);
    TEST_ASSERT_EQUAL_UINT8(2, hal.last_init_config.fb_count_no_psram);
    TEST_ASSERT_EQUAL_UINT8(8, hal.last_init_config.frame_size_no_psram);
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

    // Full lifecycle
    RUN_TEST(test_full_lifecycle);

    return UNITY_END();
}
