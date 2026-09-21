#include "hal/memory_allocator_esp32.h"

#include <esp_heap_caps.h>

/**
 * @file memory_allocator_esp32.cpp
 * @brief ESP32 implementation of IMemoryAllocator using PSRAM.
 */

void* MemoryAllocatorEsp32::alloc(size_t size)
{
    void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (ptr == nullptr)
    {
        ptr = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    }
    return ptr;
}

void MemoryAllocatorEsp32::free(void* ptr)
{
    if (ptr != nullptr)
    {
        heap_caps_free(ptr);
    }
}