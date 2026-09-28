# Fork maintenance: tests and upstream imports

This document covers two maintenance concerns of the X4 Pro reading fork: the
host test policy that guards local changes, and the record of how upstream
CrossPoint `develop` work has been imported, adapted, deferred or rejected.
Repository workflow (linear patch series, rebasing, publication rules) lives in
`docs/FORK.md`; open planning and current release state live in `WIP.md`. Byte
layouts are in `docs/file-formats.md`.

## Host test suite

### Layout and running

- All host tests live under `test/`, one directory per suite, registered with
  `add_subdirectory()` in `test/CMakeLists.txt`. GoogleTest is fetched with
  FetchContent (tag pinned in that file). The build is C++20, warnings
  `-Wall -Wextra -pedantic`, default `Release`.
- `crosspoint_test_common` exposes the repository root and `lib/` as include
  paths. Suites add their own `stubs/` directory for HAL, Arduino, SDK or
  FreeRTOS seams.
- `test/cmake/FirmwareExpat.cmake` builds the vendored `lib/expat` with the
  `XML_GE` and `XML_CONTEXT_BYTES` values read from `platformio.ini`. Configuration
  fails unless each flag is defined exactly once there. Parser tests must link
  `FirmwareExpat`, never a host-installed Expat, and use the real HTML entity table.
- Commands are in `test/README`:
  `cmake -S test -B build/test`, `cmake --build build/test`,
  `ctest --test-dir build/test --output-on-failure --timeout 60 -j 2`.
- CI (`.github/workflows/ci.yml`, job `unit-tests`) runs the suite twice, with
  `CROSSPOINT_TEST_SANITIZERS` `OFF` and `ON`, using Ninja, bounded parallelism
  (`-j 2`) and a 60-second per-test timeout. Bounded parallelism keeps sanitizer
  memory predictable; the timeout catches deadlocks.
- Some suites have their own runners or READMEs: `test/refresh_sequences/run.py`
  (compiles the production FreeInkDisplay and SSD1677/UC8179/UC8279 drivers
  against SDK host stubs, supports `--sanitize`), `test/test_list_viewport.py`,
  `test/sdfat_cache/` (real SdFat 2.3.1 from a hash-pinned archive patched by the
  production hook), and `test/parser_failure/harness` (focused parser harness;
  its cases are also registered in the normal root build).
- The SDK has separate host runners under `freeink-sdk/libs/*/*/test/host/`
  (display `run_pro.py`, `run_uc8279.py`, `run_uc8253_power.py`; FreeInkUI,
  FreeInkFont including GPOS/ligature, InputManager, FreeInkBook). Integrations
  touching display, UI or fonts run them in addition to the reader suite.

### Compiling production code into tests

Tests exercise shipping code, not copied algorithms. Where a whole translation
unit cannot link on the host, the suite extracts complete production functions
at CMake configure time and compiles them against narrow fixtures:

- CMake string extraction by start/end markers, e.g.
  `test/section_persistence/CMakeLists.txt` (Section commit, page-complete,
  build-time load, visible-offset lookup, version constants, temp path).
- Python extractors: `test/file_browser/extract.py` and `test/library_ui/extract.py`
  (brace-balanced method extraction that skips comments and string literals;
  file browser also generates a `StrId` shim for referenced `STR_*` keys),
  `test/sleep_grayscale/extract_functions.py`, `test/sleep_quick_resume/extract.py`.
- Whole-file compilation with hardware seams: `test/epd_bus` compiles the SDK's
  complete `EpdBus.cpp` with virtual GPIO levels, a deterministic virtual clock,
  ISR edges and semaphore tokens; the refresh-sequence runner copies production
  driver sources beside SDK host stubs.

Rules: configuration must fail (`FATAL_ERROR` or raised exception) when a marker
is missing, appears more than once where one is expected, or braces cannot be
balanced, so a moved boundary cannot silently test stale code. Source files are
added to `CMAKE_CONFIGURE_DEPENDS` so edits re-extract. Stubs supply only owned
fields, storage and dependency seams; they must not implement the policy under
test. Real dependencies are linked where possible (LibraryText, SDK ListNav and
FreeInkUI geometry, SectionPageReader, Page/TextBlock serialization).

