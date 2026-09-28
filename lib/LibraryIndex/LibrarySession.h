#pragma once

#include <atomic>
#include <cstdint>

namespace library {
// Reconcile once after boot and after known card mutations. Explicit refresh
// covers files copied while the device remains powered on.
class LibrarySession {
 public:
  bool needsRefresh(bool validIndex, bool matchingMetadata) const {
    return requested.load(std::memory_order_relaxed) != completed.load(std::memory_order_relaxed) || !validIndex ||
           !matchingMetadata;
  }
  uint32_t refreshToken() const { return requested.load(std::memory_order_relaxed); }
  void invalidate() { requested.fetch_add(1, std::memory_order_relaxed); }
  void reconciled(bool success, uint32_t token) {
    if (success) completed.store(token, std::memory_order_relaxed);
  }

 private:
  std::atomic<uint32_t> requested{1};
  std::atomic<uint32_t> completed{0};
};
inline LibrarySession librarySession;
}  // namespace library
