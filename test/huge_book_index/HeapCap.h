#pragma once

// Single-threaded host heap model. Explicit throwing new past the cap is
// reported as an abort risk but served so the test can finish. Nothrow new can
// fail by byte cap, block size or selected call. ASan allocator hooks also charge
// libc++/C allocations that bypass replacement new, reporting uninjected budget
// overruns separately. Native runs count replacement new only. This is not a
// model of ESP32 allocator overhead, fragmentation or peak device heap.
#include <cstddef>
#include <cstdint>

namespace heapcap {

void reset(size_t cap, size_t failNothrowAt = 0,
           size_t maxAllocation = SIZE_MAX);  // start counting with this many bytes available
void stop();                                  // stop counting
size_t available();                           // bytes left under the cap
size_t live();                                // counted bytes currently allocated
size_t peak();                                // high-water mark of live() since reset() or resetPeak()
void resetPeak();                             // restart the high-water mark from live()
unsigned aborts();                            // throwing allocations past the cap
size_t firstAbortSize();
unsigned uncontrolledOverruns();  // observed malloc above the cap; no failure injected
size_t firstUncontrolledOverrunSize();
bool observesMalloc();  // ASan hooks installed; false in native runs
// Opaque read keeps calibration allocations observable to optimizing compilers.
void observeByte(const char* pointer);
size_t allocationCalls();  // observed allocations, excluding fixture storage
size_t nothrowCalls();
size_t injectedFailures();

// Test-side storage (the fake SD card, the fake zip) is not device heap.
struct Untracked {
  Untracked();
  ~Untracked();
  Untracked(const Untracked&) = delete;
  Untracked& operator=(const Untracked&) = delete;
};

}  // namespace heapcap
