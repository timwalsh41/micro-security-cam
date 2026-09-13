#include <Arduino.h>
#include <unity.h>
#include "hal/nvs_hal_impl.h"
#include <cstring>

/**
 * @file test_nvs_hal_impl.cpp
 * @brief Hardware tests for hal::NvsHalImpl (real NVS flash).
 *
 * Verifies the repeat-begin contract against real NVS: begin() may be
 * called repeatedly (as the config flow does: boot-time load leaves the
 * namespace open, later saves re-open it), and blobs survive a
 * re-open. Tests use a dedicated namespace and clean up after
 * themselves.
 */

namespace
{

constexpr const char* TEST_NAMESPACE = "mc_nvstest";
constexpr const char* TEST_KEY = "test_blob";

} // namespace

/**
 * @brief begin() may be called repeatedly, and data persists across
 *        re-open.
 *
 * On-device regression: Preferences::begin() fails when already
 * started unless the prior handle is closed first, which broke NVS
 * saves that follow the boot-time config load.
 */
void test_repeat_begin_preserves_blob()
{
    hal::NvsHalImpl nvs;

    const uint8_t blob[] = "abc";
    TEST_ASSERT_TRUE(nvs.begin(TEST_NAMESPACE));
    TEST_ASSERT_TRUE(nvs.write_blob(TEST_KEY, blob, sizeof(blob) - 1));

    TEST_ASSERT_TRUE(nvs.begin(TEST_NAMESPACE));

    uint8_t buf[16] = {0};
    TEST_ASSERT_EQUAL_UINT32(sizeof(blob) - 1,
                             nvs.read_blob(TEST_KEY, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(blob, buf, sizeof(blob) - 1);

    TEST_ASSERT_TRUE(nvs.erase_key(TEST_KEY));
}

/**
 * @brief erase_key() works through the repeat-begin path so the suite
 *        leaves no residue in the test namespace.
 */
void test_erase_after_repeat_begin()
{
    hal::NvsHalImpl nvs;

    const uint8_t blob[] = "xyz";
    TEST_ASSERT_TRUE(nvs.begin(TEST_NAMESPACE));
    TEST_ASSERT_TRUE(nvs.write_blob(TEST_KEY, blob, sizeof(blob) - 1));
    TEST_ASSERT_TRUE(nvs.begin(TEST_NAMESPACE));
    TEST_ASSERT_TRUE(nvs.erase_key(TEST_KEY));

    uint8_t buf[8] = {0};
    TEST_ASSERT_EQUAL_UINT32(0, nvs.read_blob(TEST_KEY, buf, sizeof(buf)));
}

// ============================================================
// Test runner (Arduino entry point)
// ============================================================

/**
 * @brief Runs all NVS HAL tests once.
 *
 * @return Unity result code.
 */
int runUnityTests()
{
    UNITY_BEGIN();
    RUN_TEST(test_repeat_begin_preserves_blob);
    RUN_TEST(test_erase_after_repeat_begin);
    return UNITY_END();
}

/**
 * @brief Arduino entry point; runs the Unity suite.
 */
void setup()
{
    delay(2000);
    runUnityTests();
}

/**
 * @brief Arduino idle loop; unused.
 */
void loop() {}