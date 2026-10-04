#include "log_level_parser.h"
#include <strings.h>

namespace log_level
{
    hal::LogLevel parse(const char* name)
    {
        if (name != nullptr)
        {
            if (strcasecmp(name, "debug") == 0)
            {
                return hal::LogLevel::DEBUG;
            }
            if (strcasecmp(name, "warn") == 0 ||
                strcasecmp(name, "warning") == 0)
            {
                return hal::LogLevel::WARNING;
            }
            if (strcasecmp(name, "error") == 0)
            {
                return hal::LogLevel::ERROR;
            }
            if (strcasecmp(name, "info") == 0)
            {
                return hal::LogLevel::INFO;
            }
        }
        return hal::LogLevel::INFO;
    }
}
