# Cached EPUB page acquisition and rendering

This harness executes the production `SectionPageReader`, `Page` / `TextBlock`
serialization and deserialization, `PrefetchedPageCache`, font scan/prewarm,
compressed Noto Serif 14 glyph decoder, BW rendering and both grayscale planes.
It compares idle load/prewarm/discard followed by foreground reload with retaining
and taking the already decoded next page through the production cache.

The section fixture contains two actual `Page::serialize` bodies, a page-offset
LUT and a visible-text-offset LUT at the production header positions. Each page
contains 18 `TextBlock` lines, a horizontal rule, a link and a footnote. The
annotated variant adds ruby, focus splits, underline/strikethrough, superscript /
subscript, combining marks and accented text. TextBlocks store word positions
measured with the actual font; their production renderer applies decorations,
ruby and focus placement. The harness does not run HTML parsing, paragraph line
breaking, an Activity or navigation/UI input.

Tests verify exact serialized bodies (including words, coordinates, styles,
links and footnotes), visible progress offsets, BW and both gray planes across
four orientations. The retained object must have the same address as the idle
page and arrive without a second section-file read or C++ allocation. Both
policies issue the same base/gray display calls; idle prewarming issues none.
The HAL records these calls but does not emulate their physical duration.

Additional tests enforce cache transfer/invalidation, memory floors, oversize and
image rejection, budget accounting for shared blocks, spare vector capacity,
focus arenas and retained ruby-string capacity, and malformed section offsets.
The budget is a conservative production admission charge, not a host heap
measurement. It can overcount shared/inline objects intentionally.

`PersistenceFaultTest.cpp` adds corruption and I/O-failure tests against the
actual `Page`, `TextBlock`, and `SectionPageReader` implementations. It also
compiles the complete production `ImageBlock` constructor and metadata
serialization methods; image pixel rendering still fails loudly if reached. The HAL
keeps real bytes and a cursor; it can reject an open/read/seek/write/rename/remove
or exhaust a write budget partway through a request. Injected read errors return
`-1`; a write that exhausts its budget keeps the written prefix but returns zero,
matching SdFat's error convention (`FatFile.cpp:766-879,1361-1504` in the bundled
PlatformIO SdFat dependency, forwarded by `lib/hal/HalStorage.cpp:246-255`). It
does not emulate FAT sectors, cache flushes, power loss, or physical durability.
Fault counters assert that the configured failure was actually encountered;
separate harness tests verify cursor, prefix, and rename-failure behavior.

The nothrow allocator injector fails each actual nothrow allocation in a
two-line page decode and tracks that earlier page/line/arena allocations are
released. It does not inject into throwing STL allocations or C `malloc`.
The corrupt-ruby test uses one word and a bounded 64 KiB length, avoiding a
second unchecked length read or a deliberately unbounded host allocation.
Dedicated truncated-style tests isolate the style tail; complete-record cutoff
tests also exercise every header, arena, annotation, and geometry boundary.
All added storage and allocation instrumentation is host-only.

The r7 audit found six failures: unused link storage written to the cache,
incomplete rule-page writes, incomplete ruby/style writes, truncated style
reads, a ruby length beyond the remaining file, and a failed rule-coordinate
read. The r8 checked-I/O implementation passes those unchanged assertions.
Additional tests require immediate return at each injected text-field failure,
reject non-boolean flag bytes and invalid alignment, enforce symmetric ruby and
image-path limits, reject nonpositive image dimensions, and cover image/page
allocation failures. These remain ordinary enabled tests.

The new checked helpers are additive; legacy unchecked serialization callers
retain their existing API. Page fields and each text/image metadata field now
check exact I/O results. Checked bool reads validate a uint8 representation
before assigning it, avoiding invalid bool representations from corrupt files.
The shared string helper rejects a length larger than the caller's limit or the
remaining file before resizing. A cached TextBlock permits at most 65,535 ruby
bytes cumulatively, matching its existing 16-bit text-arena ceiling. Image path
strings are limited to 4,096 bytes each. Writers enforce the same limits as
readers; exact-limit roundtrips and one-byte-over-limit failures are tested.
These limits bound corrupted metadata; existing STL vector/string allocation
behavior remains and is not an assurance of success under device heap exhaustion.