### Fault injection and failing tests

- Fault-injection tests must prove the fault fired (nonzero fault counter or
  exactly-once injection) and then check that valid committed data survived or
  the failure was reported. A returned boolean alone is not sufficient; tests
  also verify a later clean retry and the integrity of previously committed data.
- A newly discovered production defect stays a normal failing test. Never mark it
  `WILL_FAIL`, disable it, invert it or weaken its expectation to make the suite
  green.
- Parser allocation faults use `ScopedAllocationFailure`
  (`test/chapter_html_slim_parser`, documented in `test/parser_failure/README.md`): it replaces only the executable's nothrow object/array
  `new`, is scoped to the current thread, fails one selected allocation by
  kind/size, and backs successes with the original allocator so matching
  `delete` remains valid. GTest, STL and Expat allocations are not failed, so
  infrastructure failure cannot masquerade as a parser bug. An observation-only
  scope counts allocations so exhaustive loops can fail each checked allocation
  in turn.
- Storage doubles can persist a partial write, fail a seek, and fail both
  install and rollback renames. The host rename refuses an existing destination,
  matching SdFat's `O_CREAT | O_EXCL` behavior. Read errors return `-1` as HalFile
  and both SdFat backends do; zero-byte reads are tested separately as a
  defensive stall case.
- Test-only allocators, vectors and trackers add no firmware heap, stack or
  persistent data.

### Contracts enforced by the fault suites

These came from a critical audit and are fixed in production; the tests stay as
ordinary regressions:

1. Parser allocation failure is an error. A failed TextBlock, text arena, PageLine,
   Page, rule or image allocation latches a one-byte failure state;
   `parseStep()` returns `Error`, `finishParse()` cannot report success, and
   Section refuses to commit (`Section.cpp` checks `finishParse()` and I/O
   failure). A partially consumed paragraph is not retried in place; callers
   discard the build and reparse. Tests compare visible words against a
   fault-free parse (`test/chapter_html_slim_parser`).
2. Page and cache writes are checked, and a failed rebuild never destroys the
   committed cache: footer/header writes and seeks, publication rename and
   append-cursor restore all propagate failure; truncated style/coordinate
   fields and oversize ruby lengths are rejected on decode
   (`test/section_persistence`, `test/epub_page_turn`).
3. Library recovery keeps a valid backup until the live index or its replacement
   validates (`test/library_builder`, `test/library_index_file`).
4. A stuck ActiveHigh BUSY wait or the no-semaphore refresh fallback cannot
   report ready. `EpdBus` waits return `bool`, latch a failure that blocks every
   later SPI entry until reset reaches idle, and the fallback applies the
   delayed-assertion grace. The UC level-based wait has no fixed timeout and
   passes a simulated 45-second wait (`test/epd_bus`).
5. Fixed-size link and footnote fields are serialized as the string plus zero
   padding, so unspecified tail bytes are never persisted (`test/epub_page_turn`).

### Sanitizer builds

