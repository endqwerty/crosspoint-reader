# Chapter layout, page cache and prefetch

This document covers how cached chapter pages are read back, how text blocks
resolve direction and render ruby annotations, how the reader prefetches the
next page while idle, and how pagebreak markers are handled during chapter
parsing. Byte layouts of `section.bin` and its blocks are in
[file-formats.md](file-formats.md); this document does not repeat them.

## Cached page reads (TextBlock deserialization)

`TextBlock::deserialize` reads each word's ruby annotation as a four-byte length
followed by the string payload. Most blocks have no annotations, so the lengths
are almost all zero. `readRubyTexts` (`lib/Epub/Epub/blocks/TextBlock.cpp`)
batches these small reads through a 64-byte stack buffer:

- Read-ahead never exceeds the minimum bytes still required by the remaining
  strings (the current request plus four bytes per remaining word). The reader
  therefore never reads past the annotation run into the following style fields,
  and never needs to seek back.
- Payloads consume already-buffered bytes first, then read their remainder
  directly into the destination string. Nothing is read twice.
- Lengths are copied with `memcpy` into aligned variables (RISC-V faults on
  unaligned multi-byte loads).
- Nonempty strings are decoded directly into their slots in `rubyTexts`. The
  vector is created lazily, only when a nonempty annotation is found, so a block
  never holds an all-empty `rubyTexts`.
- The cumulative annotation byte budget (`TextBlock::MAX_RUBY_BYTES`, shared
  across all annotations of a block) and the "length larger than the remaining
  file" check still reject corrupt caches before any allocation.

Each avoided read is also an avoided `HalStorage` mutex acquisition. The change
adds no heap allocation, resident cache, public API or cache-format change. The
existing word arena and HAL locking are unchanged. A page that was already
prefetched (see below) is acquired with zero reads.

## Line-break gap measurement

`ParsedText::computeLineBreaks` reaches the gap before word `j` (kerning for a
continuation, character spacing for a no-space boundary, else the scaled space
advance) from every line start within a line of it. The gap depends on `j`
alone, so each is measured once into a 64-entry `int16_t` window on the stack
(128 bytes, no heap allocation). Line start `i` fills the slot of word `i + 1`;
because `i` only decreases, the slots of the next 64 words always hold their own
gaps. A line longer than the window measures the remaining gaps directly. Breaks
are unchanged, so the section cache version is too.

Host count on the stub renderer, 400 four-letter words at 480 px: 5,847
space-advance lookups before, 1,137 after. It applies whenever a chapter is laid
out, not to turning cached pages. Device time is unmeasured.

## Word direction probe

`BidiUtils::detectParagraphLevel` (`lib/MiniBidi/BidiUtils.cpp`) finds the first
strong directional character within a probe budget. ASCII `A-Z`/`a-z` return
left-to-right immediately, without going through `utf8NextCodepoint` and the
`bidi_class` table search. Those letters are already class L in that table, so
the result is the same. All other bytes use the existing decoder and classifier.
Punctuation and digit prefixes still count against the probe budget. The initial
null/limit guard returns `fallbackLevel & 1` unchanged.

`startsWithRtl` delegates to the same probe with a left-to-right fallback, so
there is a single loop. `TextBlock` probes each word this way during rendering.
The fast path adds no table, buffer or allocation. Non-ASCII text gets no
speedup and pays one extra byte-range check before its existing path. Visual
shaping (vendored minibidi), glyph rendering, cache format and public signatures
are unaffected.

## Ruby rendering

`TextBlock::render` resolves ruby positions into a temporary
`std::vector<RubyDrawInfo>` that holds geometry only: the x position and base
direction per word. Annotation text is read directly from the block-owned
`rubyTexts` vector when drawing. This is safe because the `const TextBlock`
keeps ownership of those strings for the whole render, and no pointer is
cached beyond it. The geometry vector is the only temporary table, and it is
allocated only when the block has ruby. Ordinary prose pages make no rendering
allocations. Measurement order, drawing order, ruby grouping
(`RUBY_CONTINUE`), directions and serialized data are identical to copying the
strings. Layout (`extractLine`) has already reserved start/end offsets, so a
centered ruby stays inside the page margins.

## Idle next-page prefetch

While the reader is idle, `EpubReaderActivity::loop` decodes the next page of
the current section and hands it to `Section::retainPrefetchedPage`. The next
`Section::loadPage` for that index takes the retained page instead of reading
the SD card.

