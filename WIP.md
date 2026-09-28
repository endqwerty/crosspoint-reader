# WIP handoff — clean develop, X4 Pro r51

## Repository state

The reader and SDK personal forks are consolidated onto `develop`. The reader's
local changes form a linear series above official reader `develop`
`93e98bb78702e29868a16a13b80c40e6b36ccdff`: retained X4 Pro improvements, r51 cold
indexing, then fork instructions/setup. The legacy local merge was removed;
upstream's own history is intact. All intended firmware source is preserved.

SDK `develop` remains `703f269a1ea5bf738820db91e6a6ca6b22b68adc`, one local patch
above `111fdcc7f0176c3ee38391a160ee296bf492dbd8`, the dependency revision pinned
by the official reader. Newer SDK main commits are outside this history-only
cleanup. `.gitmodules` resolves the SDK through the personal fork.

The reader fork is `endqwerty/crosspoint-reader`; the SDK fork is
`endqwerty/freeink-sdk`. The maintained checkout uses `origin` for the
personal fork and `upstream` for official upstream, and tracks `origin/develop`
in both repos.
No PR was opened. Completed work is automatically committed, integrated into
local `develop` and pushed to personal `origin/develop` under the standing
authorization in `docs/FORK.md`. Other publication requires explicit approval.

Persistent policy: read `docs/FORK.md`. Keep every local patch above the upstream
base, adapt or drop patches when upstream supersedes them, and use rebase plus
fast-forward/squash integration. No local merge commits.

## Recovery and verification

The user reports the recovery copies deleted; do not rely on the shared copies.
Local `reader.bundle` and `sdk.bundle` still exist under
`/Users/danielyang/.local/share/crosspoint-build/branch-cleanup-20260927/`, along
with `RESTORE.md`. Both local bundles passed `git bundle verify` again on
2026-09-27 and report complete history. They were not deleted by this session.
Legacy backup branch refs were removed after verification. Active disposable
worktrees may have temporary feature branches; no work should remain only there.

The cleanup changes history, repository setup and instructions only. It does not
change firmware source or the SDK pin. Source equivalence is checked against the
pre-cleanup tree and the exact r51 release manifest; no redundant rebuild is
needed. The release archive preserves the original tested source, including its
older instructions. Use this handoff for current branch state.

## Current flash image

Use `/Volumes/workspace/builds/crosspoint-reader/x4pro-r51-rebuild-20260927-222421/firmware-x4pro-r51-211f3827.bin`.
The authoritative pointer is `/Volumes/workspace/builds/crosspoint-reader/FLASH-LATEST.md`.
Web flasher → Xteink X4 Pro → Custom .bin. Start with AA off.
Version: `1.6.5-dev-x4pro-r51-93e98bb`.
SHA-256: `2da4397367da79dbf3bc64681e0bc141e0bf8083cf2ad37e5de9aed97972ceef`.

Fresh local develop `211f3827` rebuild: firmware inputs match r51; build with an
empty cache and ESP32-S3 image inspection passed. Copied artifacts were verified
on SMB. Existing native/sanitizer tests were not rerun; no firmware source changed.
Persistent delivery policy is in the permanent checkout's `docs/FORK.md` and the
share's `BUILD-WORKFLOW.md`. Worktrees are isolated and disposable. The `builds`
folder is canonical; the earlier `firmware` folder is a redundant copy.

r51 uses a transient 512-byte nothrow buffer for the large-book spine-index scan,
with checked unbuffered OOM fallback. No cache-format or foreground-reading change.
512-chapter cold HAL reads fell 4,139 → 2,117; 2,048 chapters fell 16,512 → 8,426.
Bytes, seeks, writes and warm-open counts are unchanged. These are host fixture
counts, not measured device speedups. See `docs/cold-index-io-r51.md`.

For the original r51 validation, all 1,599 native Release and LLVM22 ASan/UBSan
tests passed, retaining all 1,596
prior test names. All 16 validation gates passed, including X4 Pro compilation,
SDK runners, scoped static analysis, image inspection and dependency checks.
Firmware compiler log is warning-free; cppcheck has four low style findings and
no medium/high findings. Static RAM: 102,320 bytes; linked flash: 5,675,354 bytes.
The original r51 package contains 6,441 tested source files and 100 checksummed
artifacts; the fresh rebuild handoff has six checksummed artifacts.
Build mirror, scripts and evidence:
`/Users/danielyang/.local/share/crosspoint-build/epub-r51/`.

## Remaining work and limits

The 128-chapter cold-open fixture still performs 33,556 HAL reads in its linear
TOC lookup. Compare memory and correctness before changing that policy. Broader
cold-open profiling should include real ZIP/container, CSS and first-page layout;
the current fixture uses archive/storage doubles. No new feature work is active.

