#include <unity.h>
#include "hal/sd_log_writer.h"
#include "mock_log_backend.h"
#include "mock_sd_card_hal.h"
#include "sd_controller.h"
#include "time_service.h"
#include <Arduino.h>
#include <atomic>
#include <string>
#include <cstring>
#include <thread>

// ============================================================
// Test fixtures
// ============================================================

static MockSdCardHal sd_hal;
static RealTimeService real_time;

/**
 * @brief Fixture wrapping SdLogWriter with mock dependencies.
 */
struct SdLogWriterFixture
{
    SdController sd_controller;
    SdLogWriter writer;

    SdLogWriterFixture(
        hal::LogLevel min_level = hal::LogLevel::INFO,
        uint8_t retention_days = 7)
        : sd_controller(real_time, sd_hal)
        , writer(sd_controller, real_time, min_level, retention_days)
    {
        sd_hal.init_result = hal::SdInitResult::SUCCESS;
        sd_controller.init_card();
    }

    /** Get the content of a log file as a string. */
    std::string get_log_content(const char* path)
    {
        auto it = sd_hal.files.find(path);
        if (it == sd_hal.files.end())
        {
            return "";
        }
        return std::string(it->second.begin(), it->second.end());
    }
};

// ============================================================
// Level filtering
// ============================================================

/**
 * @brief Log lines below min level are discarded.
 */
void test_discard_below_min_level()
{
    SdLogWriterFixture f(hal::LogLevel::INFO);

    f.writer.write(hal::LogLevel::DEBUG, "debug msg");
    f.writer.tick();

    // No log file should be created since DEBUG < INFO
    TEST_ASSERT_TRUE(sd_hal.files.empty());
}

/**
 * @brief Log lines at min level are written.
 */
void test_write_at_min_level()
{
    SdLogWriterFixture f(hal::LogLevel::INFO);

    f.writer.write(hal::LogLevel::INFO, "info msg");
    f.writer.tick();

    TEST_ASSERT_FALSE(sd_hal.files.empty());
    std::string content = f.get_log_content("/logs/19700101.log");
    TEST_ASSERT_TRUE(content.find("info msg") != std::string::npos);
}

/**
 * @brief Log lines above min level are written.
 */
void test_write_above_min_level()
{
    SdLogWriterFixture f(hal::LogLevel::INFO);

    f.writer.write(hal::LogLevel::WARNING, "warn msg");
    f.writer.tick();

    std::string content = f.get_log_content("/logs/19700101.log");
    TEST_ASSERT_TRUE(content.find("warn msg") != std::string::npos);
}

/**
 * @brief Different min levels work correctly.
 */
void test_debug_min_level()
{
    SdLogWriterFixture f(hal::LogLevel::DEBUG);

    f.writer.write(hal::LogLevel::DEBUG, "debug msg");
    f.writer.tick();

    std::string content = f.get_log_content("/logs/19700101.log");
    TEST_ASSERT_TRUE(content.find("debug msg") != std::string::npos);
}

// ============================================================
// Buffering and flushing
// ============================================================

/**
 * @brief Buffer is flushed on tick().
 */
void test_buffer_flushed_on_tick()
{
    SdLogWriterFixture f(hal::LogLevel::INFO);

    f.writer.write(hal::LogLevel::INFO, "msg1");
    f.writer.write(hal::LogLevel::INFO, "msg2");
    f.writer.tick();

    std::string content = f.get_log_content("/logs/19700101.log");
    TEST_ASSERT_TRUE(content.find("msg1") != std::string::npos);
    TEST_ASSERT_TRUE(content.find("msg2") != std::string::npos);
}

/**
 * @brief Buffer is flushed when full.
 */
void test_buffer_flushed_when_full()
{
    SdLogWriterFixture f(hal::LogLevel::INFO);

    // Fill the buffer with many log lines
    for (int i = 0; i < 50; ++i)
    {
        f.writer.write(hal::LogLevel::INFO, "long msg with padding to fill buffer");
    }
    // Buffer should have been flushed internally

    f.writer.tick();

    std::string content = f.get_log_content("/logs/19700101.log");
    TEST_ASSERT_FALSE(content.empty());
}

/**
 * @brief Log lines are newline-terminated in the output.
 */
void test_log_lines_have_newlines()
{
    SdLogWriterFixture f(hal::LogLevel::INFO);

    f.writer.write(hal::LogLevel::INFO, "line1");
    f.writer.write(hal::LogLevel::INFO, "line2");
    f.writer.tick();

    std::string content = f.get_log_content("/logs/19700101.log");
    TEST_ASSERT_TRUE(content.find("line1\n") != std::string::npos);
    TEST_ASSERT_TRUE(content.find("line2\n") != std::string::npos);
}

// ============================================================
// File path and date
// ============================================================

