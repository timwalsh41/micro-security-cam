#ifndef HAL_MEMORY_ALLOCATOR_ESP32_H
#define HAL_MEMORY_ALLOCATOR_ESP32_H

#include "memory_allocator.h"

/**
 * @class MemoryAllocatorEsp32
 * @brief ESP32 PSRAM-aware memory allocator.
 *
 * Allocates memory from PSRAM when available, falling back to
 * regular heap if PSRAM is not available.
 */
class MemoryAllocatorEsp32 : public IMemoryAllocator
{
public:
    void* alloc(size_t size) override;
    void free(void* ptr) override;
};

#endif // HAL_MEMORY_ALLOCATOR_ESP32_H