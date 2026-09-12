#include <unity.h>
#include "avi_clip_session.h"
#include "mock_sd_card_hal.h"
#include "mock_log_backend.h"
#include "time_service.h"
#include <cstring>
#include <string>

// ============================================================
// Test fixture
// ============================================================

static constexpr uint16_t TEST_FPS = 10;
static constexpr uint16_t TEST_CLIP_DURATION_S = 5;
static constexpr uint16_t TEST_WIDTH = 640;
static constexpr uint16_t TEST_HEIGHT = 480;
static constexpr size_t FAKE_JPEG_SIZE = 100;

/**
 * @brief Sets up a configured AviClipSession with mock SD backing.
 */
struct TestFixture
{
    MockSdCardHal sd_hal;
    RealTimeService time;
    MockLogBackend log_backend;
    LoggingService logger;
    SdController sd_controller;
    AviClipSession session;

    TestFixture()
        : logger(log_backend, time)
        , sd_controller(time, sd_hal)
        , session(sd_controller, time, logger)
    {
        set_mock_millis(1000);
        ConfigData::VideoConfig config;
        config.fps = TEST_FPS;
        strcpy(config.resolution, "VGA");
        config.clip_duration_s = TEST_CLIP_DURATION_S;
        config.max_file_size_mb = 0;
        config.jpeg_quality = 10;
        session.init(config, TEST_WIDTH, TEST_HEIGHT);

        memset(frame_buffer, 0xAB, FAKE_JPEG_SIZE);
    }

    uint8_t frame_buffer[FAKE_JPEG_SIZE]{};

    const uint8_t* frame() const
    {
        return frame_buffer;
    }
};

/**
 * @brief Reads the little-endian u32 at the given offset.
 */
static uint32_t read_u32_le(const std::vector<uint8_t>& content, size_t offset)
{
    return static_cast<uint32_t>(content[offset]) |
           (static_cast<uint32_t>(content[offset + 1]) << 8) |
           (static_cast<uint32_t>(content[offset + 2]) << 16) |
           (static_cast<uint32_t>(content[offset + 3]) << 24);
}

/**
 * @brief Returns whether a stored clip has a patched AVI header.
 */
static bool clip_file_is_patched(const std::vector<uint8_t>& content)
{
    if (content.size() < 224)
    {
        return false;
    }
    return read_u32_le(content, 4) ==
           static_cast<uint32_t>(content.size() - 8);
}

// ============================================================
// Tests
// ============================================================

/**
 * @brief init() accepts the configuration.
 */
void test_init_accepts_config()
{
    TestFixture f;

    ConfigData::VideoConfig config;
    config.fps = TEST_FPS;
    strcpy(config.resolution, "VGA");
    config.clip_duration_s = TEST_CLIP_DURATION_S;
    config.max_file_size_mb = 0;
    config.jpeg_quality = 10;

    TEST_ASSERT_TRUE(f.session.init(config, TEST_WIDTH, TEST_HEIGHT));
    TEST_ASSERT_FALSE(f.session.is_open());
    TEST_ASSERT_FALSE(f.session.close_pending());
    TEST_ASSERT_EQUAL_UINT32(0, f.session.frames_recorded());
    TEST_ASSERT_EQUAL_UINT32(0, f.session.file_size());
}

/**
 * @brief open() creates the parent directory, writes a placeholder
 *        header, and latches close_pending.
 */
void test_open_creates_file_with_placeholder_header()
{
    TestFixture f;

    TEST_ASSERT_TRUE(f.session.open());

    TEST_ASSERT_TRUE(f.session.is_open());
    TEST_ASSERT_TRUE(f.session.close_pending());

    // Parent directory creation happened.
    TEST_ASSERT_GREATER_THAN(0, f.sd_hal.create_dir_call_count);
    TEST_ASSERT_EQUAL_STRING("/19700101", f.sd_hal.last_create_dir_path);

    // Exactly one clip file exists with the documented naming pattern.
    TEST_ASSERT_EQUAL_UINT32(1, f.sd_hal.files.size());
    const std::string path = f.session.file_path();
    TEST_ASSERT_NOT_EQUAL(std::string::npos,
                          path.find("/19700101/19700101_000001_640x480_10fps.avi"));

    // The stored file carries the placeholder header (RIFF size 0).
    const std::vector<uint8_t>& content = f.sd_hal.files[path];
    TEST_ASSERT_GREATER_THAN(0, content.size());
    TEST_ASSERT_EQUAL_UINT8('R', content[0]);
    TEST_ASSERT_EQUAL_UINT32(0, read_u32_le(content, 4));
}

/**
 * @brief open() fails cleanly when the writer is already open (no
 *        double-open of the session).
 */
void test_open_rejected_while_writer_open()
{
    TestFixture f;

    TEST_ASSERT_TRUE(f.session.open());
    TEST_ASSERT_FALSE(f.session.open());

    TEST_ASSERT_TRUE(f.session.is_open());
    TEST_ASSERT_TRUE(f.session.close_pending());
    TEST_ASSERT_EQUAL_UINT32(1, f.sd_hal.files.size());
}

