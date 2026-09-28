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
The user authorized publishing this cleanup and removing obsolete fork/local
branches. No PR was opened. Further publication requires the user's instruction.

Persistent policy: read `docs/FORK.md`. Keep every local patch above the upstream
base, adapt or drop patches when upstream supersedes them, and use rebase plus
fast-forward/squash integration. No local merge commits.

## Recovery and verification

Verified self-contained bundles preserve every pre-cleanup local and fetched
remote branch and tag. Recovery instructions, before/after refs and verification
are under `/Users/danielyang/.local/share/crosspoint-build/branch-cleanup-20260927/`.
Recovery copies are also stored under `/Volumes/workspace/builds/crosspoint-reader/branch-cleanup-20260927/`.
Backup branch refs are removed after verification so only `develop` remains.

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

All 1,599 native Release and LLVM22 ASan/UBSan tests passed, retaining all 1,596
prior test names. All 16 validation gates passed, including X4 Pro compilation,
SDK runners, scoped static analysis, image inspection and dependency checks.
Firmware compiler log is warning-free; cppcheck has four low style findings and
no medium/high findings. Static RAM: 102,320 bytes; linked flash: 5,675,354 bytes.
The package contains 6,441 tested source files and 100 checksummed artifacts.
Build mirror, scripts and evidence:
`/Users/danielyang/.local/share/crosspoint-build/epub-r51/`.

## Remaining work and limits

The 128-chapter cold-open fixture still performs 33,556 HAL reads in its linear
TOC lookup. Compare memory and correctness before changing that policy. Broader
cold-open profiling should include real ZIP/container, CSS and first-page layout;
the current fixture uses archive/storage doubles. No new feature work is active.

The latest targeted upstream review found no newer reader develop commits. Eight
pending PRs were reviewed without import; #3705 and #3675 remain deferred for
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
see `docs/FORK.md`. Remote publication still requires explicit approval.
Official upstream was fetched on 2026-09-27 and remains `93e98bb`; local develop
contains that unchanged base with only linear fork patches above it. This handoff
changes documentation only; no firmware rebuild is needed after integration.

## Proposed next steps (not started)

Review of 2026-09-27, in priority order for the Calibre-library workflow in
`docs/FORK.md`. Each item needs the user's go-ahead.

1. Device validation of r51 with the real library. Nothing has been measured on
   hardware yet. Measure first-entry Library reconcile time, free heap and
   largest free block (serial) with the full Calibre export on the card.
2. Library size: resolved. The library is about 400-500 books, far below the
   4,096-book index cap (`lib/LibraryIndex/LibraryFormat.h:57`). No work needed.
3. State survives re-export. Progress, bookmarks, favorites and reading state
   are all keyed by path (`lib/Epub/Epub.h:46`,
   `src/activities/library/LibraryBookState.cpp`, `src/util/BookmarkUtil.cpp`).
   A Calibre rename (author or title edit) therefore orphans them. Options:
   (a) a stable Calibre save template and incremental copying, which is
   workflow only; (b) during reconcile, relink state when a vanished path and a
   new path share the Calibre UUID in the OPF.
   Upstream check (2026-09-27): open PRs #3354 (`moveBookData`) and #3166
   (`moveBook`, `lib/BookCachePath`) migrate cache, bookmarks and recents only
   for moves made on the device (web files page, WebDAV, move-to-/Read).
   Neither handles files renamed on a computer; #3354's author declined that as
   out of scope. (b) has no upstream equivalent. It would need a UUID in the
   index, but `ClixRecord` is exactly full at 128 bytes
   (`lib/LibraryIndex/LibraryFormat.h:160`), so it means a format bump and a
   relink across five path-keyed stores. If chosen, reuse the #3354/#3166
   migration helper shape rather than a parallel one.
   Done: `scripts/sync-calibre-library.sh` now lists books whose path changes,
   matched by Calibre UUID, before copying (also with `-n`). Decide between (a)
   and (b) after seeing how often real re-exports report renames.
4. Re-export cost. Reconcile reuses metadata only when size and mtime match
   (`LibraryBuilder.cpp:403`). `scripts/sync-calibre-library.sh` compares by
   content and does not copy source mtimes, so books whose bytes are unchanged
   keep their SD mtime. Remaining: check whether Calibre re-exports are
   byte-identical for unchanged books (run the script with `-n` after a
   re-export; `>f` lines are content changes, and a full list means they are
   not), and measure
   first-entry reconcile time on the device.
5. Reduce rebase burden. The fork is one ~43k-line patch with 51 `-rNN`
   revision docs. Split it into topical patches and fold the revision logs into
   a few feature docs. Upstream PR #3366 (Library) is already merged into the
   base (`652ae0d8`); the fork's Library changes extend it (16 files, +2,342
   /-665 in `lib/LibraryIndex` and `src/activities/library`). Drop local code
   that upstream supersedes.

Workspace note: the checkout and its git data live on local disk at
`~/workspace/crosspoint-reader`, and `~/.t3/worktrees` is a local folder. The
earlier SMB checkout under `/Volumes/workspace/projects/crosspoint-reader` is
retired: the macOS SMB client rejects `F_FULLFSYNC`, which T3 Code's checkpoint
`git add` forces, and worktree creation and submodule checkout took minutes there.
Exported builds moved to `/Volumes/workspace/builds/crosspoint-reader/`.
`core.untrackedCache` is enabled. `lucide` holds only icon-generator source SVGs
and is not needed to build.
