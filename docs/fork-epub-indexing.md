# EPUB loading and indexing

## Purpose and behavior

Opening an EPUB loads its derived metadata cache (`book.bin`) from the book's
cache directory. When the cache is missing or invalid, the first open indexes the
book: it parses the OPF package, the table of contents (EPUB 3 nav preferred, NCX
fallback), sizes every spine item from the ZIP central directory and publishes a
new `book.bin`. Later opens only validate and read that cache. A book without a
usable TOC still opens and reads. Library metadata scans parse only OPF display
metadata and never build the indexing structures described below.

The `book.bin` byte layout (version 10) is documented in `docs/file-formats.md`.
Cache identity is path-based: replacing EPUB content at the same path still relies
on the existing invalidation rules, and a copy under a new filename gets a separate
cache.

## Load orchestration (`Epub::load`)

- `BookMetadataCache` and `CssParser` owners are allocated with
  `makeUniqueNoThrow` and null-checked; bare `new` would abort on OOM with
  exceptions disabled.
- Before allocating a replacement on a repeated load or post-build reload, the
  previous owner is reset first. `reset(new T(...))` would construct the new object
  while the old one and its data were still alive.
- A stack-only `ScopedCleanup` releases both owners on any failure path (no
  `std::function`, no heap). A later retry starts with no retained metadata handle
  or CSS rules. Failed reloads never remove published metadata, so a retry can use it.
- Cached path: on a CSS cache that is missing, partial or invalid, the OPF is
  reparsed into an empty, scoped metadata object that ends before CSS discovery and
  parsing (`parseContentOpf` overwrites all five output fields, so no cached copy is
  needed). Metadata is released during CSS parsing and reloaded from `book.bin`
  afterwards. A low-memory CSS cache load keeps the cache for a later retry.
  Section files carry no CSS identity, so `CssParser::sectionCacheIsStale()`
  removes the book's `sections/` directory whenever the rule set they were built
  from was replaced or deleted (an invalid or unhydratable cache, even when the
  reparse then fails). A preserved partial cache or a failed parse with nothing
  deleted keeps them. The resolved CSS rule map is cleared after load; section
  building reloads it on demand.
- Fresh-index path: the parsed metadata's scope ends after `book.bin` publication
  and temp-file cleanup, before CSS parsing and the reload, including when external
  CSS is disabled. CSS is parsed before `book.bin` is reloaded to leave heap for
  rule-table growth, and `sections/` is removed whatever the parse returns. Inline
  CSS still works when external CSS is disabled.
- Temporary-file cleanup is best-effort; its failure is logged and ignored.

These changes shorten existing allocation lifetimes and remove a metadata copy;
the saving depends on string sizes, and no fixed heap figure is claimed.
Standard-library strings and parser internals are not all fallible.

## TOC selection and fallback

The loader prefers EPUB 3 nav, falls back to NCX when nav fails or is absent, and
continues without a TOC when neither succeeds. Nav and NCX parsers stream entries
into one shared staging file. A failed parser attempt closes the TOC pass and begins
a fresh one, which truncates the staging file and resets the entry count, so a
partial nav can never be merged with the NCX result or published on its own.
Failure to close or reopen the staging pass stops publication.

A restart only happens on the error path. It releases the temporary writer and
large-book href index before recreating them (same checked allocation and
unbuffered fallback), and may reread the spine and flush discarded entries.
The fix applies when metadata is rebuilt; already indexed books keep their caches.
The unmerged sparse-nav selection heuristic of PR #2603 is not adopted.

A TOC entry whose href matches no spine item keeps spine index -1. Calibre
replaces a book's cover page with its titlepage but keeps the NCX "Cover" entry
to the removed file (117 of the 750 books in the library scan). In the chapter
list, an unresolved entry ahead of every resolved one opens the start of the
book; later unresolved entries close the list. This is decided at selection
time, so `book.bin` is unchanged.

## Bounded first-open indexing

- The OPF manifest index and the large-book TOC href index are `ChunkedVector`s
  (`lib/Memory/ChunkedVector.h`): lazily allocated, non-relocating, fallible chunks.
  Both instantiations are `ChunkedVector<..., 32, 256, 256>`: a 256-pointer inline
  directory (1 KiB on device), chunks growing from 32 to 256 elements. Largest
  payload requests are about 3 KiB (manifest) and 4 KiB (TOC). Growth never copies
  entries; a failed allocation stops the pass instead of calling throwing `new`.
  Random-access iterators support the existing sort/lookup algorithms.
- Admission guards: the manifest index and TOC href index require 16 KiB of
  reported free heap in reserve. These are admission checks, not a guarantee of
  device peak heap; fragmentation can still force a clean refusal.