Fixed footnote/link strings write their meaningful prefix and zero padding from
one static constexpr 256-byte table. This adds no page-owned buffer or heap
allocation and keeps the wire format's size unchanged. Ruby serialization uses
string_view instead of materializing a temporary string. Checked POD reads need
only one scalar temporary. Fixed page geometry and text metadata are read in
small contiguous groups; the largest new wire buffer is the 24-byte style tail.
The style reader uses aligned `memcpy` for signed coordinates and validates all
four boolean bytes and the alignment enum before assignment. Tests exercise all
80 flag/alignment combinations, signed 16-bit extremes, every truncated record
boundary, negative read errors, and short reads. Writers remain field-by-field,
so their unchanged wire output independently checks the grouped reader.

The rendering fixture initializes padding after its link's terminating NUL so
its exact serialized-body oracle does not depend on host allocator fill.
In the audited r7 source, `PageLink.h:17` initialized only the first href byte;
`Page.h:111-113` copied the meaningful URL including its NUL; `Page.cpp:182`
wrote the entire 256-byte field, and `Page.cpp:313` cleared its final byte on decode. With LLVM
ASan's allocation fill, the original prose body differed at byte 2573 of 2582
and the annotated body at byte 2827 of 2836: `0xbe` became `0x00`, nine bytes
from the end (before the eight-byte link geometry). This is a production
unused-byte persistence defect, not a rendering difference. The separate
`LinkSerializationDoesNotPersistBytesAfterTerminator` test calls real `Page::addLink`,
explicitly fills its live href array after the terminating NUL with `0xa5`,
then exercises real Page serialization and requires zeroed unused href bytes.
Every tested byte has a defined value; no placement lifetime or allocator-fill
behavior is part of this regression. This is a stronger canonical-serialization
contract than merely initializing a new PageLink's storage: constructor hygiene
alone cannot prevent serialization of a subsequently populated unused tail.
The regression is deterministic under both native and sanitizer builds and
passes with the canonical writer; the eight rendering/cache comparisons retain
their full assertions.

```sh
cmake -S test -B build/host-tests -DCMAKE_BUILD_TYPE=Release \
  -DCROSSPOINT_BUILD_EPUB_PAGE_BENCHMARK=ON
cmake --build build/host-tests --target EpubPageTurnTest EpubPageTurnBenchmark -j4
build/host-tests/epub_page_turn/EpubPageTurnTest
build/host-tests/epub_page_turn/EpubPageTurnBenchmark --samples 21
```

The benchmark prints CSV for three separate phases: idle load/prewarm, foreground
page acquisition, and foreground render. It checks identical images and metadata
after every run, verifies stable operation counts across repetitions, alternates
policy order, discards a warm-up pair and reports median host CPU microseconds.
It reports no speed multiplier: moving one pointer may be below the host clock's
resolution, and physical SD latency is absent.

For the 18-line prose fixture, retention eliminates the foreground acquisition's
**1 open, 5 seeks, 193 reads, 2,616 bytes and 59 C++ allocation requests**. For
the annotated page it eliminates **1 open, 5 seeks, 198 reads, 2,870 bytes and
64 C++ allocation requests**. The checked reader skips zero-length string payload reads. The idle load still occurs once; rendering still
makes four passes (scan, BW, LSB and MSB) and one base plus one gray display call.
The cache keeps the decoded page's allocation alive between idle and foreground
instead of freeing and recreating it. It adds no copied page representation.

The r11 reader needed 487/492 HAL reads for these prose/annotated fixtures.
The r12 grouping of the 5-byte text header, style tail, page coordinates, rule
metadata and link geometry removed 294 calls per fixture with the same byte
counts and cache format. The upstream spacing integration adds one serialized
tracking byte per TextBlock (18 bytes per fixture) within that grouped style
read. Section version 49 also adopts upstream Korean word wrapping and rejects
old layout caches. The 43-byte header distinguishes the format from both older
version 47 layouts. Seeks, opens, read calls and allocations remain unchanged. Each real
`HalFile::read` acquires the storage mutex, so the reduction avoids 294 mutex
acquisitions and SdFat calls on an uncached page load or idle prefetch. Enabled
tests cap the read counts; they do not assert timing from the in-memory HAL.

C++ allocation counters include scalar and array `new`, including nothrow forms,
and report requested bytes. They exclude C `malloc` used by the font decoder,
allocator metadata, peak live heap and ESP32 fragmentation. All fixture buffers
and instrumentation are host-only. The HAL serves a memory-backed section file;
this is not a hardware SD, SPI, e-paper BUSY, optical ghosting or end-to-end
page-turn latency measurement. Image decode entry points fail loudly because
image pages are outside this cache's admission policy.
