#include <unity.h>
#include "hal/request_arg_store.h"

#include <cstdio>
#include <cstring>
#include <string>

// ============================================================
// Tests
// ============================================================

/**
 * @brief Values for different names are retrievable side by side
 *        (a handler holds key while reading value, dir while start).
 */
void test_distinct_names_stay_independent()
{
    hal::RequestArgStore store;

    const char* key = store.put("key", "camera.jpeg_quality");
    const char* value = store.put("value", "63");

    TEST_ASSERT_EQUAL_STRING("camera.jpeg_quality", key);
    TEST_ASSERT_EQUAL_STRING("63", value);

    const char* dir = store.put("dir", "/20260927");
    const char* start = store.put("start", "12");

    TEST_ASSERT_EQUAL_STRING("/20260927", dir);
    TEST_ASSERT_EQUAL_STRING("12", start);
    TEST_ASSERT_EQUAL_STRING("camera.jpeg_quality", key);
    TEST_ASSERT_EQUAL_STRING("63", value);
}

/**
 * @brief Repeated reads of the same name return the same value.
 */
void test_repeated_name_returns_same_value()
{
    hal::RequestArgStore store;

    store.put("dir", "/");
    const char* again = store.put("dir", "/");

    TEST_ASSERT_EQUAL_STRING("/", again);
    TEST_ASSERT_EQUAL_STRING("/", store.get("dir"));
}

/**
 * @brief Re-putting a name refreshes its value without affecting
 *        other names.
 */
void test_put_refreshes_value_in_place()
{
    hal::RequestArgStore store;

    store.put("a", "first");
    const char* other = store.put("b", "other");
    const char* refreshed = store.put("a", "second");

    TEST_ASSERT_EQUAL_STRING("second", refreshed);
    TEST_ASSERT_EQUAL_STRING("other", other);
}

/**
 * @brief Unknown names return an empty string, never a stale value.
 */
void test_unknown_name_returns_empty()
{
    hal::RequestArgStore store;

    store.put("key", "value");

    TEST_ASSERT_EQUAL_STRING("", store.get("missing"));
}

/**
 * @brief Unusable names and null values degrade to stored empties.
 */
void test_null_name_and_value_handled()
{
    hal::RequestArgStore store;

    TEST_ASSERT_EQUAL_STRING("", store.put(nullptr, "value"));

    const char* value = store.put("key", nullptr);
    TEST_ASSERT_NOT_NULL(value);
    TEST_ASSERT_EQUAL_STRING("", value);
}

/**
 * @brief Overlong values are truncated to MAX_VALUE_LEN characters.
 */
void test_overlong_value_truncated()
{
    hal::RequestArgStore store;

    std::string long_value(hal::RequestArgStore::MAX_VALUE_LEN + 50, 'x');
    const char* stored = store.put("key", long_value.c_str());

    TEST_ASSERT_EQUAL_UINT32(hal::RequestArgStore::MAX_VALUE_LEN,
                             strlen(stored));
}

/**
 * @brief Pointers stay valid after many further insertions (storage
 *        must not move when the entry collection grows).
 */
void test_pointers_survive_growth()
{
    hal::RequestArgStore store;

    const char* first = store.put("first", "value-0");
    for (int i = 1; i <= 50; i++)
    {
        char name[16];
        char value[16];
        snprintf(name, sizeof(name), "n%d", i);
        snprintf(value, sizeof(value), "value-%d", i);
        store.put(name, value);
    }

    TEST_ASSERT_EQUAL_STRING("value-0", first);
}

// ============================================================
// Runner
// ============================================================

int run_tests(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_distinct_names_stay_independent);
    RUN_TEST(test_repeated_name_returns_same_value);
    RUN_TEST(test_put_refreshes_value_in_place);
    RUN_TEST(test_unknown_name_returns_empty);
    RUN_TEST(test_null_name_and_value_handled);
    RUN_TEST(test_overlong_value_truncated);
    RUN_TEST(test_pointers_survive_growth);
    return UNITY_END();
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    return run_tests();
}
