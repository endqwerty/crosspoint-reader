#pragma once
#include <cstddef>

namespace epub_page_test {
struct AllocationCounts {
  size_t calls = 0;
  size_t bytes = 0;
};
extern AllocationCounts allocations;
extern bool captureAllocations;
void watchDeallocation(const void* pointer);
bool watchedAllocationWasFreed();

// Inject failure only into production's explicit nothrow allocation boundary.
// Throwing allocations made by GoogleTest and STL fixtures are not affected.
class NothrowAllocationScope {
 public:
  explicit NothrowAllocationScope(size_t failOnCall);
  ~NothrowAllocationScope();
  NothrowAllocationScope(const NothrowAllocationScope&) = delete;
  NothrowAllocationScope& operator=(const NothrowAllocationScope&) = delete;
  size_t attempts() const;
  size_t outstanding() const;
};
}  // namespace epub_page_test
