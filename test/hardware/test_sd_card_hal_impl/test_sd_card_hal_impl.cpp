#include <Arduino.h>
#include <unity.h>
#include "hal/sd_card_hal_impl.h"
#include "hal/gpio_hal.h"
#include <cstring>

/**
 * @file test_sd_card_hal_impl.cpp
 * @brief Hardware tests for hal::SdCardHalImpl (real SD card).
 *
 * Verifies open-mode semantics against the physical card: WRITE
 * truncates, APPEND preserves, and READ_WRITE seek+write preserves
 * existing content (the on-device regression for the AVI header
 * truncation bug). All tests are ignored when no card is present.
 */

/**
 * @brief Fixture wrapping the real SD card HAL.
 */
struct SdFixture
{
    hal::SdCardHalImpl sd;

    SdFixture()
        : sd(hal::SD_CS_PIN)
    {
    }

    /**
     * @brief Initializes the card and reports whether it is usable.
     *
     * @return true if the card initialized successfully.
     */
    bool init()
    {
        return sd.init() == hal::SdInitResult::SUCCESS;
    }

    /**
     * @brief Reads the full contents of a file into a buffer.
     *
     * @param path  File path on the card.
     * @param out   Destination buffer.
     * @param size  Size of the destination buffer.
     * @return Number of bytes read.
     */
    size_t read_file(const char* path, uint8_t* out, size_t size)
    {
        size_t total = 0;
        if (sd.open(path, hal::FileOpenMode::READ))
        {
            while (total < size)
            {
                size_t n = sd.read(out + total, size - total);
                if (n == 0)
                {
                    break;
                }
                total += n;
            }
            sd.close();
        }
        return total;
    }
};

/**
 * @brief Helper: ignores the test when no SD card is present.
 *
 * @param fixture Initialized fixture.
 */
static void require_sd_card(SdFixture& fixture)
{
    if (!fixture.init())
    {
        TEST_IGNORE_MESSAGE("No SD card present");
    }
}

// ============================================================
// Open-mode semantics
// ============================================================

/**
 * @brief WRITE truncates existing content.
 */
void test_write_truncates_existing_content()
{
    SdFixture f;
    require_sd_card(f);

    const char* path = "/t_trunc.bin";
    SD.remove(path);

    const uint8_t first[] = "hello";
    TEST_ASSERT_TRUE(f.sd.open(path, hal::FileOpenMode::WRITE));
    TEST_ASSERT_TRUE(f.sd.write(first, sizeof(first) - 1));
    f.sd.close();

    const uint8_t second[] = "X";
    TEST_ASSERT_TRUE(f.sd.open(path, hal::FileOpenMode::WRITE));
    TEST_ASSERT_TRUE(f.sd.write(second, sizeof(second) - 1));
    f.sd.close();

    uint8_t buf[16] = {0};
    size_t size = f.read_file(path, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT32(1, size);
    TEST_ASSERT_EQUAL_UINT8('X', buf[0]);

    SD.remove(path);
}

/**
 * @brief APPEND preserves existing content and appends at the end.
 */
void test_append_preserves_existing_content()
{
    SdFixture f;
    require_sd_card(f);

    const char* path = "/t_append.bin";
    SD.remove(path);

    const uint8_t first[] = "hello";
    TEST_ASSERT_TRUE(f.sd.open(path, hal::FileOpenMode::WRITE));
    TEST_ASSERT_TRUE(f.sd.write(first, sizeof(first) - 1));
    f.sd.close();

    const uint8_t second[] = " world";
    TEST_ASSERT_TRUE(f.sd.open(path, hal::FileOpenMode::APPEND));
    TEST_ASSERT_TRUE(f.sd.write(second, sizeof(second) - 1));
    f.sd.close();

    uint8_t buf[32] = {0};
    size_t size = f.read_file(path, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT32(11, size);
    TEST_ASSERT_EQUAL_STRING("hello world", reinterpret_cast<const char*>(buf));

    SD.remove(path);
}

/**
 * @brief READ_WRITE seek+write preserves existing content.
 *
 * On-device regression for the AVI header truncation bug: opening
 * with "r+" must not truncate, and seek+write patches in place.
 */
void test_read_write_seek_preserves_existing_content()
{
    SdFixture f;
    require_sd_card(f);

    const char* path = "/t_rw.bin";
    SD.remove(path);

    const uint8_t original[] = "0123456789";
    TEST_ASSERT_TRUE(f.sd.open(path, hal::FileOpenMode::WRITE));
    TEST_ASSERT_TRUE(f.sd.write(original, sizeof(original) - 1));
    f.sd.close();

    const uint8_t patch[] = "XY";
    TEST_ASSERT_TRUE(f.sd.open(path, hal::FileOpenMode::READ_WRITE));
    TEST_ASSERT_TRUE(f.sd.seek(2));
    TEST_ASSERT_TRUE(f.sd.write(patch, sizeof(patch) - 1));
    f.sd.close();

    uint8_t buf[32] = {0};
    size_t size = f.read_file(path, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT32(10, size);
    TEST_ASSERT_EQUAL_STRING("01XY456789", reinterpret_cast<const char*>(buf));

    SD.remove(path);
}

// ============================================================
// Test runner (Arduino entry point)
// ============================================================

/**
 * @brief Runs all SD card tests once.
 *
 * @return Unity result code.
 */
int runUnityTests()
{
    UNITY_BEGIN();
    RUN_TEST(test_write_truncates_existing_content);
    RUN_TEST(test_append_preserves_existing_content);
    RUN_TEST(test_read_write_seek_preserves_existing_content);
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
