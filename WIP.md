# WIP handoff — develop on upstream 099e89b, X4 Pro r60, split patch series

## Repository state

The reader's local changes form a linear series above official reader `develop`
`099e89bc` (see `git log upstream/develop..develop`).
Since 2026-09-28 the former single 42,767-line X4 Pro commit is nine topical
patches, each built warning-free for `x4pro-gh_release` on its own:

1. fork settings, strings and shared utilities
2. SD storage, atomic files and SdFat caching
3. display refreshes and sleep images (sets the SDK pin)
4. bounded EPUB indexing and metadata I/O
5. incremental section layout and page cache
6. offline Library index and browser
7. on-demand in-book search
8. reader navigation, page turns and bookmarks
9. host-test wiring, user guide, file formats and compatibility notes

Then r51 cold indexing, fork instructions/setup, r52 (pagebreak fix, library
scan), r53 (queued page-turn fix, Calibre sort keys), a docs commit that
folds the 50 `-rNN` revision notes into six `docs/fork-*.md` feature docs, and
r55's three fixes (CSS/section cache invalidation from #3305, finished-book
move keeping state, stale leading TOC entry).
Upstream's own history is intact; there are no local merge commits. When
rebasing, a conflict now names its topic, and a patch that upstream supersedes
can be dropped or adapted on its own.

SDK `develop` is `eb74866d43acd9a771a5c75b4c998fbd01769ff0`: two local patches
(display transactions/refresh, then list and text-area components) above
`233922603467699775f5a61fd12ae7366cf1fbda`, the revision official reader pins.
`.gitmodules` resolves the SDK through the personal fork.

The reader fork is `endqwerty/crosspoint-reader`; the SDK fork is
`endqwerty/freeink-sdk`. The maintained checkout uses `origin` for the
personal fork and `upstream` for official upstream, and tracks `origin/develop`
in both repos. No PR was opened. Completed work is automatically committed,
integrated into local `develop` and pushed to personal `origin/develop` under
the standing authorization in `docs/FORK.md`. Other publication requires
explicit approval.

Persistent policy: read `docs/FORK.md`. Keep every local patch above the upstream
base, adapt or drop patches when upstream supersedes them, and use rebase plus
fast-forward/squash integration. No local merge commits.

### Upstream conflicts resolved on 2026-09-30

Rebased the 46-patch series onto `099e89bc` (five upstream commits: #3685,
#3727, #3805, #3114, #3764) and the SDK's two patches onto the new pin
`2339226` (24 SDK commits: Metalio E-Ink 4 board, haptics, BLE host, keyboard
layouts, resumable fetch). Upstream's design won every conflict.

- #3764 (reopen on the viewed page after following a link) replaces the fork's
  "closing the book inside a footnote restores its origin". Leaving the reader
  no longer rewrites progress; the Back destinations are saved to upstream's
  `links.bin` and restored on the next open. The fork keeps
  `ReaderNavigationHistory` as the in-memory form (page count and text offset
  for returns within a session); `saveFootnoteOrigin()` is gone and the link
  navigation and page recovery host tests assert the upstream behavior. Because
  `links.bin` holds only spine and page, a restored entry is a plain link
  return (`docs/fork-reader.md`, "Links, footnotes and history").
- #3685 (separate SdFat FAT cache) merged, so patch 2 no longer carries the
  build flag, the two cache patches or the hook. What remains local: the
  directory-pointer guard (`0003`), `GIT_OPTIONAL_LOCKS=0` in the hook and the
  `test/sdfat_cache` host tests.
- #3727 (configurable paragraph indentation) adds `paragraphIndentSpaces` to
  the section header and changes the `ParsedText` constructor. The fork's
  checked header read/write and `SectionPageReader::HEADER_SIZE` (now 44
  bytes) carry the field. The user declined this as a fork feature on
  2026-09-29; it is now simply upstream's setting, migrated by upstream's
  `migrateParagraphIndentSpaces()`.
- Section cache version is 51. Upstream took 50 for the indentation header
  while r52 to r59 used 50 for the pagebreak fix in a 43-byte header, so the
  fork's pagebreak patch moves to 51 and every book re-lays out its chapters
  once (`docs/file-formats.md`).
- #3114 (SD card plugin system, content protection, reading-session events)
  touches the reader: `Epub::load` opens upstream's encrypted-entry accessor
  before the fork's scoped ownership cleanup; page turns report to
  `ReaderSession` through `notePageTurn()` (including the fork's queued turn);
  "Move finished books to /Read" moves upstream's `.rights` sidecar after
  `moveBookWithState()` and moves the book back if that fails. Chapter and
  search reads still go through `readItemContentsToStream`, so upstream's
  decrypt-on-read path is intact. The fork does not use plugins or protected
  books; none of this is exercised beyond host tests and the build.
- #3805 (dead code) removed `ButtonNavigator::onNextRelease`; the Library
  details page calls `onRelease(getNextButtons(), …)` instead.
- A Czech and a Turkish string the fork had added (`STR_NO_RECENT_BOOKS`) now
  come from upstream; the fork's duplicates are dropped.
- SDK: upstream's Metalio black-pulse refresh and grayscale fallback paths in
  `Ssd1677Driver` gained the fork's bus-failure checks (`checkBus`, checked
  `refresh()`); `_pendingFrameSync` is cleared when the bus has failed. The
  display (`run_pro.py`, UC8253, UC8279) and FreeInkUI host tests pass.
- Seven fork test harnesses that compile extracted production code needed new
  seams for upstream's hooks (content protection, plugin events, reader
  session, load-failure popup, `esp_heap_caps.h`, `replaceFile`/`truncate`).