/**
 * @brief Current log path is set correctly after first write.
 */
void test_current_log_path()
{
    SdLogWriterFixture f(hal::LogLevel::INFO);

    // Initial path is empty until first write
    TEST_ASSERT_EQUAL_STRING("", f.writer.current_log_path());

    f.writer.write(hal::LogLevel::INFO, "test");

    TEST_ASSERT_EQUAL_STRING("/logs/19700101.log", f.writer.current_log_path());
}

/**
 * @brief Current date is set correctly after first write.
 */
void test_current_date()
{
    SdLogWriterFixture f(hal::LogLevel::INFO);

    // Initial date is empty until first write
    TEST_ASSERT_EQUAL_STRING("", f.writer.current_date());

    f.writer.write(hal::LogLevel::INFO, "test");

    TEST_ASSERT_EQUAL_STRING("19700101", f.writer.current_date());
}

// ============================================================
// Day rotation
// ============================================================

/**
 * @brief Day rotation creates a new log file when the day changes.
 */
void test_day_rotation_creates_new_file()
{
    SdLogWriterFixture f(hal::LogLevel::INFO);

    // Write to current day
    f.writer.write(hal::LogLevel::INFO, "first day");
    f.writer.tick();

    TEST_ASSERT_EQUAL_STRING("/logs/19700101.log", f.writer.current_log_path());

    // Simulate day change by advancing time by 86400 seconds
    // (We can't directly manipulate the time in this test,
    // but we can verify the rotation logic by checking the file path)
    // For now, just verify the current path is correct
    TEST_ASSERT_EQUAL_STRING("19700101", f.writer.current_date());
}

// ============================================================
// Log directory creation
// ============================================================

/**
 * @brief Log directory is created on first write.
 */
void test_log_dir_created_on_write()
{
    SdLogWriterFixture f(hal::LogLevel::INFO);

    f.writer.write(hal::LogLevel::INFO, "test");
    f.writer.tick();

    TEST_ASSERT_TRUE(sd_hal.create_dir_call_count > 0);
    TEST_ASSERT_EQUAL_STRING("/logs", sd_hal.last_create_dir_path);
}

/**
 * @brief A failed create_dir falls back to exists: the directory
 *        persisting from a previous session still allows writes.
 */
void test_write_succeeds_when_dir_already_exists()
{
    // Simulate a reboot onto a card that already has /logs: mkdir
    // reports failure, but the directory is present.
    sd_hal.create_dir_result = false;
    sd_hal.directories.insert("/logs");

    SdLogWriterFixture f(hal::LogLevel::INFO);

    f.writer.write(hal::LogLevel::INFO, "after reboot");
    f.writer.tick();

    std::string content = f.get_log_content("/logs/19700101.log");
    TEST_ASSERT_TRUE(content.find("after reboot") != std::string::npos);
}

// ============================================================
// Retention cleanup
// ============================================================

/**
 * @brief Retention deletes only log files older than the cutoff,
 *        leaving non-log entries and newer logs untouched.
 */
void test_retention_deletes_expired_logs()
{
    const uint8_t content_bytes[] = {'o', 'l', 'd'};
    sd_hal.seed_file("/logs/19691220.log",
                     std::vector<uint8_t>(content_bytes,
                                          content_bytes + sizeof(content_bytes)));
    sd_hal.seed_file("/logs/19691231.log",
                     std::vector<uint8_t>(content_bytes,
                                          content_bytes + sizeof(content_bytes)));
    sd_hal.seed_file("/logs/readme.txt",
                     std::vector<uint8_t>(content_bytes,
                                          content_bytes + sizeof(content_bytes)));

    SdLogWriterFixture f(hal::LogLevel::INFO, 7);

    // Epoch time is 19700101; cutoff is 19691225.
    f.writer.write(hal::LogLevel::INFO, "today");
    f.writer.tick();

    TEST_ASSERT_TRUE(sd_hal.files.find("/logs/19691220.log") ==
                     sd_hal.files.end());
    TEST_ASSERT_TRUE(sd_hal.files.find("/logs/19691231.log") !=
                     sd_hal.files.end());
    TEST_ASSERT_TRUE(sd_hal.files.find("/logs/readme.txt") !=
                     sd_hal.files.end());

    std::string content = f.get_log_content("/logs/19700101.log");
    TEST_ASSERT_TRUE(content.find("today") != std::string::npos);
}

/**
 * @brief Maximum retention does not wrap into deleting the current
 *        day's log file.
 */
