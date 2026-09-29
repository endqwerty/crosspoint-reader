# WIP handoff — develop on upstream ce9f5c2, X4 Pro r54, split patch series

## Repository state

The reader's local changes form a linear series above official reader `develop`
`ce9f5c28` (see `git log upstream/develop..develop`).
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
scan), r53 (queued page-turn fix, Calibre sort keys) and a docs commit that
folds the 50 `-rNN` revision notes into six `docs/fork-*.md` feature docs.
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

No backup branches or extra worktrees are kept; the only branches are reader and
SDK `develop` plus the active worktree's branch. History removed on 2026-09-28
(earlier develop tips, the pre-split series, old review/test branches and the
SDK's single-patch versions) is in verified bundles under
`/Users/danielyang/.local/share/crosspoint-build/branch-cleanup-20260928/`, with
`RESTORE.md`. The 2026-09-27 bundles in `branch-cleanup-20260927/` still cover
the pre-cleanup history.

## Current flash image

Use `/Volumes/workspace/builds/crosspoint-reader/x4pro-r54-20260928-221431/firmware-x4pro-r54-5fa5562f.bin`
(Windows: `\\<server>\workspace\builds\crosspoint-reader\x4pro-r54-20260928-221431\firmware-x4pro-r54-5fa5562f.bin`).
The authoritative pointer is `/Volumes/workspace/builds/crosspoint-reader/FLASH-LATEST.md`.
Web flasher → Xteink X4 Pro → Custom .bin. Start with AA off.
Version: `1.6.5-dev-x4pro-r54-ce9f5c2`.
SHA-256: `fad60589f95d337077dfc8ee9a4a6465cb3971861747e864efbe153ed5a8542d`.
Earlier images stay in their dated folders; r53 is
`x4pro-r53-20260928-065705/firmware-x4pro-r53-f7f0fb5d.bin`.

r54 is r53's firmware on the newer upstream base `ce9f5c2` and SDK `87c4493`
(header back-button tap routing, keyboard alignment, list separators and
checkboxes, SDK atomic SD writes). No fork behavior changed; the fork's commits
are the split series above. Firmware source is commit `5fa5562f`; the handoff
commit changes only this file.

r54 validation (2026-09-28): all 1,629 native Release and 1,629 LLVM 22
ASan/UBSan tests pass; SDK UI (243,110 checks), Pro display, UC8279, font and
input host runners pass. The X4 Pro release build is warning-free: static RAM
102,352 bytes (+32 from upstream input routing), linked flash 5,686,766 bytes;
image 5,691,776 bytes, ESP32-S3 image inspection valid.

r53 changes from r52 (items 4 and 2):

- Queued page turns: a turn pressed during a page update is queued and applied
  when the update ends. Opening the toolbar from the home button, or a pushed
  screen (dictionary, footnote selection, sync), left it queued, so the page
  flipped with no input after returning. `openOverlay()` and `onSuspend()` now
  clear it. Compared with upstream #3636, this is the one case the fork's own
  queued-turn handling missed; the rest of #3636 duplicates fork code and is
  not imported.
- Calibre sort keys (Library index format 4 → 5, record still 128 bytes). The
  parser reads the title sort (title file-as, else `calibre:title_sort`), the
  first creator's file-as when that creator is an author, and the book UUID
  (uuid-scheme/`uuid_id` identifier, else `uuid:`; the "calibre" scheme changes
  between conversions). Title order, group letters and search use the title
  sort, so 199 "The …"/"A …" books move to their Calibre place; search also
  matches the shown title. Author order uses the author sort (surname guess as
  fallback) and headings show it unless written in capitals. Renamed books keep
  their Added position by UUID even when Calibre rewrote them. The first open of
  the Library after flashing re-reads all 750 package documents once; arrival
  order is kept. Parsing rules follow upstream #3757 (Nolan Hawkins).
  Across the real 750-book export the parser finds a title sort in 750, an
  author sort in 749 (the EPUB 3 book lists its illustrator first) and the
  expected UUID in 747 of 747.

r53 validation (2026-09-28): all 1,629 native Release and 1,629 LLVM 22
ASan/UBSan tests pass, retaining every r51 test name. The X4 Pro release build is
warning-free: static RAM 102,320 bytes (unchanged), linked flash 5,685,162 bytes;
image 5,690,176 bytes, ESP32-S3 image inspection valid. Firmware source is
commit `f7f0fb5d`. An independent
review of the sort-key change found four issues, all fixed with tests: the rename
UUID array outlived its phase (up to ~54 KB during the sorts), search missed
shown titles that differ from a curated title sort, size matching could claim a
UUID-renamed book's entry first, and an author sort from a later creator split
author groups. Relinking progress and bookmarks across renames is not included.

r52 changes from r51: the upstream rebase above, and pagebreak markers no longer
drop book text. Elements tagged `role="doc-pagebreak"`/`epub:type="pagebreak"`
used to be skipped with their subtree. Tagged paragraphs, headings, list items
and blockquotes now render; other markers capture up to 32 bytes in the parser
object (no heap) and drop only their own label or, without a label, a page
number; anything else replays at its original reading offsets. Adapted from
upstream PR #3349 (Sylve) without its deferred-`<br>` spacing rework. Section
cache version 49 → 50, so every book re-lays out its chapters once on first
open; progress, bookmarks and Library data are kept.

Validation (2026-09-28): all 1,614 native Release tests and all 1,614 LLVM 22
ASan/UBSan tests pass. They retain every r51 test name plus 4 new upstream tests
and 11 new pagebreak/version tests. SDK UI (242,854 checks), Pro display,
UC8279, UC8253, font, ligature, GPOS and input host runners pass. The X4 Pro
release build is warning-free: static RAM 102,320 bytes (unchanged), linked flash
5,679,002 bytes; image 5,684,016 bytes, ESP32-S3
image inspection valid. Firmware source is commit `1399c0ce`; the later handoff
commit changes only this file. An independent review of the parser change found a
block-style underflow, glued words at a swallowed `<br>`, and words such as "I"
dropped as page numbers; all three are fixed with tests. Build logs and scan
evidence: `/Users/danielyang/.local/share/crosspoint-build/wip-plan/`.

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

The 128-chapter cold-open fixture still performs 33,556 HAL reads in its linear
TOC lookup. Compare memory and correctness before changing that policy. Broader
cold-open profiling should include real ZIP/container, CSS and first-page layout;
the current fixture uses archive/storage doubles.

Official upstream was fetched on 2026-09-28 at `ce9f5c2`. Recheck upstream and
open PRs when starting new work. #3705 and #3675 remain deferred for
cold-layout/input-responsiveness concerns; earlier evidence is in
`/Volumes/workspace/builds/crosspoint-reader/upstream-review-r50/REVIEW.md`.

Device timing, peak heap, ghosting, BUSY recovery and power-loss behavior remain
unmeasured. On r54 check the Library's first open after flashing (it re-reads
every package document once), title/author order and headings, and that a turn
pressed during a page update no longer fires after opening the toolbar with the
home button. Also check Library search (upstream keyboard) and list
navigation (upstream press navigation), and that a book's first open after
flashing re-lays out chapters without errors. Check an uncached long EPUB, TOC
jumps, reopen and sleep/wake with AA off.

