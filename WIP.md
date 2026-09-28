# WIP handoff — develop on upstream e6af0c9, X4 Pro r52

## Repository state

The reader's local changes form a linear series above official reader `develop`
`e6af0c95110a66a0b7a087df2d95e7598dc79594` (four upstream commits newer than r51's
`93e98bb`): retained X4 Pro improvements, r51 cold indexing, fork
instructions/setup, then r52's pagebreak fix and library scan script. Upstream's
own history is intact; there are no local merge commits.

SDK `develop` is `d7438bb53e5a56ba698c40cdfb55cd47602977f1`, the single local patch
rebased above `225c097cfb6d5ecd4ca556041746123faeb4bc79`, the revision the new
official reader pins. `.gitmodules` resolves the SDK through the personal fork.

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

## Recovery and verification

Pre-rebase refs are kept locally: reader `backup/reader-develop-3048089c` and, in
the permanent checkout's SDK module, `backup/sdk-develop-703f269`. The older
bundles under `/Users/danielyang/.local/share/crosspoint-build/branch-cleanup-20260927/`
(with `RESTORE.md`) still cover the pre-cleanup history. The rebased series was
revalidated from source; the r51 image and package remain on the share.

## Current flash image

Use `/Volumes/workspace/builds/crosspoint-reader/x4pro-r52-20260928-053827/firmware-x4pro-r52-1399c0ce.bin`
(Windows: `\\<server>\workspace\builds\crosspoint-reader\x4pro-r52-20260928-053827\firmware-x4pro-r52-1399c0ce.bin`).
The authoritative pointer is `/Volumes/workspace/builds/crosspoint-reader/FLASH-LATEST.md`.
Web flasher → Xteink X4 Pro → Custom .bin. Start with AA off.
Version: `1.6.5-dev-x4pro-r52-e6af0c9`.
SHA-256: `5c0caf78d82f7ebc92a01a6d7b62c2cf3f93494fdedc4cb1def7cfc64062331e`.

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

Official upstream was fetched on 2026-09-28 at `e6af0c9`. Recheck upstream and
open PRs when starting new work. #3705 and #3675 remain deferred for
cold-layout/input-responsiveness concerns; earlier evidence is in
`/Volumes/workspace/builds/crosspoint-reader/upstream-review-r50/REVIEW.md`.

Device timing, peak heap, ghosting, BUSY recovery and power-loss behavior remain
unmeasured. On r52 also check Library search (upstream keyboard) and list
navigation (upstream press navigation), and that a book's first open after
flashing re-lays out chapters without errors. Check an uncached long EPUB, TOC
jumps, reopen and sleep/wake with AA off.

No feature implementation is active. Start the next isolated worktree from
personal `develop` and follow `docs/FORK.md`'s startup procedure. The roadmap
below is optional future scope, not unfinished work blocking deletion.

## Proposed next steps

Replanned 2026-09-27 after re-reading `ROADMAP.md` and triaging all 229 open
upstream PRs; updated 2026-09-28 after r52 (items 3 and 5 done). Priorities follow the
offline-EPUB, X4 Pro and Calibre-library focus in `docs/FORK.md`. Each item needs
the user's go-ahead. Upstream's roadmap (Phase 1: footprint and heap
fragmentation; Phase 2: SD-loaded hyphenation/themes) aligns with items 3, 5
and 7. Its Phase 2 hyphenation downloader is Wi-Fi-first; import it only after
upstream merges it.

1. Device validation of r52 with the real library (unchanged). Nothing has been
   measured on hardware. Measure first-entry Library reconcile time, free heap
   and largest free block (serial) with the full Calibre export on the card.
   Run `scripts/sync-calibre-library.sh -n` after a real re-export to learn
   how often renames happen and whether unchanged books are byte-identical
   (see item 2 and old item 4 below).
2. Calibre metadata in one Library format bump. `ClixRecord` is exactly full
   at 128 bytes (`lib/LibraryIndex/LibraryFormat.h:160`), so group all
   Calibre-supplied fields into a single CLIX v5 migration instead of two:
   - Calibre UUID (`dc:identifier opf:scheme="uuid"`), so reconcile can
     relink progress, bookmarks, favorites and reading state when a
     vanished path and a new path share a UUID. State is path-keyed today
     (`lib/Epub/Epub.h:46`, `src/activities/library/LibraryBookState.cpp`,
     `src/util/BookmarkUtil.cpp`). Reuse the migration-helper shape of open
     PRs #3354/#3166, which only cover moves made on the device.
   - Author sort (`opf:file-as`) and title sort (`calibre:title_sort`), which
     Calibre always writes. The fork only guesses surnames heuristically;
     `ContentOpfParser.cpp` reads `calibre:series` (line 289) but not these.
     Adapt the parsing from PR #3757 (digitaltembo, with series/tags) and
     #3651 (file-as grouping). The earlier review deferred a direct import
     because CLIX v4 diverges; take the parser and fallback rules, not
     their format. Keep the existing heuristic as the fallback for non-Calibre
     files.
   The UUID relink can be dropped if item 1 shows renames are rare; the
   sort keys are worth doing either way.
3. Done in r52: pagebreak markers keep wrapped text (adapted #3349). The
   library scan found no pagebreak attributes, so this protects future books
   only. Optional follow-up: #3349's deferred `<br>` handling, which joins
   text a converter split with `<br>` + marker into one line. Drop the local
   patch if upstream merges its own version.
4. Page-turn input around refreshes: PR #3636 (Daviex), which the author
   verified on an X4 Pro. It keeps one pending turn across the async
   `requestUpdate()` gap and chapter loads using render generations. The fork
   already has its own queued-turn handling (`docs/page-turning.md`,
   turn-r4). Compare the two, and import only the cases the fork misses
   (a turn dropped while `section` is absent, or a `RenderLock::peek()` race),
   with host tests.
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
8. Reduce rebase burden (unchanged, now more urgent before items 2-4). The
   fork is one 42,767-line commit ("feat: integrate X4 Pro reading
   improvements", 487 files) plus r51 and r52. The 2026-09-28 rebase
   conflicted only in the keyboard. Split it
   into topical patches, fold the 51 `-rNN` revision docs into a few feature
   docs, and drop code upstream supersedes.
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
  - #3764: link-return progress (`docs/reading-navigation.md`)
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