### Gates (all must hold, checked in this order)

1. `RenderLock` acquired in `Try` mode, and a section exists. Under this lock
   the cache first calls `releasePrefetchedPageIfLowMemory` on every tick.
2. The section is not building. Parsing in progress is excluded, and
   `Section::retainPrefetchedPage` also rejects retention while `build_` is set.
3. The framebuffer exists, no overlay is open, and more than 400 ms
   (`IDLE_PREWARM_DEBOUNCE_MS`) have passed since the last render completed.
4. The displayed (spine, page) differs from the last attempted one. This allows
   only one attempt per displayed page. This check precedes the heap queries, so
   later idle ticks skip redundant heap-statistics calls.
5. `PagePrefetchPolicy::hasHeadroom(ESP.getFreeHeap(), ESP.getMaxAllocHeap())`.
6. A next page exists in the section. At the end of a section nothing is read.

The attempted-page marker is set only after the heap gate passes. If memory is
insufficient, the page stays eligible and gets one attempt later if memory
recovers.

### Memory policy

`PagePrefetchPolicy` (`lib/Epub/Epub/PagePrefetchPolicy.h`) is a stateless
`constexpr` helper shared by the reader and the cache:

- `MIN_FREE_HEAP` = 80 KiB free heap
- `MIN_MAX_ALLOC` = 32 KiB largest free block

`PrefetchedPageCache` (`lib/Epub/Epub/PrefetchedPageCache.h`) aliases these as
its public constants and adds `MAX_PAGE_BUDGET` = 32 KiB per retained page
(`Page::cacheBudgetBytes`). The cache owns at most one already-decoded page,
held in a `std::unique_ptr`, so retaining it makes no second copy. It is scoped
to one `Section`, and the reader's `RenderLock` serializes access.

The same floors apply at three points:

- Before loading, so the reader does not decode a page only to discard it. The
  idle gate once used lower floors (24 KiB / 16 KiB) than retention. Under
  memory pressure the reader then read and decoded pages that could never be
  kept, and marked the displayed page as attempted.
- After decoding, in `retain`. This check is mandatory and uses fresh heap
  values, because the decoded page consumes memory and other work can change
  the heap. Passing the pre-load gate does not reserve memory or guarantee
  retention.
- On every idle tick, through `releaseIfLowMemory`, which drops the retained
  page when the floors are no longer met.

`take(n)` returns the page only for a matching index and clears the cache on a
mismatch. `retain` clears any previous page first.

### No font preparation during prefetch

The idle path loads and retains the decoded page only. It does not scan or
prepare fonts. Preparing fonts inside a `FontCacheManager::PrewarmScope` would be
wasted work, because the scope clears caches when destroyed:
`FontDecompressor::clearCache` frees page and hot-group buffers,
`SdCardFont::clearCache` resets mini glyph data, and `TtfEpdFont::clearCache`
drops cached page glyphs. The foreground render opens its own scope and prepares
the page again. Foreground font preparation and rendering are unchanged. SD-font
persistent advance data and vector-font buffer capacities have different
lifetimes, so this does not mean every backend loses every side effect.

## Pagebreak markers

Elements with `role="doc-pagebreak"` or `epub:type="pagebreak"` mark
print-page boundaries. Their own page label renders nothing, but any book text
they wrap is kept. Calibre conversions often wrap the continuation of a paragraph
in such a marker. Some publishers also put the attribute on the element that
starts a print page. Skipping the whole subtree would drop that text silently.
The logic is in `lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp`:

- `isPagebreakMarker` ignores the attribute on `p`, `h1`-`h6`, `li` and
  `blockquote`, which render normally. Any other element with the attribute
  (`span`, `a`, `div`, `hr`, ...) is a marker.
- A marker opens no block or style. Its text is captured into a fixed 32-byte
  buffer in the parser object, which needs no heap. Its `aria-label` (or
  `title`) is stored in a 24-byte buffer, truncated if longer. The reading
  offset at the marker's start is saved too.
- When the marker closes, `isPagebreakLabelText` decides what happens to the
  capture. It is dropped when it is empty or whitespace. It is also dropped
  when it matches the label, ignoring case, spacing and punctuation, with an
  optional `p`/`page` prefix. An unlabelled marker drops text of up to 8
  characters that is all digits or a lowercase roman numeral below 1000.
  Anything else is book text.