Only the tip of the rebased series was built and tested; the nine topical
patches were not rebuilt one by one this time.

### Upstream conflicts resolved on 2026-09-29

Rebased the 38-patch series onto `d1509d07` (three upstream commits: #3704,
#3732, #3773; the SDK pin is unchanged). Upstream's design won every conflict.

- #3704 (TXT/Markdown through the EPUB pipeline) deleted `TxtReaderActivity`.
  The fork's edits to it are dropped, and the reader navigation host tests no
  longer include a TXT activity (XTC/EPUB remain; the generic input tests now
  run on XTC).
- `Epub::load` keeps the fork's scoped ownership cleanup and adds upstream's
  TXT/MD branches; they set `loaded` so a successful TXT load keeps its owners.
  `Epub::loadMetadata` keeps upstream's TXT early return before the fork's
  always-reparse-the-package rule.
- `BookMetadataCache::buildBookBin` keeps the fork's chunked, bounded sizing;
  TXT/MD books take their single spine size from the source file (upstream's
  raw-file branch) and never use batch sizing.
- Book state moves follow upstream's cache naming: TXT/MD share `epub_` caches
  and keep bookmarks (`hasReflowableBookExtension`), so `BookStateMove.cpp` no
  longer looks for `txt_` caches. The stale-`txt_` prefix check in
  `isBookCacheDirectoryName` is upstream's and stays.
- Test stubs gained the `Txt`/`FsHelpers` names `Epub.cpp` and
  `BookMetadataCache.cpp` now use. The Library index still lists `.txt`/`.md`
  by filename and reads metadata only from EPUBs.

### Upstream conflicts resolved on 2026-09-28

- Upstream #3755 redesigned the keyboard. The fork's older keyboard sizing
  (full-width touch rows, `keyboardRowSpacing`, 2 px key gaps, X3 side insets,
  a language key on the symbols layer and its URL rows) conflicted and is
  dropped: `KeyboardEntryActivity.cpp` and the theme keyboard metrics now match
  upstream exactly. The X4 Pro only uses the keyboard for Library search.
- Upstream #3754 (press-based list navigation, previously "wait for merge") is
  now in the base. The fork's Library UI and ButtonNavigator host tests were
  updated to its press/hold/release contract; no fork source change was needed.
- SDK: upstream's new list reveal action and keyboard changes were kept in the
  fork's formatting; the fork's display/list/text-area patch is unchanged.
- Upstream #3698 (ButtonNavigator allocation churn, adopted earlier) merged as
  `f03d7f4`; the fork's copy was identical, so it dropped out of the series.
- Upstream `ce9f5c2` (header back-button tap routing) moved the SDK pin to
  `87c4493` (keyboard alignment, list separators and checkboxes, atomic SD
  writes, WebDAV parsers). The fork's list patch conflicted only in formatting
  of the toggle drawing; upstream's checkbox branch is kept.

## Recovery and verification

The series before the 2026-09-30 rebase (reader `e09fdec2` on `d1509d07`, SDK
`d466732a` on `87c4493a`) is in
`/Users/danielyang/.local/share/crosspoint-build/rebase-backup-20260930/`
(`develop-e09fdec2.bundle` and `sdk-develop-d466732a.bundle`, verified `git
bundle` files of the local patches). The series before the 2026-09-29 rebase
(reader `d9562882` on `ce9f5c28`) is in
`/Users/danielyang/.local/share/crosspoint-build/rebase-backup-20260929/develop-d9562882.bundle`.

No backup branches or extra worktrees are kept; reader and SDK each have only
`develop`. History removed on 2026-09-28
(earlier develop tips, the pre-split series, old review/test branches and the
SDK's single-patch versions) is in verified bundles under
`/Users/danielyang/.local/share/crosspoint-build/branch-cleanup-20260928/`, with
`RESTORE.md`. The 2026-09-27 bundles in
`/Users/danielyang/.local/share/crosspoint-build/branch-cleanup-20260927/` still
cover the pre-cleanup history.

## Current flash image

Use `/Volumes/workspace/builds/crosspoint-reader/x4pro-r60-20260930-222847/firmware-x4pro-r60-8b8c7f73.bin`
(Windows: `\\<server>\workspace\builds\crosspoint-reader\x4pro-r60-20260930-222847\firmware-x4pro-r60-8b8c7f73.bin`).
The authoritative pointer is `/Volumes/workspace/builds/crosspoint-reader/FLASH-LATEST.md`.
Web flasher → Xteink X4 Pro → Custom .bin. Start with AA off.
Version: `1.6.5-dev-x4pro-r60-099e89b`.
SHA-256: `839a96a29a2f623f462393d4c61a72753e77b9f9859dfdf217781fa068c3c8fe`.
Earlier images stay in their dated folders under
`/Volumes/workspace/builds/crosspoint-reader/`; each folder's `build-info.json`
records its source. Builds before r54 predate the history split, so their
source commits are not in `develop`.

r60 is r59 rebased onto upstream `099e89b` and SDK `eb74866` (on the new pin
`2339226`); it adds no fork feature (conflict notes above). Firmware source is
commit `8b8c7f73`; later commits change only documentation. r59
(`x4pro-r59-20260929-184820`, source `d3463ee0`) added reading-state relinking
after Calibre renames (below). r58
(`x4pro-r58-20260929-181751`, source `45c37c9d`) added the cheaper first open for
books under 400 chapters (below).
r57 (`x4pro-r57-20260929-181116`, source `12e591c3`) added the end-of-book
suggestions from the Library index (below). r56
(`x4pro-r56-20260929-171110`, source `2c02149f`) is r55 rebased onto upstream
`d1509d0` (SDK `d466732` unchanged).

r60 validation (2026-09-30): all 1,706 native Release and 1,706 LLVM 22
ASan/UBSan tests pass (40 more than r59: upstream's new tests plus five fork
tests for the `links.bin` back-stack and upstream's reader hooks). The sanitizer run needs
`--timeout 180`: `GlyphRasterParity` takes about 70 s there, as in earlier runs.
The X4 Pro release build from a fresh worktree has no warnings in fork or
upstream sources; wolfSSL reports `NO_WOLFSSL_ESP32_CRYPT_RSA_PRI` redefined 132
times because upstream's new build flag repeats a define in the library's
`user_settings.h`. Static RAM 103,240 bytes (+888 over r59), linked flash
5,829,962 bytes (+146,304, upstream's plugin and content-protection code);
image 5,835,040 bytes, ESP32-S3 image inspection valid. SDK display and
FreeInkUI host tests pass.

Upstream changes in r60 the user may notice (for device testing):

- Every book re-lays out its chapters once on first open (section cache
  version 51).
- Closing a book while inside a footnote or after following a link reopens on
  that page, and Back still returns to where the link was followed. The fork
  used to reopen at the footnote's origin.
- Text settings gain upstream's paragraph indentation width; Settings gains a
  Plugins entry (unused here).
- FAT sectors use the separate SdFat cache through upstream's own patch set
  (same behavior as the fork's earlier copy).

r59 validation (2026-09-29): all 1,666 native Release and 1,666 LLVM 22
ASan/UBSan tests pass (11 new: `LibraryRenameRelinkTest` 5, `RelinkBookStateTest`
5, `removeBookState` 1). The clean X4 Pro release build has no warnings: static
RAM 102,352 bytes (unchanged), linked flash 5,683,658 bytes (+2,296 over r58);
image 5,688,672 bytes, ESP32-S3 image inspection valid.

r58 validation (2026-09-29): all 1,655 native Release and 1,655 LLVM 22
ASan/UBSan tests pass (5 new `TocLookup` tests plus a cold-open read gate). The
clean X4 Pro release build has no warnings: static RAM 102,352 bytes
(unchanged), linked flash 5,681,362 bytes (+244 over r57); image 5,686,384
bytes, ESP32-S3 image inspection valid.

r57 validation (2026-09-29): all 1,650 native Release and 1,650 LLVM 22
ASan/UBSan tests pass (10 new, `test/library_follow_ons`). The clean X4 Pro
release build has no warnings: static RAM 102,352 bytes (unchanged), linked
flash 5,681,118 bytes (+1,092 over r56); image 5,686,128 bytes, ESP32-S3 image
inspection valid.

r56 validation (2026-09-29): all 1,640 native Release and 1,640 LLVM 22
ASan/UBSan tests pass. The clean X4 Pro release build has no warnings: static
RAM 102,352 bytes (unchanged from r55), linked flash 5,680,026 bytes (7,152
fewer, mostly the deleted TXT reader); image 5,685,040 bytes, ESP32-S3 image
inspection valid. Upstream's TXT/Markdown reader now runs through the EPUB
pipeline; the fork does not exercise it beyond host tests.

Fork change in r59 (for device testing):

- Reading state follows a book that Calibre renamed. A re-export that edits a
  title or author moves the book to a new path, which used to strand its reader
  cache, bookmarks, reading state and Recent entry under the old path. The
  Library refresh already paired such books by UUID; it now journals the
  UUID-verified pairs and, after the new index is installed, calls
  `relinkRenamedBook()` (`src/util/LibraryRelink.cpp`), which moves the cache,
  bookmark files and reading state to the new path with rollback
  (`relinkBookState()` in `src/util/BookStateMove.cpp`), removes the old
  reading state and repoints the Recent entry and open-book path. Size-only
  matches are never relinked (a wrong pairing would give a book another's
  progress), and state already built at the new path is never overwritten (the
  old state stays orphaned). The Library entry now runs the refresh before
  pruning missing Recent entries so renamed books stay in Recent. Details in
  `docs/fork-library.md` ("Relinking after external renames"). Check on the
  device: after re-exporting a book whose title changed, the new entry keeps
  its Reading/Finished mark, bookmarks and place, and the Recent tab still
  lists it. Unmeasured: relink time for many renames (a few existence checks per
  renamed book, more if it has state).

Fork change in r58 (for device testing):

- TOC-to-spine lookup resumes at the previous match. Below 400 spine items each
  TOC entry used to reread the staged spine from its start while building
  `book.bin`; it now scans from the last matched entry to the end and wraps
  (8 bytes of state, no heap, no threshold or cache-format change). Host
  cold-open reads on the 128-chapter fixture: 33,556 to 1,552 (32 chapters:
  2,255 to 395); seeks and writes unchanged. Only a spine listing the same href
  twice (invalid) can resolve to a different occurrence. Details in
  `docs/fork-epub-indexing.md`. It affects each book's first open and any cache
  rebuild; device time is unmeasured.

Fork change in r57 (for device testing):

- End-of-book "Continue with" menu. It used to list later files from the book's
  own folder, which is always empty for the Calibre export (one folder per
  book). It now asks the Library index first: the later volumes of the book's
  series in series order, else the same author's later titles (never for
  books without an author identity), skipping Finished books and missing
  files; the folder scan is the last fallback. Up to three entries, built once
  per reader session, no index rebuild. Details in `docs/fork-library.md`
  ("End-of-book suggestions"); code `lib/LibraryIndex/LibraryFollowOns.cpp`
  and `EndOfBookOptions`. The index must have been built with metadata on for
  series to appear, and reflects the card as of the last Library refresh.

Fork changes in r55, carried by r56 to r60 (for device testing):

- CSS/section cache invalidation (adopted from open upstream #3305). Opening a
  book whose CSS cache was invalid or failed to hydrate deleted it; if the
  reparse then failed (for example on low heap), old sections survived beside
  new chapters laid out without CSS. Sections are now dropped whenever the
  rule set behind them was replaced or deleted, and always after a `book.bin`
  rebuild. No cache format change.
- "Move finished books to /Read" now carries bookmarks, the Library reading
  state (including the Finished mark) and the reader cache, rolls everything
  back on failure, skips destination names with leftover state and marks the
  Library index dirty. It shares `moveBookWithState()`
  (`src/util/BookStateMove.cpp`) with Browse Files rename.
- Calibre's stale NCX "Cover" entry (117 books): an unresolved TOC entry ahead
  of every resolved one opens the start of the book instead of closing the
  chapter list. Decided at selection time; `book.bin` is unchanged.

Earlier fork changes since r51 that r55 carries (for device testing):

- Queued page turns: a turn pressed during a page update is queued and applied
  when the update ends. Opening the toolbar from the home button, or a pushed
  screen (dictionary, footnote selection, sync), used to leave it queued, so the
  page flipped with no input after returning. `openOverlay()` and `onSuspend()`
  clear it. Compared with upstream #3636, this is the one case the fork's own
  queued-turn handling missed; the rest of #3636 duplicates fork code.
- Calibre sort keys (Library index format 5, record still 128 bytes). The parser
  reads the title sort (title file-as, else `calibre:title_sort`), the first
  creator's file-as when that creator is an author, and the book UUID
  (uuid-scheme/`uuid_id` identifier, else `uuid:`; the "calibre" scheme changes
  between conversions). Title order, group letters and search use the title
  sort, so 199 "The …"/"A …" books move to their Calibre place; search also
  matches the shown title. Author order uses the author sort (surname guess as
  fallback) and headings show it unless written in capitals. Renamed books keep
  their Added position by UUID. The first Library open after moving from an
  index-format-4 build re-reads all package documents once; arrival order is
  kept. Across the 750-book export the parser finds a title sort in 750, an
  author sort in 749 (the EPUB 3 book lists its illustrator first) and the
  expected UUID in 747 of 747. (Relinking progress and bookmarks across
  renames followed in r59.)
- Pagebreak markers (`role="doc-pagebreak"`/`epub:type="pagebreak"`) no longer
  drop book text: tagged paragraphs, headings, list items and blockquotes
  render; other markers drop only their own label or a bare page number and
  replay anything else at its reading offsets (adapted from #3349). Section
  cache version 50 then, 51 since r60 (upstream took 50): each book re-lays out
  its chapters once on first open.

Design details and attributions are in `docs/fork-reader.md`,
`docs/fork-library.md` and `docs/fork-layout.md`.

## Library scan (item 5, done 2026-09-28)

`scripts/scan-epub-library.py` scanned the full Calibre export at
`/Volumes/media/Book Export` (750 EPUBs, 749 written by calibre 9.14.0), read-only.
Chapters are parsed with Expat configured like the reader.

| Pattern (upstream PR) | Books |
| --- | --- |
| pagebreak marker/paragraph holding text (#3349) | 0 |
| chapter Expat rejects, incl. unclosed `<br>` (#3375) | 0 |
| TOC href matching only by file name (#2987) | 0 |
| TOC href matching no spine item | 117 |
| landmarks nav nested in toc nav (#2297) | 0 |
| SVG/XHTML cover-image wrapper (#3539) | 0 |
| ZIP comment beyond the 1 KB scan (#2614) | 0 |
| image extension misnaming its format (#2386) | 0 |

No book in the library uses pagebreak attributes (one book has only CSS classes
named "pagebreak"), so r52's pagebreak fix changes nothing for the current
library; it protects publisher EPUBs added later. The 117 unresolved TOC entries
are all one stale NCX "Cover" entry to `OEBPS/c0.xhtml`, a file Calibre removed
when it replaced the cover with its titlepage. That row cannot be selected
on the device; the rest of each TOC works. None of #3375, #2987, #2297, #3539,
#2614 or #2386 is needed for this library. Rerun the scan after large imports.

## Remaining work and limits

The user does not take manual device measurements (recorded 2026-09-29). This
is a personal project for the user's own reading: faster, cheaper page turns,
less ghosting and a fast Library refresh, all of which are working on the X4
Pro in ordinary use. Nothing below waits on a hardware measurement. Device
timing, peak heap, ghosting, BUSY recovery and power-loss behavior stay
unmeasured, and docs must not claim otherwise. The evidence for a change is host
tests, operation and allocation counts, static RAM and a clean X4 Pro build;
the user's normal reading is the only device exercise, and a regression they
notice is reported back rather than measured. Prefer changes whose benefit is
deterministic (fewer SD reads, fewer refresh activations, less RAM) over ones
that trade latency against battery or heap in ways only a device can settle.

Official upstream was fetched on 2026-09-30 at `099e89bc` and the fork is
rebased onto it (conflicts above). Recheck upstream and open PRs when starting
new work. #3705 and #3675 remain deferred for
cold-layout/input-responsiveness concerns; earlier evidence is in
`/Volumes/workspace/builds/crosspoint-reader/upstream-review-r50/REVIEW.md`.

Cold-open profiling is host-only: it uses archive/storage doubles, so it lacks
real ZIP/container, CSS and first-page layout costs. The 128-chapter fixture
now reads 1,552 times in its cold open (was 33,556 before r58).

Unverified on hardware after r55 to r60 (informal only; the user will notice if any
misbehaves): after r60, closing a book inside a footnote reopens on the note and
Back returns to the link's page, and books open normally after the one-time
re-layout; choosing "Cover" in a Calibre book's chapter list opens its first
page; "Move finished books to /Read" keeps bookmarks, the Finished mark and the
Library entry; a page turn pressed during a page update no longer fires after
opening the toolbar with the home button; Library search (upstream keyboard)
and list navigation; the first open of each book after flashing re-lays out
chapters without errors; the end-of-book menu shows the next volume (or the
author's next title) of a Calibre book and opens it.

No feature implementation is active. Start the next isolated worktree from
personal `develop` and follow `docs/FORK.md`'s startup procedure. The roadmap
below is optional future scope, not unfinished work blocking deletion.

## Proposed next steps

Replanned 2026-09-29 after the user said they will not measure device
performance and asked for further performance and UI improvements. Priorities
follow the offline-EPUB, X4 Pro and Calibre-library focus in `docs/FORK.md`.
Each item needs the user's go-ahead. Order is the recommended order.

1. Done 2026-09-29 (r56): rebased onto upstream `d1509d07`; see the
   conflict notes above. Rebase conflicts stayed in `Epub.cpp`,
   `BookMetadataCache.cpp`, `FileBrowserActivity.cpp` and `BookCacheUtils.cpp`,
   so splitting those hotspot files by hunk (item 8) is still not worth it.
2. Done 2026-09-29 (r57): end-of-book "next in series" menu from the Library
   index (see the r57 notes above and `docs/fork-library.md`). Possible
   follow-up only if it feels wrong in use: show the series name or volume in
   the row, or offer the previous unfinished volume.
3. Done 2026-09-29 (r58): cold-open TOC lookup resumes at the previous match
   (see the r58 notes above and `docs/fork-epub-indexing.md`).
4. Declined by the user on 2026-09-29: time left in chapter/book (#3642),
   paragraph indentation override (#3727), whole-book page estimates (#2350)
   and estimate marker placement (#3758). Do not propose reading-display
   features again unless the user asks; they want speed, ghosting and Library
   handling, not more reader UI.
5. Only if the user notices it: chapter-boundary latency. Idle prefetch
   decodes only the next page of the current section
   (`docs/fork-layout.md`); turning into a chapter with no section cache waits
   for the first page of its incremental layout. Pre-building the next section
   near a chapter's end would remove that wait but adds idle CPU, SD writes and
   heap fragmentation, the same trade-offs that keep #3705, #3675 and #3060
   deferred. It cannot be validated without the device, so do not start it
   unprompted.
6. Done 2026-09-29 (r59): relink reading state across Calibre renames (see the r59
   notes above and `docs/fork-library.md`). Possible follow-up only if the
   device shows a gap: relinking books that lack a UUID (needs a safe identity
   other than size), or relinking a rename plus a same-UUID copy.
7. X4 Pro internal heap: PR #3488 (serialx) rebuilds TinyUSB with only MSC/CDC
   and recovers most of the 12,248 bytes lost with the Arduino upgrade in #3397.
   It is a build-system change under maintainer test; wait for upstream to merge
   it, then rebase onto it rather than carrying it.
8. Hotspot patch splitting (files `LibraryListActivity.cpp`,
   `EpubReaderActivity.cpp`, `Section.cpp`, `BookMetadataCache.cpp`,
   `ChapterHtmlSlimParser.cpp`): only worth it if a rebase conflicts there
   repeatedly. Item 1 will show whether it does.

Retired on 2026-09-29: the old "device validation of r55" item. Not planned without a device: further anti-ghosting
work, idle-power tuning (#3060), and prefetch changes that trade latency for
battery.

Done earlier, for reference: r52 pagebreak markers (#3349) and the library
scan (`scripts/scan-epub-library.py`, no #3375/#2987/#2297/#3539/#2614/#2386
patterns in this library); r53 Calibre sort keys and the queued-turn fix; r55
CSS/section cache invalidation (#3305), the finished-book move and the stale NCX
"Cover" entry; 2026-09-28 the nine-patch split and the revision notes folded
into `docs/fork-*.md`. Old item 4 (re-export cost) is closed: reconcile reuses
metadata only when size and mtime match (`reuseMetadata` in
`LibraryBuilder.cpp`'s `stageRecord()`); `scripts/sync-calibre-library.sh`
copies by content without source mtimes.

Upstream PR triage, 2026-09-27, rechecked 2026-09-29:

- Already in the fork, or superseded by fork code:
  - merged upstream and in the base since 2026-09-28: #3698, #3754, #3755,
    #3765, #3766
  - merged upstream and in the base since 2026-09-29: #3704, #3732, #3773
  - merged upstream and in the base since 2026-09-30: #3685 (the fork's copy
    dropped out), #3764 (upstream's behavior replaces the fork's), #3727,
    #3114, #3805
  - adapted in r52: #3349
  - adopted: #3441, #3495, #3733, #3027, #3605, #3419, #2438, #3113,
    #2603, #3305 (r55)
  - #2602: flat CSS rule pools (`CssParser.h`: `SelectorEntry`,
    `selectorPool_`, `stylePool_`)
  - #2343: ordered lists (`ChapterHtmlSlimParser.cpp`: list context
    `ctx.ordered` for `<ol>`)
  - #3452: checked `readStringChecked`. The only unchecked callers left
    read the parser's own temp item store (`ContentOpfParser.cpp` spine idref
    lookup, `serialization::readString(self->tempItemStore, …)`).
- Still deferred: #3705 and #3675 (drafts with open regressions).
- Watch only, import after merge: #3706 (hyphenation manager) and #3757 as a
  whole.
- Not adopted: large feature PRs outside this focus, such as
  highlights/clippings (#3589, #2617, #1742, #1478), drop caps (#2387), GIF
  (#2299) and table borders (#2954).
- Skipped by `docs/FORK.md` policy: OPDS, KOReader sync, WebDAV/web server,
  plugins, BLE, other boards, keyboards and translations.

Workspace note: the checkout and its git data live on local disk at
`~/workspace/crosspoint-reader`, and `~/.t3/worktrees` is a local folder. The
earlier SMB checkout under `/Volumes/workspace/projects/crosspoint-reader` is
retired: the macOS SMB client rejects `F_FULLFSYNC`, which T3 Code's checkpoint
`git add` forces, and worktree creation and submodule checkout took minutes there.
Exported builds moved to `/Volumes/workspace/builds/crosspoint-reader/`.
`core.untrackedCache` is enabled. Machine-level setup (T3 Code worktree cleanup job,
agent instruction files, toolchains) is in `/Volumes/workspace/homelab/workstation.md`. The SDK's nested `libs/assets/Icons/lucide`
submodule holds only icon-generator source SVGs and is not needed to build.