- The TOC href index is used at 400 or more spine items
  (`LARGE_SPINE_THRESHOLD`). Smaller books use a linear TOC-to-spine search, which
  is a known remaining cold-open bottleneck (a 128-chapter fixture performs more
  reads than an indexed 512-chapter one); changing that threshold needs its own
  memory/correctness comparison.
- Building the href index reads the staged spine through a transient 512-byte
  `BufferedFileReader`, freed before TOC parsing. On allocation failure it falls back
  to checked unbuffered reads. A 512-byte stack buffer would exceed the 256-byte
  local-buffer rule. This halves HAL reads/mutex entries for that pass; bytes read,
  seeks and writes are unchanged.
- More than 32,768 spine or TOC entries (the signed 16-bit cache index range), or
  cumulative uncompressed chapter sizes beyond `UINT32_MAX`, are rejected rather
  than wrapping cache fields.
- Index storage is released after parsing. No global pool, resident buffer,
  background task or reading-loop work is added.

## `book.bin` assembly (`BookMetadataCache::buildBookBin`)

- Three transient 4 KiB streams batch staging reads and output writes; they are
  freed on return, and the output writer keeps its unbuffered fallback.
- Books with 400+ spine items size chapters in chunks. One checked workspace of
  three arrays (`int16_t` TOC index, `uint32_t` size, `ZipFile::SizeTarget`) is
  allocated once and reused across chunks. Chunk length is
  `(free heap - 32 KiB) / per-item bytes`, capped at the spine count; a budget below
  64 items (or the spine count if smaller) refuses the build. Large books may need
  several TOC and ZIP-directory scans; a book fitting one chunk keeps one batch ZIP
  scan, and books under 400 items keep individual size lookups. Sparse TOCs crossing
  chunk boundaries produce byte-identical output to a single-chunk build.
- Spine and TOC records are decoded by one shared bounded reader. Every length,
  read and seek is checked against the staging file bounds; invalid contents
  destinations and trailing staging bytes are rejected. Staging file sizes use the
  HAL's 64-bit API and must fit 32 bits; output offsets are checked with 64-bit
  arithmetic.
- Fields are limited to 4 KiB (`MAX_CACHE_FIELD_BYTES`); paths, anchors and core
  metadata fields over that limit or containing NUL are rejected (core fields before
  the output is opened). Long TOC captions are trimmed at a UTF-8 boundary, and the
  TOC offset table accounts for the trimmed length so later destinations stay valid.
  Original EPUB parsing can still allocate longer input strings; OPF display
  metadata is separately capped at 512 bytes.
- One `SpineEntry`/`TocEntry` record is reused per scan instead of constructing
  string-owning records every iteration. Explicit scopes destroy it before the next
  phase, in particular before the sizing budget is measured. Because strings keep
  their largest capacity for the scan, live heap can be higher for inputs whose long
  fields peak on different records; this cuts allocation churn, not guaranteed peak.
- Buffered-pass flush failures and staging close failures latch the pass as failed
  until the next `beginWrite`. The raw fallback writer checks each write and stops
  accepting records after a failure.

## Publication and recovery

`book.bin` is never truncated in place:

1. Stream into `book.bin.new`; check flush and close (SdFat's close-time sync can
   fail).
2. Rename the current `book.bin` to `book.bin.bak`, install the replacement, then
   remove the backup to commit.

- While `book.bin.bak` exists it is authoritative for reads. A later build restores
  it before attempting another replacement.
- Failed rollback leaves the backup in place. A failed pending-file cleanup leaves an
  ignored orphan that the next build truncates.
- Readers never publish a pending file and never mutate files during recovery. A
  corrupt authoritative backup fails normal validation instead of exposing an
  uncommitted replacement.
- The three paths share one checked allocation of `3 * (cache dir length + 14)`
  bytes (`sizeof("/book.bin.new")` includes the NUL), freed at load/build return.
  A fixed stack array would bound path length; a resident table would cost memory
  while reading.
- Publication temporarily needs SD space for both old and new metadata. The
  whole-document `AtomicFile` helper is not used because it is capped at 50 KB and
  needs a complete buffer; metadata keeps its streaming writer. Builds keep a
  single-owner lifecycle; this is not a concurrent-writer facility.
- This handles I/O failure and interrupted operations; FAT power-loss durability
  is not proven.

## Cache loading and validation (`BookMetadataCache::load`)

- Header, count and offset arithmetic, and available bytes, are validated before
  allocation. Strings may not exceed their record or 4 KiB, and may not contain NUL.
  Spine LUT positions, cumulative sizes and TOC references are validated at load;
  individual TOC records are checked when requested (no eager caption scan).
- Cumulative sizes use four bytes per chapter in one fallible allocation (not a
  `vector::reserve`, which can abort). That array is temporarily reused for LUT
  validation before each offset is replaced by its size.
