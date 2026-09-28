#pragma once

#include <cstdint>
#include <cstring>

// Benchmark timing uses steady_clock around composition. The firmware's own
// instrumentation clock is inert, so per-glyph clock reads do not skew it.
inline unsigned long millis() { return 0; }
inline unsigned long micros() { return 0; }
