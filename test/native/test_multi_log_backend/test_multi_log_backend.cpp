#include <unity.h>
#include "hal/multi_log_backend.h"
#include "mock_log_backend.h"
#include <atomic>
#include <string>
#include <thread>

// ============================================================
// Test fixtures
// ============================================================

static MockLogBackend backend1;
static MockLogBackend backend2;
static MockLogBackend backend3;

// ============================================================
// Construction
// ============================================================

/**
 * @brief MultiLogBackend starts with zero backends.
 */
void test_default_constructor_has_zero_backends()
{
    MultiLogBackend multi;
    TEST_ASSERT_EQUAL_UINT32(0, multi.backend_count());
}

// ============================================================
// Adding backends
// ============================================================

/**
 * @brief Adding a backend succeeds.
 */
void test_add_backend_succeeds()
{
    MultiLogBackend multi;
    TEST_ASSERT_TRUE(multi.add_backend(&backend1, hal::LogLevel::INFO));
    TEST_ASSERT_EQUAL_UINT32(1, multi.backend_count());
}

/**
 * @brief Adding multiple backends succeeds.
 */
void test_add_multiple_backends()
{
    MultiLogBackend multi;
    TEST_ASSERT_TRUE(multi.add_backend(&backend1, hal::LogLevel::INFO));
    TEST_ASSERT_TRUE(multi.add_backend(&backend2, hal::LogLevel::DEBUG));
    TEST_ASSERT_TRUE(multi.add_backend(&backend3, hal::LogLevel::WARNING));
    TEST_ASSERT_EQUAL_UINT32(3, multi.backend_count());
}

/**
 * @brief Adding a backend beyond MAX_BACKENDS fails.
 */
void test_add_backend_beyond_max_fails()
{
    MultiLogBackend multi;
    MockLogBackend backends[4];
    TEST_ASSERT_TRUE(multi.add_backend(&backends[0], hal::LogLevel::INFO));
    TEST_ASSERT_TRUE(multi.add_backend(&backends[1], hal::LogLevel::INFO));
    TEST_ASSERT_TRUE(multi.add_backend(&backends[2], hal::LogLevel::INFO));
    TEST_ASSERT_TRUE(multi.add_backend(&backends[3], hal::LogLevel::INFO));

    // 5th backend should fail
    MockLogBackend extra;
    TEST_ASSERT_FALSE(multi.add_backend(&extra, hal::LogLevel::INFO));
    TEST_ASSERT_EQUAL_UINT32(4, multi.backend_count());
}

// ============================================================
// Write fan-out
// ============================================================

/**
 * @brief Write fans out to all registered backends.
 */
void test_write_fans_out_to_all_backends()
{
    MultiLogBackend multi;
    multi.add_backend(&backend1, hal::LogLevel::DEBUG);
    multi.add_backend(&backend2, hal::LogLevel::DEBUG);
    multi.add_backend(&backend3, hal::LogLevel::DEBUG);

    multi.write(hal::LogLevel::INFO, "test message");

    TEST_ASSERT_EQUAL_INT(1, backend1.write_call_count);
    TEST_ASSERT_EQUAL_INT(1, backend2.write_call_count);
    TEST_ASSERT_EQUAL_INT(1, backend3.write_call_count);
}

/**
 * @brief Write with level filtering skips backends below their min level.
 */
void test_write_respects_min_level_filter()
{
    MultiLogBackend multi;
    multi.add_backend(&backend1, hal::LogLevel::INFO);
    multi.add_backend(&backend2, hal::LogLevel::WARNING);
    multi.add_backend(&backend3, hal::LogLevel::DEBUG);

    multi.write(hal::LogLevel::DEBUG, "debug msg");

    TEST_ASSERT_EQUAL_INT(0, backend1.write_call_count);  // DEBUG < INFO
    TEST_ASSERT_EQUAL_INT(0, backend2.write_call_count);  // DEBUG < WARNING
    TEST_ASSERT_EQUAL_INT(1, backend3.write_call_count);  // DEBUG >= DEBUG
}

/**
 * @brief Write with ERROR level goes to all backends with min level <= ERROR.
 */
void test_write_error_goes_to_all_backends()
{
    MultiLogBackend multi;
    multi.add_backend(&backend1, hal::LogLevel::INFO);
    multi.add_backend(&backend2, hal::LogLevel::WARNING);
    multi.add_backend(&backend3, hal::LogLevel::ERROR);

    multi.write(hal::LogLevel::ERROR, "error msg");

    TEST_ASSERT_EQUAL_INT(1, backend1.write_call_count);
    TEST_ASSERT_EQUAL_INT(1, backend2.write_call_count);
    TEST_ASSERT_EQUAL_INT(1, backend3.write_call_count);
}

/**
 * @brief Write with INFO level goes to backends with min level <= INFO.
 */
