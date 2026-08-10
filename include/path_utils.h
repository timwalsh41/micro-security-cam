#ifndef PATH_UTILS_H
#define PATH_UTILS_H

#include <cstddef>
#include <cstdint>
#include <functional>

/**
 * @file path_utils.h
 * @brief Unique file path generation helpers.
 *
 * Provides suffix-based collision avoidance for SD card file names,
 * decoupled from any specific storage layer via an exists callback.
 */

/**
 * @class PathUtils
 * @brief Generates non-colliding file paths by appending serial suffixes.
 *
 * Given a base path, appends `_1`, `_2`, ... before the file extension
 * until a non-existent path is found, e.g. `photo.jpg` → `photo_1.jpg`.
 */
class PathUtils
{
    public:
        /**
         * @brief Callback that reports whether a path already exists.
         *
         * @param path The candidate path to check.
         * @return true if the path exists.
         */
        using ExistsFn = std::function<bool(const char*)>;

        /** Maximum number of suffix attempts before giving up. */
        static constexpr uint32_t MAX_SUFFIX_ATTEMPTS = 100;

        /**
         * @brief Finds a non-existent path by appending serial suffixes.
         *
         * If @p base_path does not exist, it is copied to @p out as-is.
         * Otherwise suffixes `_1`, `_2`, ... are inserted before the file
         * extension until a non-existent path is found or the attempt cap
         * is exceeded.
         *
         * @param base_path The base path (e.g. `/dir/photo.jpg`).
         * @param exists    Callback returning true if a path exists.
         * @param out       Output buffer for the unique path.
         * @param out_size  Size of the output buffer.
         * @return true if a unique path was written to @p out, false on
         *         invalid arguments, buffer overflow, or cap exceeded.
         */
        static bool make_unique_path(const char* base_path, ExistsFn exists,
                                     char* out, size_t out_size);
};

#endif // PATH_UTILS_H
