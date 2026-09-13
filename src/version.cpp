#include "version.h"
#include <cstdio>
#include <cstring>


namespace version
{
    bool get_version_string(char* buffer, size_t buffer_size)
    {
        size_t version_len = strlen(base_version_string) + strlen(git_hash) + 1;
        if (version_len >= buffer_size)
            return false;

        return snprintf(buffer, buffer_size, "%s-%s", base_version_string, git_hash) >= 0;
    }
}