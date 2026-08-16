#include "path_utils.h"
#include <cstdio>
#include <cstring>

// ============================================================
// PathUtils
// ============================================================

bool PathUtils::make_unique_path(const char* base_path, ExistsFn exists,
                                 char* out, size_t out_size)
{
    if (base_path == nullptr || out == nullptr || out_size == 0)
    {
        return false;
    }

    const size_t base_len = strlen(base_path);
    if (base_len >= out_size)
    {
        return false;
    }

    // The base path is free — use it as-is
    if (exists == nullptr || !exists(base_path))
    {
        strcpy(out, base_path);
        return true;
    }

    // Split off the extension (after the last '.') so the serial suffix
    // is inserted before it, e.g. "photo.jpg" → "photo_1.jpg"
    const char* dot = strrchr(base_path, '.');
    size_t stem_len = base_len;
    const char* ext = "";
    if (dot != nullptr && dot != base_path)
    {
        stem_len = static_cast<size_t>(dot - base_path);
        ext = dot;
    }

    for (uint32_t i = 1; i <= MAX_SUFFIX_ATTEMPTS; i++)
    {
        int written = snprintf(out, out_size, "%.*s_%u%s",
                               static_cast<int>(stem_len), base_path, i, ext);
        if (written < 0 || static_cast<size_t>(written) >= out_size)
        {
            return false;
        }
        if (exists == nullptr || !exists(out))
        {
            return true;
        }
    }

    return false;
}

bool PathUtils::extract_parent_dir(const char* path, char* out, size_t out_size)
{
    if (path == nullptr || out == nullptr || out_size == 0)
    {
        return false;
    }

    // Find the last '/' in the path.
    const char* last_slash = strrchr(path, '/');
    if (last_slash == nullptr || last_slash == path)
    {
        // No slash (bare filename) or root-level path ("/file") — no parent dir.
        return false;
    }

    size_t dir_len = static_cast<size_t>(last_slash - path);
    if (dir_len >= out_size)
    {
        return false;
    }

    memcpy(out, path, dir_len);
    out[dir_len] = '\0';
    return true;
}
