#pragma once
#include <cstddef>
#include <cstdint>

constexpr uint32_t MALLOC_CAP_8BIT = 4;
// Host strings allocate from the system heap; report ample room.
inline size_t heap_caps_get_largest_free_block(uint32_t) { return 8 * 1024 * 1024; }
