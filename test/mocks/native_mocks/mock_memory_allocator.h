#ifndef MOCK_MEMORY_ALLOCATOR_H
#define MOCK_MEMORY_ALLOCATOR_H

#include "memory_allocator.h"
#include <atomic>

/**
 * @file mock_memory_allocator.h
 * @brief Native mock of IMemoryAllocator for unit tests.
 *
 * Uses standard new/delete for memory allocation. Provides
 * call tracking for test assertions.
 */

class MockMemoryAllocator : public IMemoryAllocator
{
public:
    MockMemoryAllocator() = default;

    ~MockMemoryAllocator() override = default;

    void* alloc(size_t size) override
    {
        m_alloc_call_count++;
        m_total_allocated += size;
        return ::operator new(size);
    }

    void free(void* ptr) override
    {
        m_free_call_count++;
        ::operator delete(ptr);
    }

    // ============================================================
    // Call tracking
    // ============================================================

    /** Number of times alloc() has been called. */
    int m_alloc_call_count{0};

    /** Number of times free() has been called. */
    int m_free_call_count{0};

    /** Total bytes allocated across all alloc() calls. */
    std::atomic<size_t> m_total_allocated{0};
};

#endif // MOCK_MEMORY_ALLOCATOR_H