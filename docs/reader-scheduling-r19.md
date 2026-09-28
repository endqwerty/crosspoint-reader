# r19: upstream-aligned input and render scheduling

Based on CrossPoint develop `1d61100f90d2e7e32965c14301320d9989a7ab71`
and FreeInk SDK `111fdcc7f0176c3ee38391a160ee296bf492dbd8`. No newer develop
commit was present when this work began. Upstream Phase 1 prioritizes footprint,
reader-core cleanup and driver refinement; this release addresses the first two.

## Reviewed upstream work

Adapted from Sung-jin Brian Hong's proposals:

- [PR 3698](https://github.com/crosspoint-reader/crosspoint-reader/pull/3698),
  `21ff2fde859290e1759cd918a219aa16dc6ef6fa`: replace temporary ButtonNavigator
  vectors with synchronously consumed initializer-list views and static constexpr
  backing lists for returned directions. The production patch is adopted intact.
- [PR 3652](https://github.com/crosspoint-reader/crosspoint-reader/pull/3652),
  `0f1b86eeba28542fc2f5d5e6025806166203d307`: use nonblocking render-lock acquisition
  before reading background-work state and the main-loop scheduling hint.
  The local TryAcquire interface is retired in favor of the proposal's Mode/ownsLock
  interface. Our combined build tick retains pending navigation and error recovery.

Both proposals were open and awaiting maintainer review, not merged upstream.
Their human author was verified from the GitHub commit records as
`Sung-jin Brian Hong <serialx@serialx.net>`; retain that co-author credit if these
adaptations are later committed. This local release does not imply upstream acceptance.

The SD-font memory rewrite in PR 3705 remains deferred: its draft explicitly reports
a cold-layout regression with varying-width fonts. Metadata sorting remains with
upstream's pending file-as/language proposals. No competing font or catalog design
is added here.

## Behavior and resource effects

Button presses, repeats, releases and logical mappings remain unchanged. Removing
the temporary vectors removes their allocator calls; it does not establish a
reduction in fragmentation or measured user-visible latency. The returned lists
have static lifetime, and caller-provided brace lists are never retained. Existing
callback types stay intact; large std::function captures can still allocate.

The main loop now tries the render lock once before asking whether pagination has
work. If rendering owns it, the loop sleeps for 10 ms without reading section
state or changing power mode. Otherwise it reads the hint under the lock, releases
the lock, and uses the existing yield/idle policy. The existing short input polling
slices during longer idle sleep remain. Background build work also acquires before
reading state, and its scheduling predicate is shared with the hint. Heap admission
still runs in the build tick so a paused build can recover.

No task, mutex, priority change, heap allocation, cache format or display waveform
is introduced. The lock object retains its existing ownership flag. The two small
constexpr button lists replace repeated temporary allocations. The build's static
RAM/flash report is separate from device peak-heap behavior.

## Verification

The render-lock tests compile the production lock implementation and extract the
actual main-loop scheduling tail. They cover busy/successful acquisition, lock
release before power/yield, unchanged idle/input delays, and 1,000 consecutive
contended polls followed by recovery. Incremental reader tests exercise the real
build orchestration and scheduling predicate for contention, window limits,
explicit navigation, heap pause/resume and existing failure cases.

The ButtonNavigator suite compiles the complete production implementation. A
positive control verifies the allocation hook. With identical host instrumentation,
10,000 idle polls made 40,000 allocations requesting 160,000 bytes in the prior
implementation; the new implementation is required to make zero. Clang's normal
optimizer elided the baseline vector allocations, so these checks explicitly keep
replacement operator new observable. These are instrumented host counts, not device
CPU timings, live heap, or physical page-turn measurements. Candidate measurements
and the expected baseline failure are retained in the release verification folder.

All previous test names remain required. Final release additionally requires the
full native and LLVM22 ASan/UBSan suites, SDK checks, scoped static analysis and
X4 Pro build/image inspection. Gate logs establish the actual results.

On device, initially with anti-aliasing off: open an uncached/long chapter, turn
forward/back while pagination runs, jump near its end and return. Check held-button
repeats and release behavior on Home/menus, then sleep/wake and resume reading.
No recordings are needed. Existing Library features, cache formats, bookmarks and
positions remain. No physical speed or ghosting improvement is claimed by host tests.