- The size load skips href strings without constructing them, through one
  transient 4 KiB `BufferedFileReader` with checked unbuffered fallback. A 4 KiB
  stack buffer exceeds the C3 stack budget; a permanent buffer would outlive load.
  Progress queries afterwards need no SD I/O.
- A failed load discards the handle, metadata, counts and size cache, so a prior
  book's data never remains visible. A failed individual entry read returns an
  entirely empty entry while keeping validated counts and sizes, so an SD fault is
  never mistaken for end of book; later calls retry through the same checked reader.
  Invalid caller indices also return an empty entry without changing state.
- Caches that fail validation go through the normal rebuild/error path.

## ZIP directory scans and extraction (`lib/ZipFile`)

- The four central-directory readers (single size lookup, eager stat-cache fill,
  batch sizing, enumeration) share one checked decoder. It reads the complete
  46-byte fixed header, then the filename into a caller buffer of 255 bytes that is
  explicitly length-delimited. Longer names are skipped intact.
- Declared entry lengths are checked against the directory boundary before any
  skip; every read and seek must succeed; loops are bounded by the declared entry
  count. ZIP state carries a 32-bit directory boundary and a 16-bit cursor ordinal to
  bound scans and preserve sequential-cursor wraparound with optional directory
  signatures. A persistent I/O error cannot spin the scan.
- The end record validates single-disk counts, directory range and comment length.
  The trailer search uses a scoped fallible buffer of at most 1 KiB. Integer fields
  are decoded with `memcpy`/byte assembly, never unaligned loads or signed shifts.
  Layouts follow PKWARE APPNOTE sections 4.3.12-4.3.16.
- Batch sizing takes non-owning spans over the caller's arrays and returns -1 on I/O
  or structural failure; `BookMetadataCache` rejects that and discards the pending
  output. A filename missing from a readable archive is a normal unmatched result.
  Failed eager stat-cache population clears partial entries before retry.
- Extraction rejects zero-sized buffers, checks local-header and payload seeks,
  rejects negative stored-stream reads before they become unsigned lengths, and
  checks output-allocation arithmetic in `size_t`.
- The decoder adds no heap buffer. Existing stat-cache allocation behavior is
  unchanged; not every ZIP allocation is fallible.

## XML parser configuration

`platformio.ini` sets `XML_CONTEXT_BYTES=0` (with `XML_GE=0`). No application code
calls `XML_GetInputContext`, so retaining already-parsed bytes is unnecessary. Expat
still keeps incomplete tokens and resume input, so valid token length is not newly
limited. The mechanism: with retained context, Expat grows its input buffer and
briefly owns old and new storage; without it that growth is avoided on typical
streams. Savings depend on token lengths and chunk boundaries and are not a fixed
per-parser figure.

Host tests build the reader-owned `lib/expat` through `test/cmake/FirmwareExpat.cmake`,
which reads `XML_GE` and `XML_CONTEXT_BYTES` from `platformio.ini`, so parser tests
match firmware configuration. A host-only target also builds the same source with
1024 bytes of context for comparison.

## Related read paths

- Cached page loading reads adjacent geometry, text-header and style fields in
  small groups instead of one HAL read per scalar. Layout and cache version are
  unchanged; signed fields use aligned arrays or `memcpy`, and all enum, boolean,
  length, allocation and truncation checks remain.
- Resume/search offset lookup scans the visible-offset table sixteen entries at a
  time through a 64-byte stack buffer (`Section.cpp`), keeping first/last duplicate
  selection and incomplete-chapter behavior. Every seek/read is checked and 64-bit
  bounds checks reject truncated or overflowing tables; the reverse page-to-offset
  lookup checks I/O and whole-table bounds. Unreadable or malformed tables return
  no position. Known offsets in active builds still resolve without SD access.
- The Library index tracks its file cursor in one `uint32_t` to skip redundant
  seeks; failures, close and reopen invalidate it. One metadata reader collects
  title and canonical/source author in a single pass through a 64-byte stack window,
  reuses caller strings, clears outputs on failure and distinguishes empty fields
  from failed reads. There is no whole-library RAM cache.