No feature implementation is active. Start the next isolated worktree from
personal `develop` and follow `docs/FORK.md`'s startup procedure. The roadmap
below is optional future scope, not unfinished work blocking deletion.

## Proposed next steps

Replanned 2026-09-27 after re-reading `ROADMAP.md` and triaging all 229 open
upstream PRs; updated 2026-09-28 after r52 (items 3 and 5 done) and r53 (items 2
and 4 done). Priorities follow the
offline-EPUB, X4 Pro and Calibre-library focus in `docs/FORK.md`. Each item needs
the user's go-ahead. Upstream's roadmap (Phase 1: footprint and heap
fragmentation; Phase 2: SD-loaded hyphenation/themes) aligns with items 3, 5
and 7. Its Phase 2 hyphenation downloader is Wi-Fi-first; import it only after
upstream merges it.

1. Device validation of r54 with the real library (unchanged). Nothing has been
   measured on hardware. Measure first-entry Library reconcile time, free heap
   and largest free block (serial) with the full Calibre export on the card.
   Run `scripts/sync-calibre-library.sh -n` after a real re-export to learn
   how often renames happen and whether unchanged books are byte-identical
   (see item 2 and old item 4 below).
2. Done in r53: Calibre title/author sort keys and stored UUIDs (index
   format 5). Optional follow-up, only if item 1 shows Calibre renames are
   common: relink path-keyed reading state (reader cache dir, bookmarks,
   favorites/reading state, recents) from a vanished path to a new path with
   the same stored UUID. `FileBrowserActivity`'s RenameState already moves all
   of these with rollback; PR #3354's `moveBookData()` is the right shape. The
   same helper would also fix the "Move finished books to /Read" move, which
   today leaves Library state and bookmarks at the old path.