/**
 * @brief Two opens within the same second uniquify the path instead of
 *        clobbering the first clip.
 */
void test_open_uniquifies_path_collision()
{
    TestFixture f;

    TEST_ASSERT_TRUE(f.session.open());
    const std::string first = f.session.file_path();
    f.session.close();

    TEST_ASSERT_TRUE(f.session.open());
    const std::string second = f.session.file_path();

    TEST_ASSERT_NOT_EQUAL(0, first.compare(second));
    TEST_ASSERT_EQUAL_UINT32(2, f.sd_hal.files.size());
}

/**
 * @brief append_frame() writes the AVI chunk, accumulates the counters,
 *        and stays below the rotation limit.
 */
void test_append_frame_writes_and_counts()
{
    TestFixture f;
    TEST_ASSERT_TRUE(f.session.open());

    for (int i = 0; i < 10; i++)
    {
        f.session.append_frame(f.frame(), FAKE_JPEG_SIZE);
    }

    TEST_ASSERT_EQUAL_UINT32(10, f.session.frames_recorded());
    TEST_ASSERT_GREATER_THAN(0, f.session.file_size());
    TEST_ASSERT_FALSE(f.session.is_close_requested());

    // Chunk bytes reached the clip file.
    const std::vector<uint8_t>& content = f.sd_hal.files[f.session.file_path()];
    TEST_ASSERT_TRUE(content.size() > f.session.file_size() - FAKE_JPEG_SIZE);
    TEST_ASSERT_EQUAL_UINT8(0xAB, content[content.size() - 2]);
}

/**
 * @brief Reaching the duration limit requests clip closure.
 */
void test_rotation_requested_at_duration_limit()
{
    TestFixture f;
    TEST_ASSERT_TRUE(f.session.open());

    for (uint32_t i = 0; i < TEST_FPS * TEST_CLIP_DURATION_S; i++)
    {
        f.session.append_frame(f.frame(), FAKE_JPEG_SIZE);
    }

    TEST_ASSERT_TRUE(f.session.is_close_requested());
}

/**
 * @brief Reaching the file size limit requests clip closure.
 */
void test_rotation_requested_at_file_size_limit()
{
    TestFixture f;
    ConfigData::VideoConfig config;
    config.fps = TEST_FPS;
    strcpy(config.resolution, "VGA");
    config.clip_duration_s = 0;   // No duration limit
    config.max_file_size_mb = 1;  // 1 MB limit
    config.jpeg_quality = 10;
    f.session.init(config, TEST_WIDTH, TEST_HEIGHT);
    TEST_ASSERT_TRUE(f.session.open());

    for (int i = 0; i < 100000 && !f.session.is_close_requested(); i++)
    {
        f.session.append_frame(f.frame(), FAKE_JPEG_SIZE);
    }

    TEST_ASSERT_TRUE(f.session.is_close_requested());
    TEST_ASSERT_FALSE(f.session.frames_recorded() == 0);
}

/**
 * @brief close() finalizes the clip: patched header, appended index,
 *        cleared latch and request, reset counters.
 */