- Ordinary glyphs resolve clipping, rotation and framebuffer coordinates once per
  glyph and paint packed pixels directly into the framebuffer or gray strip (from
  PR #3633). Scaled super/subscript and explicitly rotated text keep their existing
  paths. No heap, framebuffer, waveform or refresh change.
- Long-title truncation uses upstream's `GfxRenderer::truncatedText` binary search
  (PR #3573) verbatim: one retained input string plus one reusable candidate.

## Known gaps

- Device peak heap/stack, SD latency and power-loss behavior of indexing and
  publication are unmeasured; host operation and allocation counts are not device
  timings.
- Linear TOC search below 400 spine items (see above).
- Full ZIP/container discovery, CSS parsing and initial page layout are outside
  the indexing I/O fixtures.
- The SdFat FAT-cache proposal (PR #3685) is deferred until a real-SdFat
  fault/operation-count harness exists.

## Code and tests

Code: `lib/Epub/Epub.cpp` (`Epub::load`, `parseContentOpf`, TOC parsing),
`lib/Epub/Epub/BookMetadataCache.{h,cpp}`, `lib/Epub/Epub/parsers/`
(`ContentOpfParser`, `TocNavParser`, `TocNcxParser`, `ContainerParser`),
`lib/Memory/ChunkedVector.h`, `lib/ZipFile/ZipFile.{h,cpp}`, `platformio.ini`.

Host tests under `test/`:

- `book_metadata_cache`: cache writer/reader, fault-injecting HAL, publication and
  backup recovery, staging decode, reader EOF contract (`ReaderContract.h`).
- `epub_load`: extracts the production `Epub::load` via CMake with simulated
  metadata/CSS/storage collaborators (ownership, cleanup/retry, CSS and TOC policy,
  metadata lifetime).
- `epub_indexing`: extracts `Epub::load` and `parseContentOpf` and runs them with
  the real OPF/nav/NCX parsers, `BookMetadataCache` and firmware Expat; Expat
  allocation-failure matrix, TOC fallback, cold/warm I/O counts.
- `huge_book_index`: thousands-of-chapter indexing under a heap cap (`HeapCap`).
- `chunked_vector`, `zip_file`, `inflate_stream`, `expat_streaming`,
  `content_opf_parser`, `parser_failure`.

`epub_indexing` and `epub_load` locate methods in `Epub.cpp` by their signature
text and the following function (`bool Epub::loadMetadata(`,
`void Epub::discoverCssFilesFromZip()`); CMake fails with "method boundaries moved"
if a rebase renames or reorders them.

## Attribution

- [PR #3305](https://github.com/crosspoint-reader/crosspoint-reader/pull/3305),
  head `41d0eb2a`, by Sameh Foulad `<sameh@foulad.com>`: the section-cache
  invalidation predicate and its tests. Open, not merged upstream; drop the local
  patch if upstream merges it.
- [PR #2438](https://github.com/crosspoint-reader/crosspoint-reader/pull/2438),
  head `2635ed6cdaf0e0b8fb18e55af258bb587c9598c9`: `XML_CONTEXT_BYTES=0`. Open
  proposal, not an accepted upstream decision. Original author Erica Jensen
  (`erica@mailershaven.com`); any commit incorporating it must include
  `Co-Authored-By: Erica Jensen <erica@mailershaven.com>`.
- [PR #3733](https://github.com/crosspoint-reader/crosspoint-reader/pull/3733),
  head `0ec5d6ad471b978e516eb4443d33e974128e1242`, by Tuan Q. Nguyen
  `<tuan@tenor.vn>`: bounded spine-sizing approach, adapted with fallible chunked
  containers and failure cleanup. Open, not merged upstream; its Section/anchor
  changes are not applied.
- [PR #3027](https://github.com/crosspoint-reader/crosspoint-reader/pull/3027),
  head `1d069ae1c21433a4a3f4a0614bb8bca9d123288b`: chunk primitive and its original
  four tests; random-access iteration added locally. Open, not merged upstream.
- [PR #2603](https://github.com/crosspoint-reader/crosspoint-reader/pull/2603),
  reviewed head `d49f9bdbcde0427e64ce9f02d79455aacb3e6d1b`, by Ryan Jarvis
  (Cabalist): credited for the shared TOC-pass restart/counter-reset approach. Its
  sparse-nav heuristic is not adopted. Any commit adapting that work must keep the
  human co-author trailer recorded in the package attribution file.
- [PR #3633](https://github.com/crosspoint-reader/crosspoint-reader/pull/3633),
  head `92f76969b4e30ef93073c348c528468d99ec5932`, by Sung-jin Brian Hong
  `<serialx@serialx.net>`: direct packed-glyph rasterization. Preserve this human
  attribution if the adaptation is committed.
- [PR #3573](https://github.com/crosspoint-reader/crosspoint-reader/pull/3573),
  merged upstream as `ef08c3ad`: `truncatedText` binary search, used verbatim.
- PR #3685 (SdFat FAT cache), by Sung-jin Brian Hong `<serialx@serialx.net>`:
  reviewed and deferred here; the storage fork adapts its separate FAT cache
  (see `fork-storage-display.md`).
- ZIP record layouts: [PKWARE APPNOTE](https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT)
  sections 4.3.12-4.3.16.
