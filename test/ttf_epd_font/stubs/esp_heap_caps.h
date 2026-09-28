#pragma once
#include <cstddef>
#include <cstdint>

constexpr uint32_t MALLOC_CAP_SPIRAM = 1;
constexpr uint32_t MALLOC_CAP_INTERNAL = 2;
constexpr uint32_t MALLOC_CAP_8BIT = 4;
// Control the adapter's preflight gate, not the host allocator or real PSRAM.
inline size_t ttfTestAvailableHeap = 8 * 1024 * 1024;
inline size_t heap_caps_get_largest_free_block(uint32_t) { return ttfTestAvailableHeap; }
inline size_t heap_caps_get_free_size(uint32_t) { return ttfTestAvailableHeap; }
