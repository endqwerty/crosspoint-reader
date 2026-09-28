#include "HeapCap.h"

#include <cstdlib>
#include <new>

#if defined(__SANITIZE_ADDRESS__)
#define HEAPCAP_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define HEAPCAP_ASAN 1
#endif
#endif

#ifdef HEAPCAP_ASAN
#include <sanitizer/allocator_interface.h>
#endif

namespace {
bool counting = false;
size_t capBytes = SIZE_MAX;
size_t liveBytes = 0;
size_t peakBytes = 0;
size_t allocationCount = 0;
unsigned abortCount = 0, uncontrolledOverrunCount = 0;
size_t firstUncontrolledOverrun = 0;
bool mallocHooksInstalled = false;
size_t firstAbort = 0;
int untrackedDepth = 0;
size_t failNothrowCall = 0, nothrowCount = 0, injectedFailureCount = 0, maxBlock = SIZE_MAX;

// Bookkeeping never allocates and free hooks can release entries after stop().
struct Allocation {
  const volatile void* pointer = nullptr;
  size_t bytes = 0;
};
Allocation allocations[4096];
size_t usedSlots = 0;

void recordAllocation(const volatile void* pointer, const size_t size) {
  size_t slot = 0;
  while (slot < usedSlots && allocations[slot].pointer) ++slot;
  if (slot == sizeof(allocations) / sizeof(allocations[0])) std::abort();
  if (slot == usedSlots) ++usedSlots;
  allocations[slot] = {pointer, size};
  liveBytes += size;
  if (liveBytes > peakBytes) peakBytes = liveBytes;
}

void forgetAllocation(const volatile void* pointer) {
  if (!pointer) return;
  for (size_t slot = 0; slot < usedSlots; ++slot) {
    auto& entry = allocations[slot];
    if (entry.pointer != pointer) continue;
    liveBytes -= entry.bytes;
    entry = {};
    while (usedSlots && !allocations[usedSlots - 1].pointer) --usedSlots;
    return;
  }
}

#ifdef HEAPCAP_ASAN
void observeMalloc(const volatile void* pointer, const size_t size) {
  if (!pointer || !counting || untrackedDepth != 0) return;
  ++allocationCount;
  // These calls cannot be fault-injected or classified as throwing new.
  if (size > heapcap::available() || size > maxBlock) {
    if (uncontrolledOverrunCount++ == 0) firstUncontrolledOverrun = size;
  }
  recordAllocation(pointer, size);
}
#endif

void* allocate(const size_t size, const bool nothrow) {
  const bool count = counting && untrackedDepth == 0;
  if (count) ++allocationCount;
  if (count && nothrow) {
    ++nothrowCount;
    if (failNothrowCall == nothrowCount || size > maxBlock) {
      ++injectedFailureCount;
      return nullptr;
    }
  }
  if (count && (size > heapcap::available() || size > maxBlock)) {
    if (nothrow) return nullptr;
    if (abortCount++ == 0) firstAbort = size;
  }
  ++untrackedDepth;
  void* pointer = std::malloc(size ? size : 1);
  --untrackedDepth;
  if (!pointer) {
    if (nothrow) return nullptr;
    std::abort();
  }
  if (count) recordAllocation(pointer, size);
  return pointer;
}

void release(void* pointer) {
  forgetAllocation(pointer);
  std::free(pointer);
}

}  // namespace

namespace heapcap {
void reset(const size_t cap, const size_t failNothrowAt, const size_t maxAllocation) {
  if (usedSlots != 0) std::abort();
#ifdef HEAPCAP_ASAN
  if (!mallocHooksInstalled) {
    mallocHooksInstalled = __sanitizer_install_malloc_and_free_hooks(observeMalloc, forgetAllocation) != 0;
    if (!mallocHooksInstalled) std::abort();
  }
#endif
  failNothrowCall = failNothrowAt;
  maxBlock = maxAllocation;
  nothrowCount = injectedFailureCount = allocationCount = 0;
  capBytes = cap;
  liveBytes = peakBytes = 0;
  abortCount = uncontrolledOverrunCount = 0;
  firstAbort = firstUncontrolledOverrun = 0;
  counting = true;
}
void stop() { counting = false; }
size_t available() { return liveBytes >= capBytes ? 0 : capBytes - liveBytes; }
size_t live() { return liveBytes; }
size_t peak() { return peakBytes; }
void resetPeak() { peakBytes = liveBytes; }
unsigned aborts() { return abortCount; }
size_t firstAbortSize() { return firstAbort; }
unsigned uncontrolledOverruns() { return uncontrolledOverrunCount; }
size_t firstUncontrolledOverrunSize() { return firstUncontrolledOverrun; }
bool observesMalloc() { return mallocHooksInstalled; }
void observeByte(const char* pointer) { (void)*static_cast<const volatile char*>(pointer); }
size_t allocationCalls() { return allocationCount; }
size_t nothrowCalls() { return nothrowCount; }
size_t injectedFailures() { return injectedFailureCount; }
Untracked::Untracked() { ++untrackedDepth; }
Untracked::~Untracked() { --untrackedDepth; }
}  // namespace heapcap

void* operator new(std::size_t size) { return allocate(size, false); }
void* operator new[](std::size_t size) { return allocate(size, false); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return allocate(size, true); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return allocate(size, true); }
void operator delete(void* p) noexcept { release(p); }
void operator delete[](void* p) noexcept { release(p); }
void operator delete(void* p, std::size_t) noexcept { release(p); }
void operator delete[](void* p, std::size_t) noexcept { release(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { release(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { release(p); }