`-DCROSSPOINT_TEST_SANITIZERS=ON` adds `-fsanitize=address,undefined
-fno-omit-frame-pointer` to compile and link, including refresh-sequence
subprocesses and the SdFat dependency (no suppressions). It requires GCC or
Clang. Run with `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. Integrations
use LLVM 22 ASan/UBSan locally alongside the native Release build. Sanitizers
find instrumented host memory errors only; they do not model ESP32 heap size,
alignment faults, RTOS scheduling or physical SD/display behavior.

### Heap model and calibration

`test/huge_book_index/HeapCap.{h,cpp}` is a single-threaded host heap model shared
by the huge-book index, EPUB indexing, library staging and library UI tests.

- Replacement `operator new` is counted in every build. Throwing `new` past the
  cap is served but recorded as an abort risk; nothrow `new` can be refused by
  byte cap, largest-block limit or selected call (nth-nothrow injection).
- Under ASan, public sanitizer allocator hooks are installed once (failing closed
  if installation fails) so libc++ and C allocations that bypass replacement
  `new` are also charged. Without this, libc++ string growth on the LLVM/macOS
  host was invisible (zero bytes and zero calls for multi-KiB reservations). The
  backing `malloc` of explicit `new` is excluded from the hook to avoid double
  counting. Frees after counting stops (libc++ teardown, `realloc`, nested
  fixture exclusions) still release tracked blocks. Slot bookkeeping is static,
  never allocates, and scans only the occupied span.
- Hook-observed allocations have already been served and cannot be refused;
  they are reported separately as uninjected budget/block-limit overruns. Abort
  risks, injected refusals and uninjected overruns are distinct categories and
  must not be reported as equivalent simulated failures. Fragmentation injection
  applies to replacement `new` only; raw C-allocator failure is not injected.
- `heapcap::Untracked` excludes fixture storage (fake SD card, fake ZIP) from
  device-heap accounting. `observesMalloc()` reports which accounting mode ran;
  compare peaks only between runs using the same mode.
- Calibration tests (`HeapCapTest.cpp`) read reserved buffers through the
  opaque `observeByte()` probe so optimizers cannot drop them, and require both
  observed bytes and call counts to pass positive controls. Library UI tests use
  this shared counter and keep a cold-buffer positive control and zero-allocation
  warm-loop assertions.
- Large-book budget scenarios (for example 5,000 chapters under a 110 KiB
  budget, `OPEN_HEAP` in `HugeBookIndexTest.cpp`) require the expected outcome or
  refusal path, no abort risk, zero uninjected overruns, cache-byte parity and
  cleanup. At very small block limits (256 or 1,024 bytes) some libc++/C
  allocations are still uninjected, so passing does not prove every C allocation
  fails safely there.

### Coverage boundaries

Host tests do not establish ESP32 peak heap, allocator overhead or fragmentation,
throwing-new/STL failure everywhere, flash-cache-disabled ISR execution, real
FreeRTOS interleavings, SD power-loss or sector-level durability, SD latency,
panel timing, BUSY electrical behavior or optical ghosting. Host `unsigned long`
width differs from ESP32, so millisecond-counter rollover is not covered. Report
host operation counts separately from device measurements, and never present a
host result as a hardware improvement.

## Upstream integration policy

Upstream owns public interfaces, platform support, defaults and interaction
conventions. Local work is a small, removable layer of adaptations on current
upstream interfaces, not an alternate core.

1. Fetch and inspect official `develop` and its SDK pin before touching an
   overlapping area; review existing upstream changes and open PRs before
   choosing an implementation.
2. Prefer the accepted upstream implementation. Retire duplicate local helpers,
   settings and behavior when they conflict. Do not keep private copies of whole
   upstream activities or custom panel waveforms without device evidence.
3. Keep a departure only when a concrete reading or failure-recovery requirement
   justifies it and tests cover it. Revisit every retained departure on the next
   integration. Do not trade failure recovery for a smaller diff unless upstream
   provides equivalent protection.
4. Migrate saved settings explicitly and invalidate incompatible caches instead
   of silently reinterpreting them. When upstream and the fork both use the same
   cache version number for different layouts, the combined layout takes a new
   number so both old variants are rejected and rebuilt. Reading positions,
   bookmarks and Library state keep their own formats across layout bumps.
5. Preserve meaningful tests from the previous revision and new upstream tests.
   Keep an explicit comparison against the upstream test registry and the clean
   upstream tree. A behavior change needs a documented replacement assertion,
   never a skipped test (examples: NFC tests run both string and in-place
   variants; ditherer OOM tests target the single consolidated scratch allocation;
   upstream's leading-word-preservation test replaces the article-stripping test).
6. Keep local features lazy and bounded: no always-resident search index or
   reading background task; failure paths for allocation, read and write; input
   and refresh state transitions tested.
7. Build the X4 Pro image once after the final executable edit and publish only
   after every gate passes against the same source (see `docs/FORK.md`).
   Compatibility with future upstream revisions is a recurring review, not a
   property any build can prove.
8. Local planning does not change the maintainers' `ROADMAP.md` or `SCOPE.md`.
   Avoid parallel implementations while an overlapping upstream PR is under
   review; recheck its status before starting related work.

## Upstream import log

Bases in order: RC02 source `6c83eddbf3feeb375cef20fe702f7c99e5d38703` (SDK
`2cca22fe`), then `develop` at `6f94d1ad` (SDK `13418e09`), `b88b653a` (SDK
`deb62ab7`), `1d61100f` and `4a6283db` (SDK `111fdcc7`). Later bases and the
current SDK patch are recorded in `WIP.md`.

### Adopted as upstream implements it

- Rendering/EPUB: single ownership of rendered blocks (with bounded prefetch
  and layout-cache accounting kept); hidden-content and list-marker parsing, with
  search offsets agreeing on hidden text and generated markers; bookmark
  renaming; EPUB/CSS parsing fixes; progressive JPEG scan fix; the 512-byte
  title/author/language metadata bound (complete UTF-8 kept; three fixed boolean
  guards stop later callbacks resuming a truncated field, no allocation).
- Spacing and typography: word/character spacing controls (#3528), font-table
  deduplication (#3616), per-pixel glyph rendering (#3633; the local bitmap
  helper is retired, raster byte-comparison tests remain), Portuguese
  hyphenation (#3643), Korean word-space justification (#3700), SD-font cache
  fixes and release on reader exit (#3581, #3585, #3699), WordStore with its
  reclaim-and-retry behavior.
- UI and input: list layout, queued selection, measured paging, first/last wrap,
  held-tab switching; File Browser lazy `rowProvider` rows (#3600), NFC filename
  composition (#3630) and Rename (#3572); Home button shortcuts (#3516); footnote
  navigation (#3682); popup sliders (#3669); touch swipe/tap controls (#3586); list
  scrolling (#3668, #3693); Cover Grid theme (#3657, #3670); unified header, status
  band and Back arrow (#3689); RTL tap zones (#3709); landscape toolbar and vector
  font sizes (#3748: every whole point 8-22 for vector fonts, existing limits for
  built-in bitmap and `.cpfont` sizes).
- Platform: TrueType on PSRAM boards (#3646), power/frontlight fixes, image
  scratch allocation consolidation, library refresh markers (#3608), main-loop
  contention fix (#3652; `src/main.cpp` and `RenderLock.h` match upstream, the
  local scheduling patch is retired), unreadable-book wake recovery (#3724: clear
  the remembered book before parsing, restore it only after rendering), Quick
  Resume display polarity and FAST moon refresh, removal of the startup loading
  icon (#3671).
- Display: UC8279 RC02 text-AA waveform restoration, absolute image-bank selection
  and lifecycle resets; UC8179 distinct light/dark gray waveforms (custom overlay
  exception retired); UC8279 X4 whole-screen FAST fallback (custom regional
  window retired). The optional SSD1677 fast-DU shortcut is upstream code and
  stays disabled. The fork adds no waveform bytes or timing shortcuts.
- Library normalization (#3749): leading words are preserved in title and series
  sorting/search; the article-stripping API is removed, not kept as an option.
  `The Earthsea` and `Earthsea` are distinct series.
- Arrival order uses upstream file modification time, with the fork's persistent
  `firstSeen` as tie-break and fallback. The per-book 4-byte timestamp array is
  fallible; on OOM ordering falls back to `firstSeen`.
- Upstream's compact author keys and recent-row identity lookup API are kept.

### Adapted (local behavior layered on upstream)

- Failure handling: checked parser callbacks and cache writes stay fallible
  rather than recording incomplete pages; this extends upstream interfaces while
  accepting upstream spacing settings. If WordStore's retry also fails, the parser
  latches the error before a table or rule can replace the empty block and hide
  the dropped word; neither complete nor partial caches commit.
- Display transactions: BUSY-failure and driver-transaction protections are
  independent of waveform selection. Upstream asynchronous overlay refresh needs a
  settle point before a pushed activity paints: `Activity::onSuspend()` (default
  no-op) runs under the existing render lock, and the EPUB reader settles the
  pending refresh without reacquiring it. Failed completion invalidates the
  optimistic baseline and requests a full redraw; overlay close also settles.
  #3724's remember-once marker is published only after the fork's display-commit
  check, which also guards the end-of-book screen (per-reader scalars only).
- Footnote and dictionary snapshots take the existing render lock and release it
  before navigation or child launch; popup slider input holds it while
  dispatching. The single-footnote shortcut copies its href temporarily rather
  than borrowing from the render task's link vector or truncating it into a fixed
  buffer.
- Settings migration: the old combined "tap always next" mode (4) maps to
  upstream's next-page Tap Only and previous-page Swipe Only
  (`CrossPointSettings.cpp`).
- Sleep images check grayscale capability and fall back to the decoded
  monochrome frame before any unsupported plane upload.
- File Browser: recursive search and opening-key release handling extend
  upstream's flow; Rename also moves bookmark sidecars and favorite/reading state
  with rollback and invalidates the warm Library session. Rename's temporary state
  is heap-owned for the user action only (too large for the task stack).
- Find in Book initializes only when opened; its scan pump defers upstream input
  actions and releases parser/framebuffer ownership before handling Home.
- Library: Recent (reading history) is a separate tab from arrival order.
  Full author identity is a separate helper used for collision-safe grouping at
  build time. Names-only Authors/Series drill-down, Favorites/status filters and
  group counts live inside upstream's header: Back routes through the shared
  handler with drill-down stages, and the right-hand action slot opens Options
  (including Refresh). Local fold revisions force a one-time metadata rebuild
  that keeps `firstSeen` and reading positions. The fork retains the dark boot
  splash independently of upstream Quick Resume.
- Chapter destinations outside the spine are rejected before changing position,
  section ownership or history, so bad TOC data cannot mark a book finished
  (addresses the failure class in upstream issue 3457).

### Deferred or not adopted

- Metadata-sort proposals (#3651 publisher `file-as` ordering, #3707
  language-specific title articles/search): no parallel local implementation
  while under review. Recheck #3695 before redesigning reader controls.
- TrueType on PSRAM boards is adopted as-is, but upstream still has allocations
  that can abort after its preflight check; a measured, upstream-compatible
  reliability fix is separate work. Tests check the non-PSRAM compile guard only.
- Connectivity and media workflows are carried unmodified and not customized.
- Later decisions (adopted, partially adopted, deferred, watch-only and
  rejected PRs) are tracked in `WIP.md`.

## Known gaps

- Library selection is not restored across a full rebuild: row ordinals are not
  stable after reordering, so this needs stable path identity and measured index
  I/O. The state-action fix restores context only while index order is unchanged.
- Host coverage limits above apply to every integration; device checks after an
  import start with anti-aliasing off and cover reading, menus, sleep/wake, cache
  rebuilds and Library views.

## Attribution

Adopted upstream work with human authors as recorded in the fork's integration
notes:

- #3652, avoid main-loop contention: Sung-jin Brian Hong <serialx@serialx.net>
- #3748, landscape toolbar and vector font sizes: Uri Tauber <uritaube@gmail.com>
- #3724, unreadable-book wake recovery: Ninos Yomo
  <32492083+SurayaAtouraya@users.noreply.github.com>
- #3749, preserve leading words in Library sorting/search: Uri Tauber

Other upstream PRs named above, with authors from the upstream commits in the
base history:

- #3633 font rendering overhead, #3528 spacing controls, #3630 NFC rows, #3581 and
  #3585 SD-font fixes, #3699 SD-font release, #3700 Korean justification:
  Sung-jin Brian Hong <serialx@serialx.net>
- #3586 swipe/tap controls, #3689 headers, #3669 popup sliders, #3657 and #3670
  Cover Grid, #3693 touch list scrolling, #3646 TrueType on PSRAM, #3600 File
  Browser rows, #3671 startup icon, Quick Resume polarity and FAST moon refresh:
  Justin Mitchell <justin@jmitch.com>
- #3682 footnote navigation, #3572 rename, #3608 library refresh, #3709 RTL tap
  zones: Uri Tauber <uritaube@gmail.com>
- #3643 Portuguese hyphenation: Mauricio Juba <type0labs.dev@gmail.com>
- #3616 font-table deduplication: Eszter Schuffert
  <30467951+eszter007@users.noreply.github.com>
- #3668 settings list scrolling: Phạm Bình An
  <111893501+brianhuster@users.noreply.github.com>
- #3516 Home button shortcuts, #3575 frontlight state: Julia <julia@uxj.io>
- #3089 X4 Pro frontlight/Home key: julian
  <489233+naydichev@users.noreply.github.com>
- #2332 image heap fragmentation: Leopoldo Pla Sempere
  <leopoldoplasempere@gmail.com>