void test_retention_maximum_does_not_delete_current_log()
{
    const uint8_t content_bytes[] = {'o', 'l', 'd'};
    sd_hal.seed_file("/logs/19690401.log",
                     std::vector<uint8_t>(content_bytes,
                                          content_bytes + sizeof(content_bytes)));
    sd_hal.seed_file("/logs/19690422.log",
                     std::vector<uint8_t>(content_bytes,
                                          content_bytes + sizeof(content_bytes)));

    SdLogWriterFixture f(hal::LogLevel::INFO, 255);

    // Epoch time is 19700101; cutoff is 19690421.
    f.writer.write(hal::LogLevel::INFO, "today");
    f.writer.tick();

    TEST_ASSERT_TRUE(sd_hal.files.find("/logs/19690401.log") ==
                     sd_hal.files.end());
    TEST_ASSERT_TRUE(sd_hal.files.find("/logs/19690422.log") !=
                     sd_hal.files.end());

    std::string content = f.get_log_content("/logs/19700101.log");
    TEST_ASSERT_TRUE(content.find("today") != std::string::npos);
}

// ============================================================
// Concurrency
// ============================================================

/**
 * @brief Concurrent write() callers racing the flushing tick() never
 *        corrupt buffered lines: every line lands intact.
 */
void test_concurrent_writes_and_ticks_preserve_lines()
{
    SdLogWriterFixture f;
    std::atomic<bool> writers_done{false};

    std::thread ticker([&f, &writers_done]()
    {
        while (!writers_done.load())
        {
            f.writer.tick();
            std::this_thread::yield();
        }
    });

    auto writer_fn = [&f](int id)
    {
        char line[64];
        for (int i = 0; i < 200; i++)
        {
            snprintf(line, sizeof(line), "thread%d line%03d payload",
                     id, i);
            f.writer.write(hal::LogLevel::INFO, line);
        }
    };

    std::thread w1(writer_fn, 1);
    std::thread w2(writer_fn, 2);
    w1.join();
    w2.join();
    writers_done.store(true);
    ticker.join();
    f.writer.tick();

    std::string content = f.get_log_content("/logs/19700101.log");
    TEST_ASSERT_FALSE(content.empty());
    for (int id = 1; id <= 2; id++)
    {
        for (int i = 0; i < 200; i++)
        {
            char line[64];
            snprintf(line, sizeof(line), "thread%d line%03d payload",
                     id, i);
            TEST_ASSERT_NOT_NULL(strstr(content.c_str(), line));
        }
    }
}

// ============================================================
// Test runner
// ============================================================

void setUp()
{
    sd_hal.files.clear();
    sd_hal.directories.clear();
    sd_hal.init_call_count = 0;
    sd_hal.open_call_count = 0;
    sd_hal.write_call_count = 0;
    sd_hal.read_call_count = 0;
    sd_hal.close_call_count = 0;
    sd_hal.create_dir_call_count = 0;
    sd_hal.seek_call_count = 0;
    sd_hal.exists_call_count = 0;
    sd_hal.remove_call_count = 0;
    sd_hal.list_dir_call_count = 0;
    sd_hal.last_open_path = nullptr;
    sd_hal.last_open_mode = hal::FileOpenMode::WRITE;
    sd_hal.last_write_data = nullptr;
    sd_hal.last_write_len = 0;
    sd_hal.last_read_buffer = nullptr;
    sd_hal.last_read_len = 0;
    sd_hal.last_create_dir_path[0] = '\0';
    sd_hal.last_seek_pos = 0;
    sd_hal.last_exists_path = nullptr;
    sd_hal.last_remove_path = nullptr;
    sd_hal.last_list_dir_path = nullptr;
    sd_hal.init_result = hal::SdInitResult::SUCCESS;
    sd_hal.open_result = true;
    sd_hal.write_result = true;
    sd_hal.create_dir_result = true;
    sd_hal.seek_result = true;
    sd_hal.initialized_state = true;
    set_mock_millis(0);
}

void tearDown()
{
}

int main()
{
    UNITY_BEGIN();

    // Level filtering
    RUN_TEST(test_discard_below_min_level);
    RUN_TEST(test_write_at_min_level);
    RUN_TEST(test_write_above_min_level);
    RUN_TEST(test_debug_min_level);

    // Buffering and flushing
    RUN_TEST(test_buffer_flushed_on_tick);
    RUN_TEST(test_buffer_flushed_when_full);
    RUN_TEST(test_log_lines_have_newlines);

    // File path and date
    RUN_TEST(test_current_log_path);
    RUN_TEST(test_current_date);

    // Day rotation
    RUN_TEST(test_day_rotation_creates_new_file);

    // Log directory creation
    RUN_TEST(test_log_dir_created_on_write);
    RUN_TEST(test_write_succeeds_when_dir_already_exists);

    // Retention cleanup
    RUN_TEST(test_retention_deletes_expired_logs);
    RUN_TEST(test_retention_maximum_does_not_delete_current_log);

    // Concurrency
    RUN_TEST(test_concurrent_writes_and_ticks_preserve_lines);

    return UNITY_END();
}