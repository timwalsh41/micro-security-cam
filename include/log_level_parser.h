#ifndef LOG_LEVEL_PARSER_H
#define LOG_LEVEL_PARSER_H

#include "hal/log_backend.h"

/**
 * @file log_level_parser.h
 * @brief Maps configured log-level names to LogLevel values.
 *
 * Platform-agnostic parsing of a configured severity name, shared by
 * any composition root that wires a log sink from configuration.
 */

namespace log_level
{
    /**
     * @brief Maps a configured level name to a LogLevel.
     *
     * Matching is case-insensitive. A nullptr name or an unrecognized
     * name defaults to INFO.
     *
     * @param name Configured level name, may be nullptr.
     * @return The matching LogLevel, or INFO when the name is null or
     *         unrecognized.
     */
    hal::LogLevel parse(const char* name);
}

#endif // LOG_LEVEL_PARSER_H