3. Done in r52: pagebreak markers keep wrapped text (adapted #3349). The
   library scan found no pagebreak attributes, so this protects future books
   only. Optional follow-up: #3349's deferred `<br>` handling, which joins
   text a converter split with `<br>` + marker into one line. Drop the local
   patch if upstream merges its own version.
4. Done in r53: the one #3636 case the fork missed (see above). Optional:
   #3636 also skips idle prefetch and background build on ticks with input;
   that only affects latency, so consider it only if page turns feel slow on
   the device.
5. Done in r52: `scripts/scan-epub-library.py` (results above). None of
   #3375, #2987, #2297, #3539, #2614 or #2386 reproduces in the library; do
   not import them for this library. Optional: make the stale NCX "Cover"
   entry (117 books) fall back to the first spine item or hide it.
6. Background build at idle CPU speed: PR #3060. The background tick at
   `EpubReaderActivity.cpp:366-368` runs without `HalPowerManager::Lock`,
   so it can run at `LOW_POWER_FREQ` once power saving engages. That can leave pages unbuilt when the
   reader turns to them. A maintainer questioned the battery cost. Import it
   only with a device measurement of page-turn latency into unbuilt pages
   and idle drain.
7. X4 Pro internal heap: PR #3488 (serialx) rebuilds TinyUSB with only
   MSC/CDC. That recovers most of the 12,248 bytes of S3 internal heap
   lost with the Arduino upgrade in #3397. It
   is a build-system change under maintainer test; wait for upstream to
   merge it, then rebase onto it rather than carrying it.
8. Done 2026-09-28: the X4 Pro commit is split into nine building topical
   patches, the SDK patch into two, the revision notes are folded into
   `docs/fork-*.md`, and superseded code (#3698 ButtonNavigator, #3506 keyboard
   via upstream #3755) is gone. Remaining hotspots are fork changes to large
   upstream files: `LibraryListActivity.cpp`, `EpubReaderActivity.cpp`,
   `Section.cpp`, `BookMetadataCache.cpp`, `ChapterHtmlSlimParser.cpp`. Splitting
   those by hunk into smaller patches is possible but only worth it if a future
   rebase conflicts there repeatedly.
9. Cold-open TOC lookup (unchanged): the 128-chapter fixture still does 33,556
   HAL reads in its linear TOC lookup. Compare memory and correctness first.
10. Optional reading features, for the user to pick from; none are required:
    - #3642: time left in chapter/book (8-sample pace tracker, ~48 bytes)
    - #3727: paragraph indentation override
    - #2350: whole-book page estimates
    - #3758: estimate marker placement

Upstream PR triage, 2026-09-27 (open, not merged):

- Already in the fork, or superseded by fork code:
  - merged upstream and in the base since 2026-09-28: #3754, #3755, #3765, #3766
  - adapted in r52: #3349
  - adopted: #3441, #3495, #3733, #3027, #3605, #3419, #3685, #2438, #3113,
    #2603
  - #3764: link-return progress (`docs/fork-reader.md`)
  - #3698: `ButtonNavigator` uses `std::initializer_list`
  - #2602: flat CSS rule pools in `CssParser.h:150-168`
  - #2343: ordered lists (`ChapterHtmlSlimParser.cpp:1472`)
  - #3452: checked `readStringChecked`. The only unchecked callers left
    read the parser's own temp store (`ContentOpfParser.cpp:427-442`).
- Needs checking against the fork: #3305 (section/CSS cache mismatch window).
  `Epub.cpp:505-510` also wipes sections conditionally.
- Still deferred: #3705 and #3675 (drafts with open regressions).
- Watch only, import after merge: #3706 (hyphenation manager), #3704
  (TXT/Markdown via the EPUB pipeline) and #3757 as a whole.
- Not adopted: large feature PRs outside this focus, such as
  highlights/clippings (#3589, #2617, #1742, #1478), drop caps (#2387), GIF
  (#2299) and table borders (#2954).
- Skipped by `docs/FORK.md` policy: OPDS, KOReader sync, WebDAV/web server,
  plugins, BLE, other boards, keyboards and translations.

Old item 4 (re-export cost) remains open inside item 1. Reconcile reuses
metadata only when size and mtime match (`LibraryBuilder.cpp:403`), and the
sync script copies by content without source mtimes.

Workspace note: the checkout and its git data live on local disk at
`~/workspace/crosspoint-reader`, and `~/.t3/worktrees` is a local folder. The
earlier SMB checkout under `/Volumes/workspace/projects/crosspoint-reader` is
retired: the macOS SMB client rejects `F_FULLFSYNC`, which T3 Code's checkpoint
`git add` forces, and worktree creation and submodule checkout took minutes there.
Exported builds moved to `/Volumes/workspace/builds/crosspoint-reader/`.
`core.untrackedCache` is enabled. `lucide` holds only icon-generator source SVGs
and is not needed to build.