- `replayPagebreakCapture` feeds the captured text back through `characterData`
  with `visibleTextOffset` rewound to the marker start. The words keep their
  original reading offsets, and parsing resumes at the offset it had reached.
  Replay also happens immediately if the text overflows the buffer, or if a
  child element other than a `<br>` appears. A `<br>` that follows nothing
  but a page number is treated as part of the marker, as in Calibre's
  `<span epub:type="pagebreak"><br/></span>`.
- Open marker depths are tracked (`PAGEBREAK_MARKER_NESTING` = 4), so their
  closes skip the block and style handling that their opens skipped. A
  block-level marker still ends the current word. Nesting deeper than four
  markers falls back to skipping that marker's subtree.

Known gap: the upstream deferred-`<br>` spacing rework is not included. Restored
text still follows the converter's line break. This behavior defines
`section.bin` version 51. Version 52 also preserves paragraph continuity and
top spacing across soft flushes. Version 53 incorporates upstream's redaction
glyph widths and retires older version 52 layouts, with partial sentinel 229
and the same byte layout. See
[file-formats.md](file-formats.md).

## Limits and known gaps

- Performance claims are host operation counts: HAL read requests, classifier
  calls, allocation requests and heap queries. Device page-turn latency, peak
  device heap and SD-font heap during ruby rendering have not been measured.
- None of these changes alter the e-ink refresh sequence or claim reduced
  ghosting.
- Prefetch retains only the next page of the current section, at most one page,
  and never while the section is building. It does not cross chapter
  boundaries or prepare images or fonts.

## Code and tests

- Code: `lib/Epub/Epub/blocks/TextBlock.{h,cpp}`, `lib/MiniBidi/BidiUtils.cpp`,
  `lib/Epub/Epub/PagePrefetchPolicy.h`, `lib/Epub/Epub/PrefetchedPageCache.h`,
  `lib/Epub/Epub/Section.cpp` (`retainPrefetchedPage`, `loadPage`),
  `src/activities/reader/EpubReaderActivity.cpp` (`loop`),
  `lib/Epub/Epub/parsers/ChapterHtmlSlimParser.{h,cpp}`.
- `test/epub_page_turn/`: `CachedPageReads` read/allocation budgets and
  byte-exact round trips across buffer boundaries, `RubyRendering` framebuffer
  parity (all four orientations, BW and both grayscale planes) and allocation
  budgets, `PrefetchedPageCache`/`PageBudget` in `CacheBudgetTest.cpp`, and
  short-read and ruby-limit regressions in `PersistenceFaultTest.cpp`.
- `test/minibidi_arabic/DirectionProbeTest.cpp`: probe output parity against
  the reference classifier and classifier-request budgets. The existing
  Arabic/Farsi/Urdu shaping tests stay in the same directory.
- `test/reader_incremental/`: `ReaderIdlePrefetch` compiles the production idle
  block and the shared policy with platform doubles. It covers lock, UI,
  debounce and heap gates, active builds, end of section, memory pressure, and
  the post-decode recheck.
- `test/korean_line_breaking/` (`LineBreakCost`): the lookup bound and equal
  breaks on lines longer than the gap window.
- `test/chapter_html_slim_parser/` (`PagebreakMarkerTest`) and
  `test/section_parser_integration/`
  (`CalibrePagebreakMarkersKeepWrappedTextAndDropLabels`).

## Attribution

- Pagebreak marker handling is adapted from upstream PR #3349 by
  Sylve <sylve@hyli.org> (Co-Authored-By on the fork commit). Its
  deferred-`<br>` spacing rework is not included.
- The idle-prefetch cleanup came out of reviewing Jan Steinke's draft PR #3675
  (https://github.com/crosspoint-reader/crosspoint-reader/pull/3675). Its
  additional image prefetch work and window-paused font preparation are not
  imported and remain deferred.
- The line-break gap window is adapted from open upstream PR #3814 by SeungBeom
  Choi <puritysb@gmail.com> at `2e53b6f4` (Co-Authored-By on the fork commit).
  If it merges, the fork's copy should drop out on the next rebase.
- Draft PR #3705 is deferred: its own evidence shows a cold-layout regression
  with varying-width fonts. The sources do not name its author.
