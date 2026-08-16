#include <unity.h>
#include "path_utils.h"
#include <cstring>
#include <set>
#include <string>

// ============================================================
// Test helpers
// ============================================================

/**
 * @brief Builds an ExistsFn backed by a set of existing paths.
 *
 * @param existing Set of paths that should report as existing.
 * @return ExistsFn matching the set.
 */
static PathUtils::ExistsFn make_exists_fn(const std::set<std::string>& existing)
{
    return [&existing](const char* path) {
        return existing.find(path) != existing.end();
    };
}

// ============================================================
// make_unique_path
// ============================================================

/**
 * @brief A free base path is returned as-is.
 */
void test_no_collision_returns_base()
{
    char out[64] = {0};
    std::set<std::string> existing;

    TEST_ASSERT_TRUE(
        PathUtils::make_unique_path("/dir/photo.jpg", make_exists_fn(existing),
                                    out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/dir/photo.jpg", out);
}

/**
 * @brief A colliding base path gets a _1 suffix before the extension.
 */
void test_first_collision_gets_suffix_1()
{
    char out[64] = {0};
    std::set<std::string> existing = {"/dir/photo.jpg"};

    TEST_ASSERT_TRUE(
        PathUtils::make_unique_path("/dir/photo.jpg", make_exists_fn(existing),
                                    out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/dir/photo_1.jpg", out);
}

/**
 * @brief A collision chain advances to the next free suffix.
 */
void test_collision_chain_gets_next_free()
{
    char out[64] = {0};
    std::set<std::string> existing = {"/dir/photo.jpg",
                                      "/dir/photo_1.jpg"};

    TEST_ASSERT_TRUE(
        PathUtils::make_unique_path("/dir/photo.jpg", make_exists_fn(existing),
                                    out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/dir/photo_2.jpg", out);
}

/**
 * @brief Paths without an extension get the suffix appended directly.
 */
void test_no_extension_gets_suffix()
{
    char out[64] = {0};
    std::set<std::string> existing = {"/dir/photo"};

    TEST_ASSERT_TRUE(
        PathUtils::make_unique_path("/dir/photo", make_exists_fn(existing),
                                    out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/dir/photo_1", out);
}

/**
 * @brief A dot in a directory name does not split the extension.
 */
void test_dot_in_directory_not_treated_as_extension()
{
    char out[64] = {0};
    std::set<std::string> existing = {"/dir.v2/photo.jpg"};

    TEST_ASSERT_TRUE(
        PathUtils::make_unique_path("/dir.v2/photo.jpg", make_exists_fn(existing),
                                    out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/dir.v2/photo_1.jpg", out);
}

/**
 * @brief An exists callback returning nullptr behaves as "never exists".
 */
void test_null_exists_fn_returns_base()
{
    char out[64] = {0};

    TEST_ASSERT_TRUE(
        PathUtils::make_unique_path("/dir/photo.jpg", nullptr,
                                    out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/dir/photo.jpg", out);
}

/**
 * @brief Exceeding the suffix attempt cap returns false.
 */
void test_cap_exceeded_returns_false()
{
    char out[64] = {0};
    // Every candidate collides — all MAX_SUFFIX_ATTEMPTS fail
    std::set<std::string> existing;
    for (uint32_t i = 0; i <= PathUtils::MAX_SUFFIX_ATTEMPTS; i++)
    {
        existing.insert(std::string("/dir/photo_") + std::to_string(i));
    }
    // Also include the extensionless base itself
    existing.insert("/dir/photo");

    TEST_ASSERT_FALSE(
        PathUtils::make_unique_path("/dir/photo", make_exists_fn(existing),
                                    out, sizeof(out)));
}

/**
 * @brief Null arguments are rejected.
 */
void test_null_args_rejected()
{
    char out[64] = {0};
    std::set<std::string> existing;

    TEST_ASSERT_FALSE(PathUtils::make_unique_path(nullptr, make_exists_fn(existing),
                                                  out, sizeof(out)));
    TEST_ASSERT_FALSE(
        PathUtils::make_unique_path("/dir/photo.jpg", make_exists_fn(existing),
                                    nullptr, sizeof(out)));
    TEST_ASSERT_FALSE(PathUtils::make_unique_path("/dir/photo.jpg",
                                                  make_exists_fn(existing),
                                                  out, 0));
}

/**
 * @brief An output buffer too small for the base path is rejected.
 */
void test_out_too_small_rejected()
{
    char out[4] = {0};
    std::set<std::string> existing;

    TEST_ASSERT_FALSE(
        PathUtils::make_unique_path("/dir/photo.jpg", make_exists_fn(existing),
                                    out, sizeof(out)));
}

// ============================================================
// extract_parent_dir
// ============================================================

/**
 * @brief Returns the directory portion of a nested path.
 */
void test_extract_parent_dir_nested()
{
    char out[64];
    TEST_ASSERT_TRUE(PathUtils::extract_parent_dir(
        "/20260213/20260213_123456.jpg", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/20260213", out);
}

/**
 * @brief Returns the directory portion of a doubly-nested path.
 */
void test_extract_parent_dir_deep_nested()
{
    char out[64];
    TEST_ASSERT_TRUE(PathUtils::extract_parent_dir(
        "/2026/02/13/photo.jpg", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/2026/02/13", out);
}

/**
 * @brief Returns false for a path directly under the root.
 *
 * "/photo.jpg" has no intermediate parent directory to create, so
 * no directory path is returned.
 */
void test_extract_parent_dir_root_level()
{
    char out[64];
    TEST_ASSERT_FALSE(PathUtils::extract_parent_dir(
        "/photo.jpg", out, sizeof(out)));
}

/**
 * @brief Returns false for a bare filename with no slash.
 */
void test_extract_parent_dir_bare_filename()
{
    char out[64];
    TEST_ASSERT_FALSE(PathUtils::extract_parent_dir(
        "photo.jpg", out, sizeof(out)));
}

/**
 * @brief Returns false on a too-small output buffer.
 */
void test_extract_parent_dir_buffer_too_small()
{
    char out[4];
    TEST_ASSERT_FALSE(PathUtils::extract_parent_dir(
        "/20260213/20260213_123456.jpg", out, sizeof(out)));
}

/**
 * @brief Returns false on null arguments.
 */
void test_extract_parent_dir_null_args()
{
    char out[64];
    TEST_ASSERT_FALSE(PathUtils::extract_parent_dir(nullptr, out, sizeof(out)));
    TEST_ASSERT_FALSE(PathUtils::extract_parent_dir("/a/b.jpg", nullptr, sizeof(out)));
    TEST_ASSERT_FALSE(PathUtils::extract_parent_dir("/a/b.jpg", out, 0));
}

// ============================================================
// Test runner
// ============================================================

int main()
{
    UNITY_BEGIN();

    RUN_TEST(test_no_collision_returns_base);
    RUN_TEST(test_first_collision_gets_suffix_1);
    RUN_TEST(test_collision_chain_gets_next_free);
    RUN_TEST(test_no_extension_gets_suffix);
    RUN_TEST(test_dot_in_directory_not_treated_as_extension);
    RUN_TEST(test_null_exists_fn_returns_base);
    RUN_TEST(test_cap_exceeded_returns_false);
    RUN_TEST(test_null_args_rejected);
    RUN_TEST(test_out_too_small_rejected);

    // extract_parent_dir
    RUN_TEST(test_extract_parent_dir_nested);
    RUN_TEST(test_extract_parent_dir_deep_nested);
    RUN_TEST(test_extract_parent_dir_root_level);
    RUN_TEST(test_extract_parent_dir_bare_filename);
    RUN_TEST(test_extract_parent_dir_buffer_too_small);
    RUN_TEST(test_extract_parent_dir_null_args);

    return UNITY_END();
}
