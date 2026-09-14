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
    for (size_t i = 0; i < m_backend_count; ++i)
    {
        if (level >= m_min_levels[i])
        {
            m_backends[i]->write(level, line);
        }
    }
}

void MultiLogBackend::tick()
{
    for (size_t i = 0; i < m_backend_count; ++i)
    {
        m_backends[i]->tick();
    }
}

size_t MultiLogBackend::backend_count() const
{
    return m_backend_count;
}