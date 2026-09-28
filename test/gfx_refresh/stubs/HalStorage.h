#pragma once

#include <cstddef>

// This rendering-only harness never opens a storage handle.
class HalFile {
 public:
  size_t position() const { return 0; }
  size_t size() const { return 0; }
};
