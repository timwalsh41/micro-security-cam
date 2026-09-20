#include <unity.h>
#include "mic_service.h"
#include "mock_mic_hal.h"
#include "mock_log_backend.h"
#include "config_service.h"
#include "time_service.h"
#include "mock_mic_task_runner.h"
#include "mock_memory_allocator.h"
#include <cstring>
#include <vector>

// ============================================================
// Test fixture
// ============================================================

struct TestFixture
{
    MockMicHal mic_hal;
    MockLogBackend log_backend;
    RealTimeService time;
    LoggingService logger;
    ConfigService config;
    MockMicTaskRunner task_runner;
    MockMemoryAllocator allocator;
    MicService svc;

    TestFixture()
        : logger(log_backend, time)
        , config(logger)
        , svc(mic_hal, logger, config, task_runner, allocator)
    {
    }
};

// ============================================================
// Tests
// ============================================================

/**
 * @brief init() returns true when MicHal init succeeds.
 */
void test_init_success()
{
    TestFixture f;
    f.mic_hal.init_result = hal::MicInitResult::SUCCESS;
    TEST_ASSERT_TRUE(f.svc.init());
}

/**
 * @brief init() returns false when MicHal init fails.
 */
void test_init_failure()
{
    TestFixture f;
    f.mic_hal.init_result = hal::MicInitResult::HARDWARE_FAILURE;
    TEST_ASSERT_FALSE(f.svc.init());
}

/**
 * @brief start() returns true after init.
 */
void test_start_after_init()
{
    TestFixture f;
    f.mic_hal.init_result = hal::MicInitResult::SUCCESS;
    f.svc.init();
    TEST_ASSERT_TRUE(f.svc.start());
    TEST_ASSERT_TRUE(f.svc.is_capturing());
    // Stop the capture so the mic task thread is joined before the
    // service and fixture go out of scope.
    TEST_ASSERT_TRUE(f.svc.stop());
}

/**
 * @brief stop() returns true after start.
 */
void test_stop_after_start()
{
    TestFixture f;
    f.mic_hal.init_result = hal::MicInitResult::SUCCESS;
    f.svc.init();
    f.svc.start();
    TEST_ASSERT_TRUE(f.svc.stop());
    TEST_ASSERT_FALSE(f.svc.is_capturing());
}

/**
 * @brief deinit() releases resources.
 */
void test_deinit_releases_resources()
{
    TestFixture f;
    f.mic_hal.init_result = hal::MicInitResult::SUCCESS;
    f.svc.init();
    f.svc.deinit();
    TEST_ASSERT_EQUAL_INT(1, f.mic_hal.deinit_call_count);
}

/**
 * @brief get_audio_chunk() returns 0 when ring buffer is empty.
 */
void test_get_audio_chunk_empty()
{
    TestFixture f;
    f.mic_hal.init_result = hal::MicInitResult::SUCCESS;
    f.svc.init();
    int16_t buffer[100];
    TEST_ASSERT_EQUAL_UINT32(0, f.svc.get_audio_chunk(buffer, 100));
}

/**
 * @brief sample_rate() returns 16000.
 */
void test_sample_rate()
{
    TestFixture f;
    f.mic_hal.init_result = hal::MicInitResult::SUCCESS;
    f.svc.init();
    TEST_ASSERT_EQUAL_UINT32(16000, f.svc.sample_rate());
}

/**
 * @brief bits_per_sample() returns 16.
 */
void test_bits_per_sample()
{
    TestFixture f;
    f.mic_hal.init_result = hal::MicInitResult::SUCCESS;
    f.svc.init();
    TEST_ASSERT_EQUAL_UINT16(16, f.svc.bits_per_sample());
}

/**
 * @brief channels() returns 1.
 */
void test_channels()
{
    TestFixture f;
    f.mic_hal.init_result = hal::MicInitResult::SUCCESS;
    f.svc.init();
    TEST_ASSERT_EQUAL_UINT16(1, f.svc.channels());
}

/**
 * @brief generate_wav_header() produces valid 44-byte header.
 */
void test_generate_wav_header()
{
    TestFixture f;
    f.mic_hal.init_result = hal::MicInitResult::SUCCESS;
    f.svc.init();
    uint8_t header[44];
    TEST_ASSERT_TRUE(f.svc.generate_wav_header(header, sizeof(header), 32000));
    // Check RIFF marker
    TEST_ASSERT_EQUAL_UINT8('R', header[0]);
    TEST_ASSERT_EQUAL_UINT8('I', header[1]);
    TEST_ASSERT_EQUAL_UINT8('F', header[2]);
    TEST_ASSERT_EQUAL_UINT8('F', header[3]);
    // Check WAVE marker
    TEST_ASSERT_EQUAL_UINT8('W', header[8]);
    TEST_ASSERT_EQUAL_UINT8('A', header[9]);
    TEST_ASSERT_EQUAL_UINT8('V', header[10]);
    TEST_ASSERT_EQUAL_UINT8('E', header[11]);
}

/**
 * @brief generate_wav_header() returns false with null buffer.
 */
void test_generate_wav_header_null_buffer()
{
    TestFixture f;
    f.mic_hal.init_result = hal::MicInitResult::SUCCESS;
    f.svc.init();
    TEST_ASSERT_FALSE(f.svc.generate_wav_header(nullptr, 44, 0));
}

/**
 * @brief generate_wav_header() returns false with small buffer.
 */
void test_generate_wav_header_small_buffer()
{
    TestFixture f;
    f.mic_hal.init_result = hal::MicInitResult::SUCCESS;
    f.svc.init();
    uint8_t header[10];
    TEST_ASSERT_FALSE(f.svc.generate_wav_header(header, sizeof(header), 0));
}

// ============================================================
// Test runner
// ============================================================

int main()
{
    UNITY_BEGIN();

    RUN_TEST(test_init_success);
    RUN_TEST(test_init_failure);
    RUN_TEST(test_start_after_init);
    RUN_TEST(test_stop_after_start);
    RUN_TEST(test_deinit_releases_resources);
    RUN_TEST(test_get_audio_chunk_empty);
    RUN_TEST(test_sample_rate);
    RUN_TEST(test_bits_per_sample);
    RUN_TEST(test_channels);
    RUN_TEST(test_generate_wav_header);
    RUN_TEST(test_generate_wav_header_null_buffer);
    RUN_TEST(test_generate_wav_header_small_buffer);

    return UNITY_END();
}