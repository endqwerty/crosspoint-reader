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
`endqwerty/freeink-sdk`. The maintained checkout uses `origin` for official
upstream, `fork` for the personal fork, and tracks `fork/develop` in both repos.
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
