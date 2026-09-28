# Page turning and ghosting

## Design constraints

CrossPoint prioritizes legibility, bounded memory, and portable reader logic
([SCOPE.md](../SCOPE.md)). Display-controller policy belongs in the HAL/SDK;
reader changes should use existing capabilities. This work retains the current
refresh-frequency setting, anti-aliasing setting, cleanup waveforms, and
single-framebuffer contract.

The X4 Pro ships with SSD1677, UC8179, and UC8279 display controllers. A gain on
one controller is not evidence of a gain on another. The SDK already includes
SPI plane batching and corrected cleanup behavior at revision
`c881d219b05520d42ff93c20c2bf3a4c401c64fe`.

Relevant upstream decisions:

- [SDK #38](https://github.com/Free-Ink/freeink-sdk/pull/38) keeps the SSD1677
  fast-DU shortcut opt-in. It skips temperature and power sequencing, with
  reported ghosting and blotching on some panels. It remains disabled here.
- [SDK #56](https://github.com/Free-Ink/freeink-sdk/pull/56) documents why HALF
  must perform cleanup rather than behave like FAST. The maintainer's fixes
  are already included in the SDK revision above.
- [SDK #91](https://github.com/Free-Ink/freeink-sdk/pull/91) explores regional
  refresh for sparse updates. Its reported panel timings do not establish a
  large benefit for full reading-page changes.
- [Reader #2179](https://github.com/crosspoint-reader/crosspoint-reader/pull/2179)
  illustrates the need to distinguish CPU rendering time from visible page-turn
  time: review measured about 65–70 ms less CPU work with about 664 ms of panel
  refresh remaining. Those measurements were on the contributor's hardware.

## Reader changes

### Accepted upstream font-cache fixes

[Reader #3521](https://github.com/crosspoint-reader/crosspoint-reader/pull/3521)
(`c80c537f287506dbac4f99f0b97a89cffbe36302`) preserves freshly prefetched SD-font
glyphs until drawing, replaces obsolete page glyph sets after a complete page
scan, and releases temporary allocations before growing a bitmap arena.
Incremental UI strings retain their existing accumulating cache behavior.

[Reader #3527](https://github.com/crosspoint-reader/crosspoint-reader/pull/3527)
(`c4d8c395dca44cbefc0e9d7bf16f1ac469ec6819`) releases rebuildable font caches
before chapter layout allocations. Both were authored by Sung-jin Brian Hong
<serialx@serialx.net> and approved by Justin Mitchell. Preserve the original
author's attribution if committing this integration.

These changes add no persistent buffers. Existing temporary mapping and read
order arrays use checked RAII allocations and are freed earlier. Fragmentation
recovery can reread metadata and rebuild width tables; chapter transitions may
need to warm fonts again. Upstream measured improvement on X3 backward turns,
with ordinary cached forward turns essentially unchanged. That is not an
X4 Pro timing measurement.

### Whole-plane grayscale composition

The buffered anti-aliasing path already allocates one or two complete planes,
subject to free-heap and largest-block headroom checks. It now uses each buffer
as one full-height render target, instead of walking the page again for each
80-row band. A 480-row panel needs one traversal per plane instead of six.
Glyphs crossing old strip boundaries also avoid repeated decoding.

This changes CPU work, not the waveform duration. It adds no allocations or
persistent buffers. The existing 80-row scratch path remains the fallback when
full planes do not fit. The B/W framebuffer stays intact while the panel is
busy. Capability checks restrict this optimization to the buffered overlap
path; UC8179/UC8279 X4 Pro drivers currently use a different path.

### Honor the control panel's ghost-cleanup request

The control panel requests a one-shot FULL refresh of the underlying screen.
Both grayscale-base entry points now honor that request, as ordinary B/W
refreshes already did. If an explicit grayscale mode is unsupported, the
request remains pending so the B/W fallback can perform cleanup.

This fixes an ignored cleanup request, particularly on non-overlapping
anti-aliased reader paths. It does not change the normal cleanup interval or
claim to eliminate every source of physical panel ghosting.

### Skip unavailable grayscale in night mode

The SDK reports grayscale as unsupported while inverted
(`FreeInkDisplay.cpp:573-575`). EPUB and TXT now use that capability before
selecting their grayscale path. Previously they still backed up the B/W
framebuffer and rendered two gray passes whose SDK uploads were discarded.

`ReaderGrayscalePlan` centralizes this small decision without allocating memory.
For an X4 Pro inverted page it avoids the 48,000-byte temporary B/W backup and
two gray render passes. The B/W drawing, user AA preference, image content and
scheduled cleanup remain intact. Supported day-mode paths retain their existing
capability selection, including the low-memory strip fallback.

## Automated measurement

Kindle responsiveness is a direction for this work, not a required device
comparison. Recordings are not part of the acceptance criteria. The automated
checks measure unnecessary rendering, SD traffic, framebuffer correctness and
the transition data supplied to the display controller.

### Real-font composition

[The renderer harness](../test/gfx_refresh/README.md) links the real compressed
Noto Serif font, shaping, prewarm cache, decompressor and rasterizer. Forty-eight
cases compare full-height and legacy 80-row composition byte for byte across
four orientations, two grayscale encodings, three scenes and cold/prewarmed
caches. Both gray planes must match and the B/W framebuffer must remain intact.
Scenes include four font styles, multilingual text, combining marks, ligatures,
clipping, overlapping and rotated text, and decoration primitives.

The optional benchmark alternates approaches over repeated samples and exports
median host CPU time plus actual bitmap-fetch/decompression counters. Two planes
take 12 page traversals with the old method and 2 with full-height composition.
Band culling already skips many off-screen glyphs, so the traversal reduction
does not imply a sixfold reduction in bitmap work or overall page-turn time.
Prewarmed fixtures require zero decompression misses in both approaches.

### SD-font cache behavior

[The SD-font harness](../test/sd_card_font/README.md) executes production
`SdCardFont` with instrumented storage and allocation stubs. The benchmark
compiles the current source and an explicit baseline revision against the same
fixture. It records opens, reads, seeks, bytes, requested glyphs and allocations;
each foreground glyph must have the correct bytes.

For successive disjoint 100-glyph pages, the old accumulating cache reads
14,544, 28,944, 43,344 and 57,744 bytes; the page-scoped cache stays at 14,544
bytes per page. On the fourth page, that removes about 75% of prefetch traffic.
Warm foreground drawing performs no storage reads in either version. Repeated
older sparse subsets expose the deliberate tradeoff: bounded current-page
retention can reread glyphs the accumulating cache still holds. The benchmark
includes that case and does not claim a universal speedup.

### Refresh sequences and cleanup

[The sequence harness](../test/refresh_sequences/README.md) compiles the actual
SDK facade and X4 Pro drivers against a recording bus. It runs 5,000 synthetic
page submissions across SSD1677, UC8179 and three UC8279 variants in single-
and dual-buffer configurations. Workloads cover B/W, asynchronous ownership,
repeated AA, AA-to-B/W transitions, periodic/manual cleaning and night toggles.

The oracle checks all 48,000 visible bytes, controller padding, previous-frame
baselines, HALF/FULL clean seeds, AA plane encoding, and activation counts.
An overlay AA page must have one base and one gray activation; plane uploads
and RAM cleanup must add none. Separate renderer tests verify that a pending
manual FULL request reaches the grayscale base and survives unsupported-mode
fallback. Five reader policy tests cover supported capabilities and night mode.

These checks protect known causes of ghosting: stale transition baselines,
lost cleanup requests, and incorrect plane contents. They measure command
correctness, not optical residue or panel response time. No synthetic ink model
or guessed ghosting percentage is used.

## Recorded turn-r2 results

The macOS arm64 Release host run used 21 samples per renderer case. Prewarmed
composition was 1.71–2.03 times faster across the 12 scene/orientation cases:

| Portrait scene | Legacy composition | Full-height composition |
| --- | ---: | ---: |
| Prose | 675.1 microseconds | 385.1 microseconds |
| Multilingual | 625.7 microseconds | 331.0 microseconds |
| Clipping/overlap | 821.0 microseconds | 414.9 microseconds |

These times cover the two gray planes only, excluding prewarm, initial B/W
render, SPI and panel BUSY time. Cold-font results are recorded separately;
they are not used as the headline for the normally prewarmed reader.

All 386 host tests passed, including the 5,000-submission driver harness. The
X4 Pro release build passed with 100,312 bytes of static RAM, unchanged from
Library r4 and turn-r1. The new night-mode gate adds no persistent buffer and
avoids the existing temporary backup when grayscale is unavailable. This is
not a measurement of peak heap on the device.

## Turn-r3: reuse the decoded next page

Idle font prewarming already loads and decodes the next page. A Section now owns
one optional `PrefetchedPageCache` entry so a forward turn can consume that same
object. A mismatched request discards it before loading another page. Section
reload, cache removal, build start, build suspension/abandonment and destruction
clear the entry. Chapter and layout changes inherit that invalidation when they
replace the Section. Opening the reader menu or toolbar also releases it, and a
completed redraw re-arms idle prewarming.

Retaining the existing object allocates no second representation. Admission is
limited to text/rule pages with a conservative charge at most 32 KiB, at least
80 KiB free heap and a largest free block of at least 32 KiB **after** prewarming.
The charge includes owned objects, retained vector/string capacities, text arenas
and 64-byte allowances per allocation/control block. It deliberately overcounts
shared blocks and inline strings; it is a policy bound, not measured peak heap.
Low-memory checks release the entry. Images and building sections use the
existing load path. No new background task or cache-file format is introduced.

The idle path acquires a nonblocking RenderLock before touching the Section.
This follows the focused TryAcquire/locked API from
[Reader #3113](https://github.com/crosspoint-reader/crosspoint-reader/pull/3113),
authored by Erica Jensen <erica@mailershaven.com>. Preserve that attribution if
committing. [Reader #3050](https://github.com/crosspoint-reader/crosspoint-reader/pull/3050)
previously explored retaining a Page; it was closed without a stated reason.
This implementation scopes ownership to Section rather than introducing a
separate activity generation/lifecycle mechanism. Maintainer feedback on
[#2437](https://github.com/crosspoint-reader/crosspoint-reader/pull/2437#issuecomment-4820867481)
asks for reproducible speed and memory evidence, which motivates the explicit
admission budget and separate acquisition measurements here.

`ReaderProgressState` also compares the exact saved snapshot, including the
estimated total and visible offset. An unchanged redraw of a partially built
chapter no longer mismatches its actual page count against the previously saved
estimate. Only successful writes update the snapshot, preserving retries and
the existing progress-file replacement sequence. Its fixed state replaces the
three old saved-position integers; it does not allocate.

[The cached EPUB harness](../test/epub_page_turn/README.md) executes the production
section-file loader, Page/TextBlock deserialization, font prewarm and rendering.
The prose fixture eliminates one foreground open, five seeks, 577 reads,
2,598 bytes and 96 C++ allocation requests; the annotated fixture eliminates
2,852 bytes and 101 requests. The idle load still occurs once. Both policies
produce identical serialized metadata, progress offsets, B/W and gray planes
across four orientations, with unchanged display call counts.

This harness tests the real cached-page pipeline, not Activity/UI input or HTML
pagination. Section/Activity invalidation wiring is source-reviewed; ownership,
budgeting, pressure release and content are unit-tested. Separate tests exercise
the real lock implementation and the successful-save snapshot policy. Host
memory-backed file timings do not predict physical SD or page-turn latency.

The final turn-r3 run passed all 415 host tests, including 5,000 synthetic driver
submissions, and the X4 Pro release build. Static RAM remains 100,312 bytes;
retained pages are additional, reclaimable runtime heap usage. The measured host
budget charges were 11,326 bytes for prose and 14,655 bytes for annotated text.
Targeted static analysis passed with no high/medium findings and four style
suggestions. That historical release used `firmware-x4pro-turn-r3-final.bin`.
For the current image, follow the package FLASH.md or [r7 handoff](reader-reliability-r7.md).

## Turn-r4: avoid unnecessary reader refreshes

Rejected page/chapter navigation no longer requests a repaint. This includes
EPUB queued turns and toolbar chapter scrubbing, plus TXT/XTC page boundaries.
Successful navigation, end-of-book transitions, unfinished chapter builds and
explicit refresh retain their existing behavior. An ignored input consumes no
refresh-cycle count and needs no panel activation.

Monochrome EPUB pages can close the toolbar by restoring its existing B/W
snapshot. The restore keeps the panel baseline intact so the differential
update erases the visible chrome, and uses the existing refresh-cycle helper
on Xteink panels. Scheduled and promoted cleanup still run; explicit reader
refresh falls back to rendering. Pages requiring grayscale retain the full
render path. No extra snapshot is allocated. The snapshot is invalidated before
rendering can return early and immediately on toolbar chapter changes, preventing
an old page from reappearing after an empty chapter or failed load.

EPUB/TXT also skip text anti-aliasing when the real body draw uses only black
1-bit text (or no text), and there are no images. GfxRenderer conservatively
tracks the resolved family/style: any 2-bit font, white text or unknown font
data retains the pass. This covers fallback, rotated and scaled text without a
pixel scan. Tracking excludes the font-prewarm scan and status bar. The saved
AA setting, glyph rasterization and image compositing remain unchanged. The
tracker adds two boolean fields; the EPUB page classification adds one boolean.
It creates no buffers or allocations. On eligible pages it avoids both gray
composition passes, their scratch/backup allocations and the gray activation.
A normal 2-bit-font AA page still has its original base and gray activations.

This follows the existing capability/HAL boundary and preserves image cleanup
required by [Reader #2226](https://github.com/crosspoint-reader/crosspoint-reader/pull/2226).
Font weight is unchanged: the maintainer closed [Reader #2466](https://github.com/crosspoint-reader/crosspoint-reader/pull/2466#issuecomment-4949412205)
because its native-monochrome conversion made small text too thin. The SSD
absolute/combined capability uses a different quality waveform; reducing its
activation count alone would not establish a faster reading turn.

The new [navigation tests](../test/reader_navigation/README.md) compile actual
production navigation methods and dispatch blocks. The [overlay tests](../test/reader_overlay/README.md)
compile the real close/discard/prologue/chapter-jump code and refresh helper,
using the real GfxRenderer for byte-exact snapshot restoration in four
orientations. Font tests verify conservative selection and mask equality.
The SDK sequence suite now includes 3,000 page/chrome/restore submissions,
checking the old frame at activation and periodic/manual cleanup seeds across
all X4 Pro controllers. These are software/RAM invariants, not optical ghosting
or device-latency measurements. No recording is required.

## Reproduce

```sh
cmake -S test -B build/host-tests -DCMAKE_BUILD_TYPE=Release \
  -DCROSSPOINT_BUILD_PAGE_BENCHMARK=ON \
  -DCROSSPOINT_BUILD_EPUB_PAGE_BENCHMARK=ON
cmake --build build/host-tests -j 8
ctest --test-dir build/host-tests --output-on-failure
build/host-tests/gfx_refresh/GfxPageBenchmark --samples 21
build/host-tests/epub_page_turn/EpubPageTurnBenchmark --samples 21
python3 test/sd_card_font/run_benchmark.py --output build/font-benchmark.json
python3 test/refresh_sequences/run.py --report build/refresh-sequences.json
pio run -e x4pro-gh_release
```

Elapsed-time thresholds are intentionally excluded from unit tests; exact
bytes, operation counts and failure recovery are the stable regression gates.
The host harnesses add no firmware RAM or tasks. They do not simulate whole EPUB
pagination, on-device peak heap, SD electrical timing or panel physics. Packaged
verification logs and benchmark outputs identify the tested source and build.
