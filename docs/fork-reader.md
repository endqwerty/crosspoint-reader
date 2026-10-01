# Reader: navigation, page turns, scheduling and in-book search

This document describes the EPUB reader's page-turn pipeline, refresh and
ghost-cleanup policy, navigation history, incremental chapter scheduling,
failure recovery, persistence of reading state, and the Find in Book command.
Byte layouts of cache files are in [file-formats.md](file-formats.md).

Main code:

- `src/activities/reader/EpubReaderActivity.{h,cpp}` (turn queue, overlays,
  navigation, incremental build, progress), `ReaderNavigationHistory.h`,
  `ReaderProgressState.h`, `ReaderGrayscalePlan.h`, `ReaderRefreshTransaction.h`,
  `ReaderToolbarUi.{h,cpp}`, `EpubReaderFootnoteSelectActivity`,
  `EpubReaderBookmarksActivity`, `EpubSearchActivity`,
  `XtcReaderActivity`
- `lib/Epub/Epub/Section.cpp`, `PrefetchedPageCache.h`, `PagePrefetchPolicy.h`
- `lib/EpubSearch/` (search engine), `src/activities/RenderLock.h`,
  `src/util/ButtonNavigator.{h,cpp}`, `src/main.cpp` (main-loop scheduling tail)
- `lib/Serialization/AtomicFile.{h,cpp}`, `PersistableStore.{h,cpp}`,
  `src/util/BookmarkFile.cpp`, `src/util/BookmarkUtil.cpp`

## Design constraints

CrossPoint prioritizes legibility, bounded memory and portable reader logic
([SCOPE.md](../SCOPE.md)). Display-controller policy belongs in the HAL/SDK; the
reader uses existing capabilities. The refresh-frequency setting, anti-aliasing
(AA) setting, cleanup waveforms and single-framebuffer contract are retained.
No change here alters SDK waveforms, BUSY timeouts or the AA default.

The X4 Pro ships with SSD1677, UC8179 and UC8279 controllers; a gain on one is
not evidence for another. Relevant upstream SDK decisions:

- [SDK #38](https://github.com/Free-Ink/freeink-sdk/pull/38): the SSD1677 fast-DU
  shortcut skips temperature and power sequencing and has reported ghosting and
  blotching. It stays disabled.
- [SDK #56](https://github.com/Free-Ink/freeink-sdk/pull/56): HALF must perform
  cleanup rather than behave like FAST (fixes included in the SDK).
- [SDK #91](https://github.com/Free-Ink/freeink-sdk/pull/91): regional refresh for
  sparse updates; its timings do not establish a benefit for full page changes.
- [Reader #2179](https://github.com/crosspoint-reader/crosspoint-reader/pull/2179)
  illustrates that CPU rendering time and visible page-turn time are distinct:
  most of a turn is panel refresh, so CPU savings do not translate one-for-one.

Host tests throughout measure software/RAM invariants (bytes, operation and
allocation counts, command sequences). They do not measure optical ghosting,
panel response, SD latency, peak device heap or physical input, and no
Kindle-parity or speed claim rests on them.

## Page turns

### Input queue and ordering

- Manual turns use a single-slot queue (`pendingManualTurn`, one `int8_t`). The
  latest direction wins, including input that arrives as the guard expires.
  Fresh input is sampled before the queued turn is drained.
- A turn is held while `RenderLock::peek()` reports rendering in progress or
  less than 200 ms (`kMinManualTurnGapMs`) has passed since the last turn. This
  prevents stale turns; it does not raise the turn rate.
- Chapter skips (long press >= `SKIP_HOLD_MS`, 700 ms), orientation changes,
  end-of-book actions, a missing section, `clearPendingNavigation()` and opening
  the reader menu discard the queued turn.
- `openOverlay()` and `onSuspend()` also clear it. Every overlay (for example the
  toolbar opened from the home button) and every pushed screen (dictionary,
  footnote selection, sync, and so on) goes through one of them, so a turn queued
  behind an in-flight page update cannot flip the page with no input after the
  reader returns. Compared with upstream
  [PR #3636](https://github.com/crosspoint-reader/crosspoint-reader/pull/3636),
  this was the one case the fork's queued-turn handling had missed. Covered by
  `test/reader_overlay`.
- Rejected page/chapter navigation (queued EPUB turns, toolbar chapter scrubbing,
  XTC page boundaries) requests no repaint, consumes no refresh-cycle count
  and activates no panel. Successful navigation, end-of-book transitions,
  unfinished chapter builds and explicit refresh behave normally.

### Reusing the decoded next page

Idle font prewarming already loads and decodes the next page. A Section owns
one optional `PrefetchedPageCache` entry so a forward turn consumes that same
object instead of loading it again. A mismatched request discards it first.
The entry is cleared on Section reload, cache removal, build start, build
suspension/abandonment and destruction; chapter and layout changes inherit this
by replacing the Section. Opening the reader menu or toolbar releases it, and a
completed redraw re-arms idle prewarming.

Admission (policy bound, not measured peak heap):

- text/rule pages only; images and sections still building use the normal load
  path;
- conservative charge at most `MAX_PAGE_BUDGET` = 32 KiB, counting owned
  objects, retained vector/string capacities, text arenas and a 64-byte
  allowance per allocation/control block (deliberately overcounts shared blocks
  and inline strings);
- at least 80 KiB free heap and a 32 KiB largest free block **after**
  prewarming (`PagePrefetchPolicy`).

Low-memory checks release the entry. No second representation, background task
or cache-file format is introduced; the retained page is reclaimable runtime
heap. The idle path takes the render lock nonblockingly
(`RenderLock(RenderLock::Mode::Try)` / `ownsLock()`) before touching the Section.
Ownership is scoped to Section rather than a separate activity generation
mechanism. [Reader #3050](https://github.com/crosspoint-reader/crosspoint-reader/pull/3050)
explored retaining a Page and was closed without a stated reason; maintainer
feedback on [#2437](https://github.com/crosspoint-reader/crosspoint-reader/pull/2437#issuecomment-4820867481)
asks for reproducible speed/memory evidence, which is why the admission budget
is explicit.

### Progress snapshot

`ReaderProgressState` compares against the exact saved snapshot, including the
estimated total and visible offset, so an unchanged redraw of a partially built
chapter does not rewrite progress because its page count differs from the saved
estimate. Only successful writes update the snapshot, preserving retries and the
progress-file replacement sequence. It is fixed state with no allocation.
Progress is saved only after the target page has successfully reached the
display.

### Font caches

Adopted upstream fixes (see Attribution):

- [#3521](https://github.com/crosspoint-reader/crosspoint-reader/pull/3521):
  freshly prefetched SD-font glyphs are kept until drawing; obsolete page glyph
  sets are replaced after a complete page scan; temporary allocations are
  released before a bitmap arena grows. Incremental UI strings keep the
  accumulating cache.
- [#3527](https://github.com/crosspoint-reader/crosspoint-reader/pull/3527):
  rebuildable font caches are released before chapter layout allocations.

No persistent buffers are added; temporary mapping/read-order arrays use checked
RAII allocations and are freed earlier. Fragmentation recovery may reread
metadata and rebuild width tables, and chapter transitions may need to rewarm
fonts. The page-scoped SD-font cache bounds prefetch traffic per page instead of
growing with every page read; the deliberate tradeoff is that revisiting older
sparse glyph subsets can reread glyphs an accumulating cache would still hold.
Warm foreground drawing performs no storage reads.

The SD-font memory rewrite in upstream PR 3705 is deferred: its draft reports a
cold-layout regression with varying-width fonts.

## Rendering, grayscale and refresh

### Whole-plane grayscale composition

The buffered AA path allocates one or two complete planes, subject to free-heap
and largest-block headroom checks, and renders each as one full-height target
instead of walking the page once per 80-row band (one traversal per plane
instead of six on a 480-row panel; glyphs crossing band boundaries are decoded
once). This reduces CPU work, not waveform duration, and adds no allocations.
The 80-row scratch path remains the fallback when full planes do not fit. The
B/W framebuffer stays intact while the panel is busy. Capability checks restrict
this to the buffered overlap path; the UC8179/UC8279 X4 Pro drivers use a
different path. Band culling already skipped many off-screen glyphs, so fewer
traversals do not mean proportionally less bitmap work.

### Skipping unnecessary grayscale

- Night mode: the SDK reports grayscale unsupported while inverted.
  `ReaderGrayscalePlan` (no allocation) lets EPUB check that before
  choosing a grayscale path, avoiding the 48,000-byte temporary B/W backup and
  two gray passes whose uploads would be discarded. B/W drawing, the user AA
  preference, images and scheduled cleanup are unchanged; supported day-mode
  paths keep their capability selection, including the low-memory strip
  fallback.
- Monochrome text: EPUB (including TXT/Markdown) skips text AA when the body draw uses only black
  1-bit text (or no text) and there are no images. GfxRenderer conservatively
  tracks the resolved family/style; any 2-bit font, white text or unknown font
  data keeps the AA pass. This covers fallback, rotated and scaled text without
  a pixel scan. Tracking excludes the font-prewarm scan and status bar. It adds
  two booleans to the renderer and one to the EPUB page classification.
  Glyph rasterization, image compositing and the saved AA setting are unchanged,
  and image cleanup required by
  [#2226](https://github.com/crosspoint-reader/crosspoint-reader/pull/2226) is
  preserved. Font weight is not changed:
  [#2466](https://github.com/crosspoint-reader/crosspoint-reader/pull/2466#issuecomment-4949412205)
  was closed because native-monochrome conversion made small text too thin. The
  SSD absolute/combined capability uses a different quality waveform, so fewer
  activations alone would not make a turn faster.

### Toolbar close without re-render

Monochrome EPUB pages close the toolbar by restoring its existing B/W snapshot.
The panel baseline is kept so the differential update erases the chrome, using
the existing refresh-cycle helper on Xteink panels. Scheduled and promoted
cleanup still run; explicit refresh and grayscale pages take the full render
path. No extra snapshot is allocated. The snapshot is invalidated before
rendering can return early and immediately on toolbar chapter changes, so an old
page cannot reappear after an empty chapter or failed load.

### Ghost cleanup and manual refresh

- The control panel's one-shot FULL cleanup request is honored by both
  grayscale-base entry points, as B/W refreshes already did. If an explicit
  grayscale mode is unsupported, the request stays pending so the B/W fallback
  performs it. The normal cleanup interval is unchanged.
- The pull-down Refresh action records intent on the panel activity; its
  `onExit()` promotes the destination frame to FULL while ActivityManager holds
  the RenderLock, after the panel's last possible paint. A late panel repaint
  therefore cannot consume the cleanup intended for the book. Normal dismissal
  requests no extra FULL.
- The panel input loop holds the RenderLock around FreeInkApp routing and
  callbacks, because routing and rendering share event/interaction state.
  Callbacks do not acquire a second lock.
- The motivating report was
  [#3555](https://github.com/crosspoint-reader/crosspoint-reader/issues/3555);
  the related [#3289](https://github.com/crosspoint-reader/crosspoint-reader/pull/3289)
  is part of the baseline. Host tests demonstrate a software race, not every
  physical symptom in that report.

### Display commit failures

HAL and renderer expose the driver commit result. An EPUB render keeps its
cleanup request and refresh cadence until the complete page succeeds, including
deferred and grayscale work (`ReaderRefreshTransaction`, a small stack
transaction holding references and prior cadence/flag values). A placeholder
image does not certify the page. Failed output stops automatic page turns and
returns before saving progress; a queued manual refresh stays available for the
next submission. Typed grayscale recovery preserves the canvas even in
dual-buffer builds. BUSY-failure latching in the bus/driver is covered by the
storage and display reliability document.

## Navigation

### Links, footnotes and history

- Following a link or a footnote records a Back destination and never replaces
  saved progress: the book reopens on the page that was showing, as upstream
  #3764 defines. Opening the Footnotes action starts an excursion. Links followed
  from a note stay in the excursion; Back unwinds each destination.
- The Back destinations outlive the reader (sleep, home, KOReader sync) in the
  book cache's `links.bin`, in upstream's format: a depth byte, then spine and
  page as two little-endian `uint16` per entry. The file is written on exit only
  when history exists and is consumed (read, then removed) on the next open, so
  an unclean shutdown cannot resurrect a stale stack. Restored entries are plain
  link returns: the file has no jump kind, page count or text offset, so after a
  reopen the Footnotes shortcut opens the list instead of returning, and a
  return across a re-pagination lands on the saved page number.
- The parser collects internal links for the Footnotes list without retaining
  `epub:type=noteref`, so the reader distinguishes the user's action (link tap vs
  Footnotes action), not a meaning guessed from link text or filenames.
- `ReaderNavigationHistory` has `CAPACITY = 3` fixed entries and no dynamic
  allocation. Each entry holds spine, page, page count, jump kind and an optional
  exact visible-text offset, so returning from a footnote within a session
  preserves the origin across reflow. When full, the oldest entry is dropped,
  except that an outermost footnote origin in slot 0 is kept and the next-oldest
  is dropped. Invalid href resolution does not modify history.
- An explicit chapter, bookmark, percentage, search or toolbar jump clears the
  history, so Back cannot return across it.
- The Footnotes menu entry comes first when a page has links. One item opens
  directly (no picker allocation); several use the picker, whose allocation is
  fallible and checked. The power-button shortcut and menu share this behavior;
  pressing it inside a footnote excursion returns to the origin. Cancelling the
  picker returns to the menu or page. The existing translated Footnotes label is
  used for both entry points.
- `links.bin` is upstream's file; the fork adds no persistent format of its own
  here.

### Resume and percentage jumps

- A saved position in spine 0 resumes instead of being treated as a first launch.
- Exact percentage chapter boundaries select the following chapter.
- Percentage jumps and backward jumps into an unfinished previous chapter
  schedule the target, start the existing parser and return without foreground
  pagination; the build tick advances it (see Scheduling). Complete chapter
  caches resolve immediately. HTML inflation and parser startup remain
  synchronous.
- Automatic turning waits for a pending jump and starts a fresh interval once it
  resolves. Allocation, startup or background-build failure shows an indexing
  error and stops automatic retries until explicit navigation.

### Section lookups

Cached page-count, anchor, paragraph and list-index lookups in `Section.cpp`
share one checked reader that validates the whole lookup header, supported
finalized or partial version, section ordering and table extents with 64-bit
arithmetic, including the full declared extent even for an early row. Stored
counts are checked against the header. Failed seeks, negative reads and short
reads return no result. Partial caches allow navigation over committed pages
but cannot supply a complete chapter total.

- Paragraph and list-item searches keep first-at-or-after semantics, duplicate
  handling and final-page fallback, reading 16 `uint16_t` indices per read into a
  32-byte stack buffer.
- Anchor matching bounds each accessed entry by the anchor-region end, rejects
  out-of-range pages, skips names of different length and compares equal-length
  names through a fixed 64-byte buffer. No allocation uses a length read from SD
  (a corrupt length such as `UINT32_MAX` once reached `std::string::resize`). The
  first match wins and later records are not scanned; empty and long keys work.
  This validates lookup structure and accessed entries, not every page payload.
- File size is queried once and the anchor cursor is tracked locally. No
  resident cache, heap allocation or saved state is added.

### Failed page reads

Before destroying a Section after a failed cached-page load, the reader stores
that Section's current target page in its resume fields and clears stale
deferred positioning and the previous page's text offset, so the rebuild retries
the failed target rather than the section entry page. The three-retry limit
stays. Unsuccessful attempts save no progress and do not alter link/footnote
history. Reuses existing fields; no allocation.

## Scheduling and the render lock

- `RenderLock` offers `Mode::Blocking` and `Mode::Try` with `ownsLock()` and a
  static `peek()`. Deferrable work takes it nonblockingly and yields when
  rendering owns it or heap is short.
- Main loop (`src/main.cpp`): it tries the lock once before asking whether
  pagination has work. If rendering owns it, the loop sleeps 10 ms without
  reading section state or changing power mode, so contention is not treated as
  idle. Otherwise it reads `skipLoopDelay()` under the lock, releases it, then
  applies the existing yield/idle policy, including short input-polling slices
  during idle power saving.
- Background build work also acquires before reading state and shares its
  scheduling predicate with the main-loop hint. Heap admission stays in the
  build tick so a paused build can resume.
- Incremental chapter builds request `BACKGROUND_BUILD_PAGES_PER_TICK` = 2 pages
  per tick, paused while free heap < 32 KiB or largest block < 16 KiB
  (`BACKGROUND_BUILD_MIN_*`). Foreground chunks use `BUILD_PAGES_PER_CHUNK` = 8.
  The page count is a requested pagination budget, not a wall-clock or output
  limit; the parser yields at its input/paragraph boundaries. The combined build
  tick retains pending navigation and error recovery. No task is created.
- `ButtonNavigator` takes synchronously consumed initializer-list views and
  returns directions from `static constexpr` lists instead of temporary vectors,
  so idle polling makes no allocations. Caller brace lists are never retained.
  Button presses, repeats, releases and mappings are unchanged; existing callback
  types remain, and large `std::function` captures can still allocate. This does
  not by itself establish reduced fragmentation or latency.

No task, mutex, priority change, heap allocation, cache format or waveform is
introduced by this scheduling.

## Cache and text integrity

- Checked layout allocation failures stop the chapter parser (byte-sized failure
  enum). Page delivery checks Section writer failures, and Section honors the
  final parser result (plus an I/O failure flag), so an incomplete build is
  never installed as complete nor saved as a partial cache on exit. A fresh
  build can retry a transient failure.
- Page, text and image metadata serialization stops at the first failed
  operation. Decoding rejects truncated fields, invalid booleans/alignment,
  unterminated fixed strings and oversized lengths. Ruby strings share a
  `MAX_RUBY_BYTES` (65,535) budget per text block; each cached image path is
  limited to `MAX_CACHED_PATH_BYTES` (4,096). The writer applies the same bounds.
  Fixed-size links/footnotes are zero-padded after the NUL from a 256-byte
  `static constexpr` block in flash.
- Section checks header, footer, lookup tables, seeks and close. Reading an
  in-progress page must restore the append cursor; failure closes the writer and
  blocks publication. Installation keeps the prior cache as `.bak` until the new
  file is in place; a failed install rolls back, a failed rollback keeps the
  backup for next-open recovery, and interrupted cleanup conservatively restores
  the older committed layout. Backup path strings exist only at build/open time.
  Progress and bookmarks live outside these files.
- Section cache version bumps invalidate old layouts (current value in
  file-formats.md); a book may rebuild its layout once without losing progress.
- The build LUT reserves a 32-entry batch (384 bytes) during a build only and
  still grows; TOC anchor collection reserves eight entries and grows only for
  anchors in the current spine.
- Paragraph/page allocations use checked nothrow ownership. STL containers still
  use the default allocator; behavior under every possible throwing allocation
  on an exhausted ESP32 is not established.

## Persistence and bookmarks

- Shared JSON persistence (`AtomicFile`, `PersistableStore`) writes `.new`,
  checks write/close and readback, keeps the prior primary as `.bak`, then
  installs. It commits only when backup cleanup succeeds. A retained backup is
  the committed copy readers use; a retry restores it before writing again.
  Failed partial writes, readback, rename, rollback or cleanup never report
  success. FAT metadata and SD power-loss durability still depend on the device.
- `MAX_FILE_BYTES` = 50,000 applies to reads and writes; oversized or incomplete
  JSON is rejected before replacing the prior file. Reads reserve once and check
  each 128-byte HAL read and String append (avoiding the SDK reader's silent
  truncation). Costs: a 128-byte local buffer, a shared transaction mutex, one
  checked `makeUniqueNoThrow<char[]>` for variable-length suffix paths (a fixed
  buffer would truncate valid paths or stay resident), and reuse of the existing
  serialization String with no second full-file buffer.
- Bookmark add/remove/list-delete serialize the proposed edit before changing
  the live list; rename failure restores the old name; failures show a
  translated error. Bookmarking requires a rendered, committed page with a known
  content offset; a page change or failed render invalidates the offset until
  the next successful render. Missing bookmark files are distinguished from
  unreadable or malformed ones; an unavailable cache is retried on explicit edit,
  and a failed retry blocks the edit rather than replacing bookmarks with an
  empty list. Existing vectors and a single proposed entry are reused.

## Find in Book

The reader's **Find in Book** command opens the keyboard, scans the current EPUB
for a literal phrase and lists snippets with section numbers. Selecting a result
uses visible-text offset navigation; Back cancels without moving the position.
Results are transient; reopening starts a new scan.

### Resource contract

- No worker, index, prefetch, timer or polling path. During ordinary reading
  search does zero CPU or SD work and retains no heap, parser, query, buffers or
  results. Dormant state is one nullable atomic allocator-context pointer (4
  bytes on ESP32). No search fields exist in cached pages, sections or the reader.
- `EpubSearchActivity` owns query and results. A chapter parser exists only
  inside `scanChapter()` and is destroyed before the next loop; `finish()`
  releases its XML allocations on cancellation, malformed input and allocation
  failure. Results and UI rows are freed in `onExit()`, and destruction releases
  the query capacity before the reader resumes.
- Launch order: the reader allocates the search activity first (launch OOM is
  handled), keeps the current page and visible-text offset, then under the render
  lock releases the section (with its incremental parser, lookup tables and
  prefetched page), overlay, image cache and SD font caches. The EPUB and image
  extractor stay loaded. Cancel reloads the saved page from the section cache.
- Large fixed arrays use `makeUniqueNoThrow` while search is open (not stack, not
  static): results at most 5,600 bytes and the chapter matcher at most 2,600
  bytes (host size assertions). UI rows (32 list items and 32 short section
  labels) are allocated after scanning. These exclude keyboard/UI objects and
  ZIP streaming buffers.
- During each synchronous ZIP read, search borrows the framebuffer under the
  render lock for the inflater's state and 32 KiB window. The loan returns before
  parser finalization, result-row allocation or rendering, including on read
  failure and cancellation; the panel keeps showing the searching screen.
- Expat allocations are charged to a 32 KiB ledger including aligned block
  headers. This bounds retained parser memory; it excludes platform allocator
  overhead and transient `realloc` copies. Because Expat's allocator API has no
  context argument, create/parse calls claim the nullable pointer atomically and
  release it immediately; competing or nested calls fail closed. Each allocation
  records its owner for cleanup.

### Limits (`lib/EpubSearch/EpubSearch.h`)

| Limit | Value |
| --- | --- |
| Query | 64 UTF-8 bytes, nonempty and not only whitespace |
| Results | First 32 matches in spine order |
| Snippet context | Up to 12 codepoints before and after a match |
| Input chunk / cancellation checkpoint | 1 KiB |
| Uncompressed input per chapter | 4 MiB |
| Uncompressed input per search | 64 MiB |
| Spine items per search | 4,096 |
| XML nesting | 64 elements |
| Single markup token | 8 KiB |
| Element name | 128 bytes |
| Accounted Expat allocations | 32 KiB |

### Behavior and gaps

- One chapter per activity loop, yielding at input checkpoints. Cancellation is
  checked during the scan, but an in-progress storage read must return first. A
  held Back is consumed before returning to the reader. The display updates for
  the searching screen and final results only. Partial results use the
  **Search results (partial)** header; failures and incomplete scans show their
  own messages.
- Matching decodes UTF-8 and HTML entities, collapses whitespace, joins text
  across inline tags and separates block boundaries. Offsets count decoded
  body-text codepoints consistent with the reader's whitespace offsets. Head,
  script, style, title and ruby fallback text are excluded. Internal DTD subsets
  are rejected; external entities are never fetched. A malformed chapter rolls
  back its own results but keeps earlier valid chapters.
- Case folding covers ASCII, Latin-1 uppercase and basic Greek and Cyrillic
  uppercase only; no full Unicode folding or accent normalization. CSS-hidden
  text can match. Phrases do not cross spine items. More than 32 matches need a
  narrower query. No persistent index or match highlighting.

## Library changes recorded with the reader work

These were delivered alongside reader reliability work and are summarized here
so they are not lost; the Library document is authoritative for the index.

- Whole-library search keeps the 96-byte folded title prefix as the fast path. A
  miss near that limit (UTF-8 truncation can stop the prefix at byte 93) checks
  the full stored title, or the filename stem when metadata is absent. The stored
  title/name blob is itself limited to 255 bytes. Author and series matching
  remain.
- Combined text and shelf filters test text before opening per-book state files,
  so a narrow query reads index records but only matching books' path hash and
  state file; an empty text query still checks every needed shelf state. Read
  failures discard partial results and show the filter error. Filtering reuses
  one local string, reserving 255 bytes only after the first prefix miss (a
  256-byte stack buffer would duplicate storage and exceed the stack guideline).
  Search and filtering run synchronously; the 4,096-book limit and index format
  are unchanged.
- Author identity uses the complete normalized token sequence. The 12-byte
  record field holds a four-byte prefix and eight-byte fingerprint; the builder
  compares normalized identities before sharing a canonical spelling, and a
  fingerprint collision fails the new build and keeps the prior index (so
  Christopher Tolkien/Priest/Paolini stay separate). Directory scans compare
  record keys, not truncated captions. The 128-byte record and 14-byte-per-book
  sort scratch are unchanged.
- Library recovery validates every prior record and path hash before treating a
  backup as obsolete; read/seek errors keep both copies and defer rebuilding.
  Validation streams one reused 128-byte record at a time and yields.

## Known gaps

- Title ordering uses only a 12-byte folded prefix.
- Bookmark filenames flatten `/` and `\` to `_` (`src/util/BookmarkUtil.cpp`),
  so distinct SD paths can share a bookmark file; fixing it needs a migration
  that preserves existing bookmarks.
- STL containers in the parser and reader still use the default (aborting)
  allocator; only paragraph/page allocations are checked nothrow.
- Find in Book limits and gaps are listed in its section.

## Tests

Host tests build through the root `test/` CMake project and compile production
methods rather than reimplementing decisions:

- Page turns and refresh: `test/reader_navigation`, `test/reader_overlay`
  (includes the queued-turn clearing on overlay/suspend and byte-exact toolbar
  snapshot restore in four orientations), `test/manual_refresh`,
  `test/reader_grayscale_plan`, `test/reader_progress_state`,
  `test/epub_page_turn` (real cached-page pipeline and prefetch reuse),
  `test/gfx_refresh` (full-height vs 80-row composition, byte-for-byte),
  `test/sd_card_font` (instrumented SD-font I/O), `test/refresh_sequences`
  (SDK facade and X4 Pro drivers against a recording bus).
- Navigation and scheduling: `test/reader_link_navigation`,
  `test/reader_incremental`, `test/reader_page_recovery`, `test/render_lock`,
  `test/button_navigator` (allocation hook requires zero allocations).
- Cache integrity: `test/section_persistence`,
  `test/section_parser_integration`, `test/parser_failure`.
- Persistence: `test/atomic_file`, `test/bookmark_actions`,
  `test/bookmark_persistence`.
- Search: `test/epub_search` (engine against firmware `lib/expat` with
  `XML_GE=0`, `XML_CONTEXT_BYTES=1024`, plus the activity), and
  `test/reader_search_handoff` (menu dispatch, cache release, launch OOM,
  cancel/resume coordinates, invalid and intentional result navigation).

Optional benchmarks: configure with `-DCROSSPOINT_BUILD_PAGE_BENCHMARK=ON` and
`-DCROSSPOINT_BUILD_EPUB_PAGE_BENCHMARK=ON`, then run
`GfxPageBenchmark` / `EpubPageTurnBenchmark`,
`test/sd_card_font/run_benchmark.py` and `test/refresh_sequences/run.py`.
Elapsed-time thresholds are excluded from unit tests; exact bytes, operation
counts and failure recovery are the regression gates.

Device checks (AA off): turn pages both ways quickly and during pagination of a
long uncached chapter; open and close the toolbar and pushed screens with a turn
pending; pull-down Refresh; follow a footnote, jump elsewhere and reopen; try
percentage and backward chapter jumps; add/rename/delete a bookmark; run and
cancel Find in Book; sleep/wake. A debug build can confirm heap stays above
50 KiB after repeated activity and search exits.

## Attribution

Preserve these human authors as `Co-Authored-By` when committing the
corresponding adaptations.

- [Reader #3521](https://github.com/crosspoint-reader/crosspoint-reader/pull/3521)
  (`c80c537f287506dbac4f99f0b97a89cffbe36302`) and
  [Reader #3527](https://github.com/crosspoint-reader/crosspoint-reader/pull/3527)
  (`c4d8c395dca44cbefc0e9d7bf16f1ac469ec6819`), font-cache fixes: authored by
  Sung-jin Brian Hong <serialx@serialx.net>, approved by Justin Mitchell.
- [PR 3698](https://github.com/crosspoint-reader/crosspoint-reader/pull/3698)
  (`21ff2fde859290e1759cd918a219aa16dc6ef6fa`), ButtonNavigator allocation
  removal, adopted intact and since merged upstream (the fork no longer carries it), and
  [PR 3652](https://github.com/crosspoint-reader/crosspoint-reader/pull/3652)
  (`0f1b86eeba28542fc2f5d5e6025806166203d307`), nonblocking render-lock
  scheduling and the `Mode`/`ownsLock` interface: Sung-jin Brian Hong
  <serialx@serialx.net> (verified from GitHub commit records). Both were open
  and awaiting review when adapted; no upstream acceptance is implied.
- [Reader #3113](https://github.com/crosspoint-reader/crosspoint-reader/pull/3113),
  focused TryAcquire/locked render-lock API first used for idle prefetch (since
  replaced by the #3652 interface): Erica Jensen <erica@mailershaven.com>.
- [PR #3495](https://github.com/crosspoint-reader/crosspoint-reader/pull/3495)
  (`7935842b55c91e8fc718961c93454bd04f990164`), link/footnote progress
  semantics: **Dan <dpatynski@gmail.com>** (`dfourn`).
- [PR #2457](https://github.com/crosspoint-reader/crosspoint-reader/pull/2457)
  (`995f3d4821a1985c31d98b28a191b8c2935e7428`,
  `7e0638f3bf8385ea6021fe7c970cf836c4b574da`), single-note shortcut and menu
  ordering: **Davide Masserut <dm@mssdvd.com>** (`mssdvd`). Its dynamic
  single-note label was not imported.
- [PR #3605](https://github.com/crosspoint-reader/crosspoint-reader/pull/3605)
  (reviewed at `f4b875e878404e837f92ac21a72a9c5488402c4d`), deferred percentage
  and backward chapter jumps: **asterism (0137)**,
  `1546578+0137@users.noreply.github.com`. The adaptation removes the
  full-chapter sentinel and dead full-build branch and integrates cancellation,
  progress guards and failure handling.
- [PR #3441](https://github.com/crosspoint-reader/crosspoint-reader/pull/3441),
  Find in Book idea (open when reviewed): AmirMohammad Cheraghali
  (`QuercusCode`), Git commit author address
  `QuercusCode@users.noreply.github.com`. The engine and
  lifecycle were rewritten to avoid persistent reading instrumentation and to
  bound parser resources.
