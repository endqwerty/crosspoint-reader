#pragma once

#include <cstddef>

struct PagePrefetchPolicy {
  static constexpr size_t MIN_FREE_HEAP = 80 * 1024;
  static constexpr size_t MIN_MAX_ALLOC = 32 * 1024;

  static constexpr bool hasHeadroom(const size_t freeHeap, const size_t largestBlock) {
    return freeHeap >= MIN_FREE_HEAP && largestBlock >= MIN_MAX_ALLOC;
  }
};
