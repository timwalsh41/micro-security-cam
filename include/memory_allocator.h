#ifndef MEMORY_ALLOCATOR_H
#define MEMORY_ALLOCATOR_H

#include <cstddef>

/**
 * @class IMemoryAllocator
 * @brief Abstract interface for memory allocation.
 *
 * Provides memory allocation/deallocation that may use PSRAM
 * on ESP32 or standard heap on native tests.
 */
class IMemoryAllocator
{
public:
    virtual ~IMemoryAllocator() = default;

    /**
     * @brief Allocates memory (potentially PSRAM on ESP32).
     * @param size Number of bytes to allocate.
     * @return Pointer to allocated memory, or nullptr on failure.
     */
    virtual void* alloc(size_t size) = 0;

    /**
     * @brief Frees memory previously allocated by alloc().
     * @param ptr Pointer to memory to free.
     */
    virtual void free(void* ptr) = 0;
};

#endif // MEMORY_ALLOCATOR_H