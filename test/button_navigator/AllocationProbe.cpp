#include "AllocationProbe.h"

#include <cstdlib>
#include <new>

// Host instrumentation counts requested allocations, not device heap fragmentation.
void* operator new(size_t size) {
  if (countButtonAllocations) {
    ++buttonAllocations;
    buttonAllocationBytes += size;
  }
  void* result = std::malloc(size ? size : 1);
  if (!result) std::abort();
  return result;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }

void* allocateButtonProbe(size_t size) { return ::operator new(size); }
void freeButtonProbe(void* pointer) { ::operator delete(pointer); }
