# Shared heap-test calibration after r36

This is a host-test change on develop `93e98bb78702e29868a16a13b80c40e6b36ccdff`
with SDK `111fdcc7f0176c3ee38391a160ee296bf492dbd8`. Firmware source is unchanged
from r36. The intended flash image remains firmware-x4pro-epub-r36-final.bin.

## Confirmed gap and repair

The shared HeapCap model only observed replacement operator new. On the LLVM22
ASan/macOS host, string growth inside libc++ bypassed those replacements. Two
new calibration tests reproduce the blind spot against the saved r36 helper:
reserving over 4 KiB reports zero allocations and zero live bytes, as does growth
to over 8 KiB. Explicit new tests still passed, so testing only those entry points
would miss the problem.

ASan runs now install the public sanitizer allocator hooks once, failing closed
if hook installation fails. These observe libc++ and C allocation/free events.
Explicit new retains the existing cap, largest-block and nth-nothrow injection;
its backing malloc is excluded from the hook to avoid double counting. Frees
release tracked allocations after counting stops, including libc++ destruction,
realloc and frees inside nested fixture exclusions. Slot bookkeeping is static,
never allocates, and searches only the occupied span. This is a single-threaded
host fixture, not firmware or a general-purpose allocator.

The library UI tests now use this shared calibrated counter instead of maintaining
a separate ASan-only counter. Their existing cold-buffer positive control and
zero-allocation warm-loop assertions remain required.

## Evidence boundaries

Native runs count replacement new; ASan additionally observes C/libc++ allocation
calls. The model reports those scopes explicitly. A direct throwing-new overrun
is recorded as an abort risk; a nothrow refusal is injected. Hook-only allocations
are already served by the time they are observed, cannot be refused by this
injector, and are reported separately as uninjected budget/block-limit overruns.
These categories must not be presented as equivalent simulated failures.

Large-book total-budget scenarios now require zero uninjected overruns, in addition
to their existing outcomes, no-abort checks, cache-byte parity and cleanup checks.
Fragmentation injection remains limited to replacement new. The sanitizer probe
reports nine uninjected oversized calls at a 256-byte block limit and three at a
1,024-byte limit; zero at 4,096 and 16,384. Passing that test does not establish
that every C allocation can fail safely at those smallest limits. Targeted raw
allocator failure coverage remains a separate follow-up.

Calibration string buffers are read through an opaque volatile-byte probe so
optimizing native compilers cannot discard an otherwise unused reservation.
Both observed bytes and allocation counts must pass their positive controls.

Thirteen calibration tests cover direct and libc++ allocation visibility, string
growth, budget and block limits, injected/natural nothrow refusals, nested fixture
exclusions, C allocation scope, realloc/free, peak reset, and zero-byte allocation
cleanup. The original large-book and library UI tests remain required.

The 5,000-chapter sanitizer fixture still builds and reloads with its 110 KiB
synthetic budget; larger fixtures either build or take the existing refusal path.
This does not measure ESP32 heap, allocator overhead, fragmentation or SD/panel
latency. The broader sanitizer accounting changes measured host peaks; compare
results only with the accounting mode identified.

## Verification and firmware

Run the complete native Release and LLVM22 ASan/UBSan registries, including every
r36 test name and the 13 new calibration tests. Run the retained display, UI, font,
viewport and real-storage host checks. Preserve source fingerprints, baseline
failures and final test logs in build/test-audit-after-r36.

Do not rebuild or rename firmware for this test-only change. Verify that every
production source and the existing r36 BIN/hash and FLASH-LATEST.md remain unchanged.
No new on-device action is required by this audit; use the r36 flash instructions.
