#ifndef VERSION_H
#define VERSION_H

#include <cstddef>

namespace version
{
    constexpr const char* base_version_string = "v0.0.1";
    constexpr const char* git_hash = "77b0bb";

    bool get_version_string(char* buffer, size_t buffer_size);
}

#endif // VERSION_H