void test_close_finalizes_clip()
{
    TestFixture f;
    TEST_ASSERT_TRUE(f.session.open());
    f.session.append_frame(f.frame(), FAKE_JPEG_SIZE);
    f.session.append_frame(f.frame(), FAKE_JPEG_SIZE);

    f.session.close();

    TEST_ASSERT_FALSE(f.session.is_open());
    TEST_ASSERT_FALSE(f.session.close_pending());
    TEST_ASSERT_FALSE(f.session.is_close_requested());
    TEST_ASSERT_EQUAL_UINT32(0, f.session.frames_recorded());
    TEST_ASSERT_EQUAL_UINT32(0, f.session.file_size());

    // The stored clip is a complete AVI: patched header plus index.
    const std::vector<uint8_t>& content = f.sd_hal.files[f.session.file_path()];
    TEST_ASSERT_TRUE(clip_file_is_patched(content));
    bool found_idx1 = false;
    for (size_t i = 0; i + 4 <= content.size(); i++)
    {
        if (memcmp(&content[i], "idx1", 4) == 0)
        {
            found_idx1 = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(found_idx1);
}

/**
 * @brief consume_close_request() exchanges the flag exactly once.
 */
void test_consume_close_request()
{
    TestFixture f;

    TEST_ASSERT_FALSE(f.session.is_close_requested());
    TEST_ASSERT_FALSE(f.session.consume_close_request());

    f.session.request_close();
    TEST_ASSERT_TRUE(f.session.is_close_requested());
    TEST_ASSERT_TRUE(f.session.consume_close_request());
    TEST_ASSERT_FALSE(f.session.is_close_requested());

    // clear_close_request() also drops a pending request.
    f.session.request_close();
    f.session.clear_close_request();
    TEST_ASSERT_FALSE(f.session.consume_close_request());
}

/**
 * @brief A rotation cycle finalizes clip A and opens clip B under a
 *        fresh path; both end up patched.
 */
void test_rotation_cycle_finalizes_and_reopens()
{
    TestFixture f;
    TEST_ASSERT_TRUE(f.session.open());
    const std::string clip_a = f.session.file_path();
    for (uint32_t i = 0; i < TEST_FPS * TEST_CLIP_DURATION_S; i++)
    {
        f.session.append_frame(f.frame(), FAKE_JPEG_SIZE);
    }

    // Rotation: consume the request, finalize A, open B.
    TEST_ASSERT_TRUE(f.session.consume_close_request());
    f.session.close();
    TEST_ASSERT_TRUE(f.session.open());
    const std::string clip_b = f.session.file_path();
    TEST_ASSERT_NOT_EQUAL(0, clip_a.compare(clip_b));
    TEST_ASSERT_TRUE(f.session.is_open());

    f.session.append_frame(f.frame(), FAKE_JPEG_SIZE);
    TEST_ASSERT_EQUAL_UINT32(1, f.session.frames_recorded());

    f.session.close();

    TEST_ASSERT_EQUAL_UINT32(2, f.sd_hal.files.size());
    TEST_ASSERT_TRUE(clip_file_is_patched(f.sd_hal.files[clip_a]));
    TEST_ASSERT_TRUE(clip_file_is_patched(f.sd_hal.files[clip_b]));
}

/**
 * @brief open() fails when the placeholder header write to SD fails,
 *        without latching close-pending.
 */
void test_open_fails_when_header_write_fails()
{
    TestFixture f;
    f.sd_hal.write_result = false;

    TEST_ASSERT_FALSE(f.session.open());

    TEST_ASSERT_FALSE(f.session.is_open());
    TEST_ASSERT_FALSE(f.session.close_pending());
    TEST_ASSERT_EQUAL_UINT32(0, f.session.frames_recorded());
    TEST_ASSERT_EQUAL_UINT32(0, f.session.file_size());
}

/**
 * @brief The file-size counter counts only bytes actually written, so
 *        a failed frame write does not inflate the reported size.
 */
void test_file_size_counts_only_written_bytes()
{
    TestFixture f;
    TEST_ASSERT_TRUE(f.session.open());

    // Failed write: no bytes counted.
    f.sd_hal.write_result = false;
    f.session.append_frame(f.frame(), FAKE_JPEG_SIZE);
    TEST_ASSERT_EQUAL_UINT32(1, f.session.frames_recorded());
    TEST_ASSERT_EQUAL_UINT32(0, f.session.file_size());

    // Successful write: the counter grows by exactly the bytes added
    // to the stored file (frame chunk including chunk header).
    size_t file_before = f.sd_hal.files[f.session.file_path()].size();
    f.sd_hal.write_result = true;
    f.session.append_frame(f.frame(), FAKE_JPEG_SIZE);
    size_t file_after = f.sd_hal.files[f.session.file_path()].size();
    TEST_ASSERT_EQUAL_UINT32(2, f.session.frames_recorded());
    TEST_ASSERT_EQUAL_UINT32(file_after - file_before,
                             f.session.file_size());
}

/**
 * @brief close() tolerates SD write failures without crashing and still
 *        clears the latch and counters.
 */
void test_close_with_sd_write_failure()
{
    TestFixture f;
    TEST_ASSERT_TRUE(f.session.open());
    f.session.append_frame(f.frame(), FAKE_JPEG_SIZE);

    f.sd_hal.write_result = false;
    f.session.close();

    TEST_ASSERT_FALSE(f.session.is_open());
    TEST_ASSERT_FALSE(f.session.close_pending());
    TEST_ASSERT_EQUAL_UINT32(0, f.session.frames_recorded());
    TEST_ASSERT_EQUAL_UINT32(0, f.session.file_size());
}

/**
 * @brief close() without an open clip is a safe no-op.
 */
void test_close_without_open_clip()
{
    TestFixture f;

    f.session.close();

    TEST_ASSERT_FALSE(f.session.is_open());
    TEST_ASSERT_FALSE(f.session.close_pending());
    TEST_ASSERT_EQUAL_UINT32(0, f.sd_hal.files.size());
}

// ============================================================
// Test runner
// ============================================================

int main()
{
    UNITY_BEGIN();

    RUN_TEST(test_init_accepts_config);
    RUN_TEST(test_open_creates_file_with_placeholder_header);
    RUN_TEST(test_open_rejected_while_writer_open);
    RUN_TEST(test_open_uniquifies_path_collision);
    RUN_TEST(test_append_frame_writes_and_counts);
    RUN_TEST(test_rotation_requested_at_duration_limit);
    RUN_TEST(test_rotation_requested_at_file_size_limit);
    RUN_TEST(test_close_finalizes_clip);
    RUN_TEST(test_consume_close_request);
    RUN_TEST(test_rotation_cycle_finalizes_and_reopens);
    RUN_TEST(test_close_with_sd_write_failure);
    RUN_TEST(test_open_fails_when_header_write_fails);
    RUN_TEST(test_file_size_counts_only_written_bytes);
    RUN_TEST(test_close_without_open_clip);

    return UNITY_END();
}