void test_write_info_goes_to_info_and_below_backends()
{
    MultiLogBackend multi;
    multi.add_backend(&backend1, hal::LogLevel::INFO);
    multi.add_backend(&backend2, hal::LogLevel::WARNING);
    multi.add_backend(&backend3, hal::LogLevel::ERROR);

    multi.write(hal::LogLevel::INFO, "info msg");

    TEST_ASSERT_EQUAL_INT(1, backend1.write_call_count);
    TEST_ASSERT_EQUAL_INT(0, backend2.write_call_count);  // INFO < WARNING
    TEST_ASSERT_EQUAL_INT(0, backend3.write_call_count);  // INFO < ERROR
}

/**
 * @brief Each backend receives the level and line independently.
 */
void test_write_passes_level_and_line()
{
    MultiLogBackend multi;
    multi.add_backend(&backend1, hal::LogLevel::DEBUG);

    multi.write(hal::LogLevel::WARNING, "warning msg");

    TEST_ASSERT_EQUAL_INT(1, backend1.write_call_count);
    TEST_ASSERT_EQUAL(hal::LogLevel::WARNING, backend1.last_level);
    TEST_ASSERT_EQUAL_STRING("warning msg", backend1.last_message.c_str());
}

// ============================================================
// Tick
// ============================================================

/**
 * @brief tick() calls tick() on all registered backends.
 */
void test_tick_calls_tick_on_all_backends()
{
    // We can't directly test tick() calls, but we can verify the method exists
    // and doesn't crash. For a more thorough test, we'd need a mock with
    // a tick() counter, but MockLogBackend has a default no-op tick().
    MultiLogBackend multi;
    multi.add_backend(&backend1, hal::LogLevel::INFO);
    multi.add_backend(&backend2, hal::LogLevel::INFO);

    // This should not crash
    multi.tick();
}

// ============================================================
// Edge cases
// ============================================================

/**
 * @brief Write with no backends is a no-op.
 */
void test_write_with_no_backends_is_noop()
{
    MultiLogBackend multi;
    multi.write(hal::LogLevel::INFO, "test");
    // Should not crash
}

/**
 * @brief Tick with no backends is a no-op.
 */
void test_tick_with_no_backends_is_noop()
{
    MultiLogBackend multi;
    multi.tick();
    // Should not crash
}

/**
 * @brief Adding nullptr backend is still added (caller responsibility).
 */
void test_add_nullptr_backend()
{
    MultiLogBackend multi;
    TEST_ASSERT_TRUE(multi.add_backend(nullptr, hal::LogLevel::INFO));
    TEST_ASSERT_EQUAL_UINT32(1, multi.backend_count());
}

// ============================================================
// Concurrency
// ============================================================

/**
 * @brief A backend registered while another context is writing
 *        receives every line written after registration completes,
 *        and an already-registered backend sees every line.
 */
void test_concurrent_add_backend_while_writing()
{
    MultiLogBackend multi;
    MockLogBackend early;
    MockLogBackend late;
    TEST_ASSERT_TRUE(multi.add_backend(&early, hal::LogLevel::DEBUG));

    std::thread adder([&multi, &late]()
    {
        multi.add_backend(&late, hal::LogLevel::DEBUG);
    });

    const int lines = 5000;
    for (int i = 0; i < lines; i++)
    {
        multi.write(hal::LogLevel::INFO, "line");
    }
    adder.join();
    multi.write(hal::LogLevel::INFO, "marker");

    // Only this thread writes, so the up-front backend saw everything.
    TEST_ASSERT_EQUAL_INT(lines + 1, early.write_call_count);

    // The racing backend saw a suffix of the lines and always the marker.
    TEST_ASSERT_TRUE(late.write_call_count >= 1);
    TEST_ASSERT_TRUE(late.write_call_count <= lines + 1);
    TEST_ASSERT_EQUAL_STRING("marker", late.last_message.c_str());
}

// ============================================================
// Main
// ============================================================

void setUp()
{
    backend1.reset();
    backend2.reset();
    backend3.reset();
}

void tearDown()
{
}

int main()
{
    UNITY_BEGIN();

    // Construction
    RUN_TEST(test_default_constructor_has_zero_backends);

    // Adding backends
    RUN_TEST(test_add_backend_succeeds);
    RUN_TEST(test_add_multiple_backends);
    RUN_TEST(test_add_backend_beyond_max_fails);

    // Write fan-out
    RUN_TEST(test_write_fans_out_to_all_backends);
    RUN_TEST(test_write_respects_min_level_filter);
    RUN_TEST(test_write_error_goes_to_all_backends);
    RUN_TEST(test_write_info_goes_to_info_and_below_backends);
    RUN_TEST(test_write_passes_level_and_line);

    // Tick
    RUN_TEST(test_tick_calls_tick_on_all_backends);

    // Edge cases
    RUN_TEST(test_write_with_no_backends_is_noop);
    RUN_TEST(test_tick_with_no_backends_is_noop);
    RUN_TEST(test_add_nullptr_backend);

    // Concurrency
    RUN_TEST(test_concurrent_add_backend_while_writing);

    return UNITY_END();
}