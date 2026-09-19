#ifndef MULTI_LOG_BACKEND_H
#define MULTI_LOG_BACKEND_H

#include "hal/log_backend.h"
#include <array>
#include <cstddef>
#include <cstdint>

/**
 * @file multi_log_backend.h
 * @brief Composite LogBackend that fans out to multiple child backends.
 *
 * Supports up to MAX_BACKENDS child backends, each with its own
 * minimum severity level. This allows independent verbosity
 * control per sink (e.g., serial vs SD card).
 */

/**
 * @class MultiLogBackend
 * @brief Extensible composite LogBackend with per-backend level filtering.
 *
 * Fans out each log line to all registered backends, filtering by
 * each backend's configured minimum severity level. Designed for
 * future extensibility (e.g., web API buffered logs, push endpoints).
 */
class MultiLogBackend : public hal::LogBackend
{
    public:
        /**
         * @brief Maximum number of child backends.
         */
        static constexpr size_t MAX_BACKENDS = 4;

        /**
         * @brief Constructs an empty MultiLogBackend.
         */
        MultiLogBackend();

        /**
         * @brief Add a child backend with a minimum severity level.
         *
         * @param backend   Pointer to the child LogBackend.
         * @param min_level Minimum severity level for this backend.
         * @return true if the backend was added successfully (space available),
         *         false if the maximum number of backends was reached.
         */
        bool add_backend(hal::LogBackend* backend, hal::LogLevel min_level);

        /**
         * @brief Write a log line to all registered backends.
         *
         * Filters each backend by its minimum severity level.
         *
         * @param level The severity level of the log message.
         * @param line  The formatted log line to output. Null-terminated.
         */
        void write(hal::LogLevel level, const char* line) override;

        /**
         * @brief Call tick() on all registered backends.
         *
         * This is used to flush buffered data from backends that
         * support it (e.g., SdLogWriter).
         */
        void tick() override;

        /**
         * @brief Returns the number of registered backends.
         *
         * @return Number of backends.
         */
        size_t backend_count() const;

    private:
        /**
         * @brief Array of child backends.
         */
        std::array<hal::LogBackend*, MAX_BACKENDS> m_backends;

        /**
         * @brief Minimum severity levels for each backend.
         */
        std::array<hal::LogLevel, MAX_BACKENDS> m_min_levels;

        /**
         * @brief Number of registered backends.
         */
        size_t m_backend_count;
};

#endif // MULTI_LOG_BACKEND_H