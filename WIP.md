# WIP handoff — develop on upstream d1509d0, X4 Pro r57, split patch series

## Repository state

The reader's local changes form a linear series above official reader `develop`
`d1509d07` (see `git log upstream/develop..develop`).
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

SDK `develop` is `d466732a9a10002e23e5c05eda53430cd2490101`: two local patches
(display transactions/refresh, then list and text-area components) above
`87c4493a6a5aa0c7c0e61aacc4a24e2c273e6895`, the revision official reader pins.
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

The series before the 2026-09-29 rebase (reader `d9562882` on `ce9f5c28`) is
in `/Users/danielyang/.local/share/crosspoint-build/rebase-backup-20260929/develop-d9562882.bundle`
(`git bundle` of `upstream/develop..HEAD`; also still on `origin/develop` until
the rebased branch is pushed).

No backup branches or extra worktrees are kept; reader and SDK each have only
`develop`. History removed on 2026-09-28
(earlier develop tips, the pre-split series, old review/test branches and the
SDK's single-patch versions) is in verified bundles under
`/Users/danielyang/.local/share/crosspoint-build/branch-cleanup-20260928/`, with
`RESTORE.md`. The 2026-09-27 bundles in
`/Users/danielyang/.local/share/crosspoint-build/branch-cleanup-20260927/` still
cover the pre-cleanup history.

## Current flash image

Use `/Volumes/workspace/builds/crosspoint-reader/x4pro-r57-20260929-181116/firmware-x4pro-r57-12e591c3.bin`
(Windows: `\\10.10.0.214\workspace\builds\crosspoint-reader\x4pro-r57-20260929-181116\firmware-x4pro-r57-12e591c3.bin`).
The authoritative pointer is `/Volumes/workspace/builds/crosspoint-reader/FLASH-LATEST.md`.
Web flasher → Xteink X4 Pro → Custom .bin. Start with AA off.
Version: `1.6.5-dev-x4pro-r57-d1509d0`.
SHA-256: `e448a1e808bb566c0b8d6130dab657405d58e31367eb24f689d5d06311364917`.
Earlier images stay in their dated folders under
`/Volumes/workspace/builds/crosspoint-reader/`; each folder's `build-info.json`
records its source. Builds before r54 predate the history split, so their
source commits are not in `develop`.

r57 is r56 plus end-of-book suggestions from the Library index (below). Firmware
source is commit `12e591c3`; later commits change only documentation. r56
(`x4pro-r56-20260929-171110`, source `2c02149f`) is r55 rebased onto upstream
`d1509d0` (SDK `d466732` unchanged).

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

Fork changes in r55, carried by r56 and r57 (for device testing):

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
  expected UUID in 747 of 747. Relinking progress and bookmarks across renames
  is not included.
- Pagebreak markers (`role="doc-pagebreak"`/`epub:type="pagebreak"`) no longer
  drop book text: tagged paragraphs, headings, list items and blockquotes
  render; other markers drop only their own label or a bare page number and
  replay anything else at its reading offsets (adapted from #3349). Section
  cache version 50: each book re-lays out its chapters once on first open.

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

Official upstream was fetched on 2026-09-29 at `d1509d07` and the fork is
rebased onto it (conflicts above). Recheck upstream and open PRs when starting
new work. #3705 and #3675 remain deferred for
cold-layout/input-responsiveness concerns; earlier evidence is in
`/Volumes/workspace/builds/crosspoint-reader/upstream-review-r50/REVIEW.md`.

The 128-chapter cold-open fixture still performs 33,556 HAL reads in its linear
TOC lookup (item 3 below). Broader cold-open profiling would need real
ZIP/container, CSS and first-page layout; the current fixture uses
archive/storage doubles.

Unverified on hardware after r55/r56/r57 (informal only; the user will notice if any
misbehaves): choosing "Cover" in a Calibre book's chapter list opens its first
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
3. Cold-open TOC lookup with a cursor hint (performance). For each TOC entry,
   `BookMetadataCache::createTocEntry` (`BookMetadataCache.cpp`) seeks the spine
   staging file to 0 and scans it linearly (below `LARGE_SPINE_THRESHOLD`, 400).
   TOCs are almost always in spine order, so remember the file offset and index
   after the last match, scan forward from there, then wrap. About 8 bytes of
   state, no heap, no threshold change. The gain is host-measurable (33,556 HAL
   reads on the 128-chapter fixture) and lands on each book's first open and
   after any cache rebuild. Needs tests for duplicate hrefs (keep first-match
   semantics), unresolved entries and wrap-around.
4. Optional reading features, for the user to pick; each is small:
   - #3642: time left in chapter/book (8-sample pace tracker, ~48 bytes)
   - #3727: paragraph indentation override
   Skip #2350 (whole-book page estimates) and #3758: they need every chapter laid
   out or add UI surface for little gain.
5. Only if the user notices it: chapter-boundary latency. Idle prefetch
   decodes only the next page of the current section
   (`docs/fork-layout.md`); turning into a chapter with no section cache waits
   for the first page of its incremental layout. Pre-building the next section
   near a chapter's end would remove that wait but adds idle CPU, SD writes and
   heap fragmentation, the same trade-offs that keep #3705, #3675 and #3060
   deferred. It cannot be validated without the device, so do not start it
   unprompted.
6. Relink reading state across Calibre renames (from the old item 2). Done in
   r53/r55: sort keys, stored UUIDs, and state-preserving /Read moves. The
   relink itself (move the cache directory, bookmarks, reading state and
   recents from a vanished path to a new path with the same UUID, reusing
   `moveBookWithState()` in `src/util/BookStateMove.cpp`) is worth doing only
   if the user finds reading state lost after a re-export.
7. X4 Pro internal heap: PR #3488 (serialx) rebuilds TinyUSB with only MSC/CDC
   and recovers most of the 12,248 bytes lost with the Arduino upgrade in #3397.
   It is a build-system change under maintainer test; wait for upstream to merge
   it, then rebase onto it rather than carrying it.
8. Hotspot patch splitting (files `LibraryListActivity.cpp`,
   `EpubReaderActivity.cpp`, `Section.cpp`, `BookMetadataCache.cpp`,
   `ChapterHtmlSlimParser.cpp`): only worth it if a rebase conflicts there
   repeatedly. Item 1 will show whether it does.

Retired on 2026-09-29: the old "device validation of r55" item, and its use as
the trigger for the relink. Not planned without a device: further anti-ghosting
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
  - adapted in r52: #3349
  - adopted: #3441, #3495, #3733, #3027, #3605, #3419, #3685, #2438, #3113,
    #2603, #3305 (r55)
  - #3764: link-return progress (`docs/fork-reader.md`, "Links, footnotes and
    history")
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
