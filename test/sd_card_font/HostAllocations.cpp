#include "HostAllocations.h"

#include <cstdio>
#include <cstdlib>
#include <new>

SdFontTestAllocations sdFontTestAllocations;
size_t failNextArraySize = 0;

// Host-only hook: firmware array allocations use nothrow new. It records requested
// bytes, not live heap or allocator overhead. No ESP32 fragmentation is simulated.
void* operator new[](size_t size, const std::nothrow_t&) noexcept {
  ++sdFontTestAllocations.attempts;
  sdFontTestAllocations.requestedBytes += size;
  if (failNextArraySize != 0 && size == failNextArraySize) {
    failNextArraySize = 0;
    return nullptr;
  }
  return std::malloc(size == 0 ? 1 : size);
}

// Keep new[]/delete[] paired, including arrays created by the host test framework.
void* operator new[](size_t size) {
  void* allocation = std::malloc(size == 0 ? 1 : size);
  if (!allocation) {
    std::fputs("Unexpected host OOM in SdCardFont fixture\n", stderr);
    std::exit(EXIT_FAILURE);
  }
  return allocation;
}
void operator delete[](void* allocation) noexcept { std::free(allocation); }
void operator delete[](void* allocation, size_t) noexcept { std::free(allocation); }
void operator delete[](void* allocation, const std::nothrow_t&) noexcept { std::free(allocation); }
