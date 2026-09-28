#pragma once

#include <cstddef>
#include <memory>
#include <utility>

#include "Page.h"
#include "PagePrefetchPolicy.h"

// Owns an already-decoded page; retaining it allocates no second copy.
// Access is serialized by the reader's RenderLock and scoped to one Section.
class PrefetchedPageCache {
 public:
  static constexpr size_t MAX_PAGE_BUDGET = 32 * 1024;
  static constexpr size_t MIN_FREE_HEAP = PagePrefetchPolicy::MIN_FREE_HEAP;
  static constexpr size_t MIN_MAX_ALLOC = PagePrefetchPolicy::MIN_MAX_ALLOC;

  bool retain(const int pageNumber, std::unique_ptr<Page> decoded, const size_t freeHeap, const size_t largestBlock) {
    clear();
    if (pageNumber < 0 || !decoded || !PagePrefetchPolicy::hasHeadroom(freeHeap, largestBlock) ||
        decoded->cacheBudgetBytes() > MAX_PAGE_BUDGET) {
      return false;
    }
    page = std::move(decoded);
    index = pageNumber;
    return true;
  }

  std::unique_ptr<Page> take(const int pageNumber) {
    if (pageNumber != index) {
      clear();
      return nullptr;
    }
    index = -1;
    return std::move(page);
  }

  void clear() {
    page.reset();
    index = -1;
  }

  void releaseIfLowMemory(const size_t freeHeap, const size_t largestBlock) {
    if (!PagePrefetchPolicy::hasHeadroom(freeHeap, largestBlock)) clear();
  }

 private:
  std::unique_ptr<Page> page;
  int index = -1;
};
