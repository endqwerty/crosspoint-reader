#pragma once

#include <cstddef>

#include "../../gfx_refresh/stubs/Arduino.h"

// ComplexShaper reads heap headroom before it allocates shaping state.
struct TtfTestEsp {
  size_t getFreeHeap() const { return 1u << 20; }
  size_t getMaxAllocHeap() const { return 1u << 20; }
};
inline TtfTestEsp ESP;
