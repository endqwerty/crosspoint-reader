#include "HostAllocations.h"

#include <array>
#include <cstdlib>
#include <new>

namespace epub_page_test {
AllocationCounts allocations;
bool captureAllocations = false;
const void* watchedAllocation = nullptr;
bool watchedFreed = false;
void watchDeallocation(const void* pointer) {
  watchedAllocation = pointer;
  watchedFreed = false;
}
bool watchedAllocationWasFreed() { return watchedFreed; }
namespace {
bool trackNothrow = false;
size_t failNothrowCall = 0;
size_t nothrowCalls = 0;
std::array<void*, 64> nothrowPointers{};
}  // namespace
NothrowAllocationScope::NothrowAllocationScope(size_t failOnCall) {
  failNothrowCall = failOnCall;
  nothrowCalls = 0;
  nothrowPointers.fill(nullptr);
  trackNothrow = true;
}
NothrowAllocationScope::~NothrowAllocationScope() { trackNothrow = false; }
size_t NothrowAllocationScope::attempts() const { return nothrowCalls; }
size_t NothrowAllocationScope::outstanding() const {
  size_t count = 0;
  for (auto* pointer : nothrowPointers) count += pointer != nullptr;
  return count;
}
}  // namespace epub_page_test
namespace {
void* allocate(size_t size) noexcept {
  if (epub_page_test::captureAllocations) {
    ++epub_page_test::allocations.calls;
    epub_page_test::allocations.bytes += size;
  }
  return std::malloc(size ? size : 1);
}
void* allocateNothrow(size_t size) noexcept {
  using namespace epub_page_test;
  if (!trackNothrow) return allocate(size);
  if (++nothrowCalls == failNothrowCall) return nullptr;
  auto* result = allocate(size);
  if (result) {
    for (auto& pointer : nothrowPointers) {
      if (pointer) continue;
      pointer = result;
      return result;
    }
    // A bounded host-only tracker: fixture growth must not silently hide leaks.
    std::abort();
  }
  return result;
}
void deallocate(void* pointer) noexcept {
  if (pointer && pointer == epub_page_test::watchedAllocation) epub_page_test::watchedFreed = true;
  if (epub_page_test::trackNothrow) {
    for (auto& tracked : epub_page_test::nothrowPointers) {
      if (tracked == pointer) tracked = nullptr;
    }
  }
  std::free(pointer);
}
}  // namespace
void* operator new(size_t size) {
  if (void* p = allocate(size)) return p;
  std::abort();
}
void* operator new[](size_t size) { return ::operator new(size); }
void* operator new(size_t size, const std::nothrow_t&) noexcept { return allocateNothrow(size); }
void* operator new[](size_t size, const std::nothrow_t&) noexcept { return allocateNothrow(size); }
void operator delete(void* p) noexcept { deallocate(p); }
void operator delete[](void* p) noexcept { deallocate(p); }
void operator delete(void* p, size_t) noexcept { deallocate(p); }
void operator delete[](void* p, size_t) noexcept { deallocate(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { deallocate(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { deallocate(p); }