The upstream reviews recorded on 2026-09-27 found no newer reader develop
commits; the full open-PR triage is under "Proposed next steps". Recheck
upstream/PR state when starting new work. Earlier, eight pending PRs were
reviewed without import; #3705 and #3675 remain deferred for
cold-layout/input-responsiveness concerns. Evidence is in
`/Volumes/workspace/builds/crosspoint-reader/upstream-review-r50/REVIEW.md` and the r51 package.

Device timing, peak heap, ghosting, BUSY recovery and power-loss behavior remain
unmeasured. Check an uncached long EPUB, TOC jumps, reopen and sleep/wake with AA
off. No cache deletion or recording required.

## Completed build and administration handoff

The fresh r51 rebuild is exported to the canonical SMB `builds` folder above.
All six exported artifact checksums passed; build and image inspection passed.
Future work uses isolated disposable worktrees. The user's standing instruction
now authorizes automatic commits, local rebases and integration into `develop`;
see `docs/FORK.md`. The user also authorizes automatic pushes of completed
`develop` to the verified personal reader fork, `origin`; upstream pushes,
other branches, PR actions and release publication still require approval.
Official upstream was fetched on 2026-09-27 and remains `93e98bb`; local develop
contains that unchanged base with only linear fork patches above it. This handoff
changes documentation only; no firmware rebuild is needed after integration.

No feature implementation is active. Start the next isolated worktree from
personal `develop` and follow `docs/FORK.md`'s startup procedure. The current
worktree has no unique source or required build artifacts once integrated and
pushed; its deletion does not remove the shared firmware handoff. The roadmap
below is optional future scope, not unfinished work blocking deletion.

## Proposed next steps (not started)

Replanned 2026-09-27 after re-reading `ROADMAP.md` and triaging all 229 open
upstream PRs (official `develop` unchanged at `93e98bb`). Priorities follow the
offline-EPUB, X4 Pro and Calibre-library focus in `docs/FORK.md`. Each item needs
the user's go-ahead. Upstream's roadmap (Phase 1: footprint and heap
fragmentation; Phase 2: SD-loaded hyphenation/themes) aligns with items 3, 5
and 7. Its Phase 2 hyphenation downloader is Wi-Fi-first; import it only after
upstream merges it.

1. Device validation of r51 with the real library (unchanged). Nothing has been
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
3. Stop silent text loss in Calibre conversions: PR #3349 (s0lness).
   `ChapterHtmlSlimParser.cpp:1315-1323` skips the whole subtree of any
   `doc-pagebreak`/`epub:type="pagebreak"` element. Calibre conversions wrap
   real paragraph text inside those markers, so it disappears. The PR is
   conflicting and unreviewed; adapt it to the fork's parser, bump the
   section cache version and add a fixture. Highest correctness value for
   this library.
4. Page-turn input around refreshes: PR #3636 (Daviex), which the author
   verified on an X4 Pro. It keeps one pending turn across the async
   `requestUpdate()` gap and chapter loads using render generations. The fork
   already has its own queued-turn handling (`docs/page-turning.md`,
   turn-r4). Compare the two, and import only the cases the fork misses
   (a turn dropped while `section` is absent, or a `RenderLock::peek()` race),
   with host tests.
5. Scan the real library for parser failures before choosing the fixes. Add a
   host-side script that walks the SD export and counts the patterns these
   open PRs fix, so only reproduced ones get imported:
   - pagebreak markers carrying text (#3349)
   - unclosed void elements like `<br>`, which fail the whole chapter
     (#3375, sfoulad). Calibre "Save to disk" keeps publisher XHTML, so
     this is possible.
   - TOC hrefs that only match by filename (#2987)
   - nested hidden landmarks nav (#2297)
   - an SVG wrapper named as `cover-image` (#3539)
   - ZIP comments larger than 1 KB (#2614; `lib/ZipFile/ZipFile.cpp:211`
     scans only the last 1 KB)
   - extension-less images (#2386)
   Report counts per pattern; do not modify the library.
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
   fork is one 42,767-line commit (`d1c8e0fb`, 487 files) plus r51. Split it
   into topical patches, fold the 51 `-rNN` revision docs into a few feature
   docs, and drop code upstream supersedes.
9. Cold-open TOC lookup (unchanged): the 128-chapter fixture still does 33,556
   HAL reads in its linear TOC lookup. Compare memory and correctness first.
10. Optional reading features, for the user to pick from; none are required:
    - #3642: time left in chapter/book (8-sample pace tracker, ~48 bytes)
    - #3727: paragraph indentation override
    - #2350: whole-book page estimates
    - #3754: press-based list navigation; approved, wait for merge
    - #3758: estimate marker placement

Upstream PR triage, 2026-09-27 (open, not merged):

- Already in the fork, or superseded by fork code:
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
