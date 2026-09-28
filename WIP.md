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
Recovery copies are also stored under ignored `build/branch-cleanup-20260927/`.
Backup branch refs are removed after verification so only `develop` remains.

The cleanup changes history, repository setup and instructions only. It does not
change firmware source or the SDK pin. Source equivalence is checked against the
pre-cleanup tree and the exact r51 release manifest; no redundant rebuild is
needed. The release archive preserves the original tested source, including its
older instructions. Use this handoff for current branch state.

## Current flash image

Use `build/x4pro-epub-r51/firmware-x4pro-epub-r51-final.bin`.
The authoritative pointer is `build/FLASH-LATEST.md`.
Web flasher → Xteink X4 Pro → Custom .bin. Start with AA off.
Version: `1.6.5-dev-x4pro-r51-93e98bb`.
SHA-256: `a53470a2e253fcf5804f0f3c91ebc3b6dac9e017abb481dae0a8cb92f3cd0040`.

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
`build/upstream-review-r50/REVIEW.md` and the r51 package.

Device timing, peak heap, ghosting, BUSY recovery and power-loss behavior remain
unmeasured. Check an uncached long EPUB, TOC jumps, reopen and sleep/wake with AA
off. No cache deletion or recording required.

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

Workspace note: `/Volumes/workspace` is an SMB share whose server does not
advertise named-stream support (`smbutil statshares -m /Volumes/workspace`), so
macOS stores file metadata as `._*` AppleDouble files. `._*` is ignored through
`.git/info/exclude` and each submodule git dir's `info/exclude`. That rule cannot
override the `!Ubuntu/**`-style re-includes in
`lib/EpdFont/builtinFonts/source/.gitignore`, so `._*` files there reappear after
checkouts. Delete them with
`find lib/EpdFont/builtinFonts/source -name '._*' -delete`. The lasting fix is on
the server: Samba `vfs objects = catia fruit streams_xattr`. The macOS SMB client
also rejects `F_FULLFSYNC` (`ENOTSUP`) while plain `fsync` works. T3 Code's
checkpoint capture runs `git add` with `-c core.fsyncMethod=fsync
-c core.fsync=objects,reference`, which git implements as `F_FULLFSYNC` on
macOS, so it fails with exit 128 ("fsync error on .../objects/xx/tmp_obj_*")
whenever a turn has new file content. The command-line `-c` overrides repository
config, so the git directory now lives on local disk at
`~/.local/share/git-dirs/crosspoint-reader.git`. The checkout's `.git`, both
worktrees and the SDK/lucide submodules point there with absolute paths; the
submodules' `core.worktree` values are absolute paths on the share. `git worktree
list` shows that directory as the main worktree, which is cosmetic. The pre-move
copy is `/Volumes/workspace/projects/crosspoint-reader.git.smb-backup-20260927`
and can be deleted once everything checks out. `core.untrackedCache` is enabled. New T3 worktrees take
~1.5 min for `git worktree add` and ~2 min for a recursive submodule update over
SMB; T3 cut the latter short and left a half-written SDK. Submodule git dirs
created on local disk also get `core.filemode=true`, which marks every SMB file
as a mode change. Use T3 project settings: worktree submodules `none` plus a
blocking run-on-worktree-create script
`git submodule update --init && git submodule foreach -q 'git config core.fileMode false'`.
`lucide` holds only icon-generator source SVGs and is not needed to build.
