#pragma once

// Raster fixtures do not model device heap pressure. Font allocations use the
// SDK host allocator; renderer sink registration is inert in these fixtures.
#include <cstddef>
#include <functional>

namespace freeink {
enum class MemPool : unsigned char { Internal, Psram, Default };
struct CacheSink {
  const char* name;
  unsigned char priority;
  std::function<size_t(size_t)> evict;
};
class MemoryManager {
 public:
  static MemoryManager& instance() {
    static MemoryManager manager;
    return manager;
  }
  int registerSink(const CacheSink&) { return -1; }
  size_t freeBytes(MemPool = MemPool::Default) const { return 1024 * 1024; }
  bool ensureFree(size_t, MemPool = MemPool::Default) { return true; }
};
}  // namespace freeink
