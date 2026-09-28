# Production BUSY-wait fault tests

This target compiles the whole SDK `EpdBus.cpp` and its actual header. It models
GPIO levels, elapsed time, the binary semaphore and ISR edges at the hardware
boundary. The SDK's wait logic is neither copied nor replaced. The translation
unit resets the file-static semaphore between fixtures to simulate fresh boots;
board hooks are defined only to satisfy Mach-O linkage, and power/reset/SPI
electrical behavior is outside this harness.

Delays advance a deterministic virtual clock, so a 45-second panel wait costs
milliseconds on the host. Scheduled level changes can generate ISR tokens.
Coverage includes UC idle/long waits, begin/end/slice hooks, delayed assertion,
stale tokens, semaphore allocation failure, and stuck ActiveHigh command/refresh
waits. Every CTest case has a real ten-second timeout to contain a regression
that accidentally stops advancing time.

The existing recorded-driver tests replace the bus and count commands/payloads.
They still test driver sequencing; this target covers the bus implementation
that those tests omit. Neither harness proves physical ghosting or full task/ISR
concurrency. Host `unsigned long` width also differs from the ESP32, so these
cases deliberately do not claim coverage of millisecond-counter wraparound.

The three failing fault contracts are intentional discoveries, not expected
failures marked as passes. They remain enabled in the default suite:

- No-semaphore fallback returns before a delayed ActiveHigh refresh assertion.
- ActiveHigh command and ISR refresh waits silently finish while BUSY is stuck.

The current wait API returns void and exposes no error result; checking whether
BUSY is still active detects that unsafe return. A future explicit failure API
must update these contracts to require an error result plus suppression of
subsequent driver writes, rather than requiring an impossible idle pin. The UC
level-based path has no fixed timeout and passes the extended-busy tests.
