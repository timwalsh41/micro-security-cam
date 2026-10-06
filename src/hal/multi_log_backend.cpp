#include "hal/multi_log_backend.h"
#include <cstring>

// ============================================================
// Construction
// ============================================================

MultiLogBackend::MultiLogBackend()
    : m_backends{}
    , m_min_levels{}
    , m_backend_count(0)
{
}

// ============================================================
// Public interface
// ============================================================

bool MultiLogBackend::add_backend(hal::LogBackend* backend, hal::LogLevel min_level)
{
    std::lock_guard<std::mutex> lock(m_registry_mutex);
    if (m_backend_count >= MAX_BACKENDS)
    {
        return false;
    }

    m_backends[m_backend_count] = backend;
    m_min_levels[m_backend_count] = min_level;
    m_backend_count++;
    return true;
}

void MultiLogBackend::write(hal::LogLevel level, const char* line)
{
    // Snapshot under the lock, fan out outside it: a backend added
    // concurrently either misses this line or receives it, and no
    // child backend's I/O runs while the registry lock is held.
    std::array<hal::LogBackend*, MAX_BACKENDS> backends;
    std::array<hal::LogLevel, MAX_BACKENDS> min_levels;
    size_t count;
    {
        std::lock_guard<std::mutex> lock(m_registry_mutex);
        backends = m_backends;
        min_levels = m_min_levels;
        count = m_backend_count;
    }

    for (size_t i = 0; i < count; ++i)
    {
        if (level >= min_levels[i] && backends[i] != nullptr)
        {
            backends[i]->write(level, line);
        }
    }
}

void MultiLogBackend::tick()
{
    std::array<hal::LogBackend*, MAX_BACKENDS> backends;
    size_t count;
    {
        std::lock_guard<std::mutex> lock(m_registry_mutex);
        backends = m_backends;
        count = m_backend_count;
    }

    for (size_t i = 0; i < count; ++i)
    {
        if (backends[i] != nullptr)
        {
            backends[i]->tick();
        }
    }
}

size_t MultiLogBackend::backend_count() const
{
    std::lock_guard<std::mutex> lock(m_registry_mutex);
    return m_backend_count;
}
