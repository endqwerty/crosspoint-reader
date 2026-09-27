# WIP handoff — X4 Pro reader, r51

The requested upstream review and cold EPUB-open profiling are complete.
Current branch: `feature/epub-cold-open`, based on the preserved r50 local merge
`1b73e1c96c229ebd3ad3520c6b8994cdaba7bd19`. The user requested a local commit;
the reader branch has not been pushed and no PR was opened.

During the initial push request, personal reader/SDK forks were created and the
existing SDK commit `703f269a1ea5bf738820db91e6a6ca6b22b68adc` was pushed to
`endqwerty/freeink-sdk`, branch `codex/x4pro-r50-sdk`. The user then canceled
further pushes. Do not push the reader branch without renewed authorization.
`.gitmodules` retains the upstream URL; local SDK source is unchanged. Both repos
have `origin` for upstream and `fork` for the personal fork.

## Current flash image

Use `build/x4pro-epub-r51/firmware-x4pro-epub-r51-final.bin`.
The authoritative pointer is `build/FLASH-LATEST.md`.
Web flasher → Xteink X4 Pro → Custom .bin. Start with AA off.
SHA-256: `a53470a2e253fcf5804f0f3c91ebc3b6dac9e017abb481dae0a8cb92f3cd0040`.
Version: `1.6.5-dev-x4pro-r51-93e98bb`.

## Completed in this continuation

- Fetched upstream develop: still `93e98bb78702e29868a16a13b80c40e6b36ccdff`.
  Reviewed eight recent/relevant PRs; no upstream code imported. Decisions and
  pinned PR metadata/patches are in `build/upstream-review-r50/REVIEW.md` and the
  r51 package's `verification/upstream-review/`. #3705 and #3675 remain deferred.
- Added cold/warm index profiling through production Epub::load and parsers,
  with archive/storage doubles. It does not profile ZIP decompression, CSS or
  initial page layout.
- Batched the existing large-book spine-index scan through a transient 512-byte
  nothrow buffer, with checked direct-read fallback on OOM. See
  `docs/cold-index-io-r51.md`. No cache-format or foreground reading changes.
- Cold HAL reads: 512 chapters 4,139 → 2,117; 2,048 chapters 16,512 → 8,426.
  Bytes/seeks/writes/warm opens unchanged. Native fixture peak allocation bytes
  unchanged; one extra transient allocation. These are not device latency gains.
- All 1,599 native Release and LLVM22 ASan/UBSan tests passed, preserving all
  1,596 prior test names. All 16 validation gates passed, including target build,
  SDK runners, image/dependency verification and scoped static analysis.
  Firmware compiler log is warning-free; cppcheck has four low style findings,
  no medium/high findings. Static RAM 102,320 bytes; linked flash 5,675,354 bytes.
- Packaged 6,441 source files and 100 checksummed artifact files.
  Build mirror/scripts/evidence are under
  `/Users/danielyang/.local/share/crosspoint-build/epub-r51/`.

## Resume boundaries

The release source archive contains the exact tested source. **Only WIP.md changed
after packaging**, to record this handoff. Do not rebuild for that documentation
or the local commit. r50 remains available as the prior baseline. Historical r50 instructions
below apply only to its package and merge, not the current flash recommendation.

The remaining measured hotspot is the small-book linear TOC lookup (128 chapters:
33,556 HAL reads). A follow-up should compare memory/correctness before changing
that policy. Broader cold-open profiling should include real ZIP/container, CSS
and first-page fixtures. No additional feature work is in progress.

Device timing, peak heap, ghosting, BUSY recovery and power-loss behavior remain
unmeasured. Check an uncached long EPUB, TOC jumps, reopen and sleep/wake with AA
off. No cache deletion or recording required.

---

# Historical r50 baseline handoff

The user requested wrap-up and a local merge of `feature/x4pro-library-ux` into
`develop`. Do not restart open-ended roadmap work without a new request. Nothing
has been pushed and no PR has been opened. Read `AGENTS.md` before continuing.

## Current flash image

Use **`build/x4pro-epub-r50/firmware-x4pro-epub-r50-final.bin`**.
The authoritative pointer is [`build/FLASH-LATEST.md`](build/FLASH-LATEST.md).
Web flasher → **Xteink X4 Pro → Custom .bin**. Start testing with AA off.

- Version: `1.6.5-dev-x4pro-r50-93e98bb`.
- SHA-256: `e1d21aa1ffdff8b881f4e25bc0a83daecbb000252f458f2eaaf34dea56df8886`.
- Size: 5,680,480 bytes. Static RAM: 102,320 bytes; linked flash: 5,675,470 bytes.
- Package includes source archive, source fingerprints, test inventories, logs,
  dependency provenance, checksums, and flash/rebuild instructions.
- Builds are ignored local artifacts. Older `*-final.bin` files are historical;
  never infer the current recommendation from filenames alone.

## Integration boundary

Last fetched upstream `origin/develop` is
`93e98bb78702e29868a16a13b80c40e6b36ccdff`; FreeInk SDK base is
`111fdcc7f0176c3ee38391a160ee296bf492dbd8`.
The local SDK additions are saved on `codex/x4pro-r50-sdk`; the root commit pins
that local SDK commit. Both remotes named `origin` are the public upstreams, not
personal forks. Do not push to either without explicit authorization.

Local `develop` is updated to the upstream base and receives the tested feature
branch through a merge commit. `codex/develop-before-r50` preserves its previous
local position. Upstream commits remain intact ancestors. Local-only commit
bundles and integration verification are kept under `build/x4pro-r50-handoff/`.
The SDK bundle requires the base commit above, and the reader bundle requires the
upstream root base above. They preserve local work that upstream cannot serve.

Preserve upstream design and public interfaces; adapt or drop conflicting local
features instead of maintaining a divergent parallel implementation. Compare
future integrations against `origin/develop`, not the customized local branch.

## Retained work and product constraints

Target is ESP32-S3 Xteink X4 Pro (`x4pro-gh_release`), while shared code must respect
C3 memory limits, HAL storage locking, nothrow allocation, and upstream style.
Focus on offline EPUB reading: the user copies books to SD. No additional network
or media features were requested. Prefer synthetic operation counts and meaningful
unit/integration tests; do not ask the user to record page turns.

The six approved areas are implemented in the retained overlay: scalable Library
and search, Author/Series drill-in, Favorites/reading states, display/refresh
reliability, link/footnote navigation, and on-demand find-in-book. Search resources
must remain lazy and be released on exit; active reading must not pay a background
search cost. See `docs/epub-library-r5.md`, `docs/library-browsing-r10.md`,
`docs/find-in-book.md`, and `docs/reading-navigation.md` for feature boundaries.
Boot remains mostly black for night use. Keep ordinary reading and refresh behavior
consistent with upstream. Historical release notes describe their release's state;
later notes and actual code supersede them.

Recent performance changes retained:

| Release | Change | Host evidence / limit |
| --- | --- | --- |
| r41 | Batch ruby-length reads during page decoding | Fewer HAL reads, same bytes/cache format |
| r42–r44 | Reuse series matches, author sort keys and search scratch | Less repeated Library work and allocation |
| r45 | ASCII direction fast path | 4,000 classification requests avoided in fixture |
| r46 | Avoid redundant metadata buffer refills | Combined fixture reads 6,525 → 4,853 |
| r47 | Read prior Library records contiguously before path data | 4,096-book unchanged refresh seeks 20,480 → 16,385 |
| r48 | Borrow ruby annotation text while rendering | Annotated-page allocation requests 4,000 → 800 host bytes; identical pixels |
| r49 | Remove immediately discarded idle glyph preparation | Keep decoded next-page cache; foreground rendering unchanged |
| r50 | Share prefetch memory policy and order cheap checks first | Low-memory idle decode attempts 1 → 0; each heap query 201 → 102 over 100 ticks |

r50 uses the existing 80 KiB free / 32 KiB largest-block retention floors before
loading as well as after decoding. It adds no buffer or allocation. Low-memory
skips do not consume the page's one-attempt marker, so memory recovery permits
prefetch. Active parser exclusion, cache-release checks, lock/UI/debounce gates,
page budget and foreground loading are preserved.

## Validation and remaining uncertainty

r50 passed all **1,596 native Release tests** and the same complete registry under
**LLVM22 ASan/UBSan**, plus SDK Pro display, UI, three font runners, upstream
viewport checks, scoped static analysis, X4 Pro firmware compilation, image
inspection, and dependency-byte verification: **16 gates**. Firmware compiler log
is warning-free. Existing host constructor-order/theme warnings were retained.
The package verifies 6,439 source files and 99 packaged files. Independent clean
upstream evidence remains the r29 run at ef08c3a; an inspected Git comparison
shows only release workflow differences through 93e98bb, not a new upstream run.

The merge and this handoff change Git metadata/documentation only after validation.
The release's pre-commit source archive remains the exact tested source. Verify
all its source fingerprints against the merged checkout; `WIP.md` is the only new
source document outside that release inventory. Do not rebuild just for a commit,
merge, formatting-only change or this handoff.

**Not measured on hardware:** real SD/page-turn latency, ghosting, peak heap,
BUSY recovery, and power-loss behavior. Host timings and allocation/request counts
are not device improvements or Kindle parity. A normal reading check with AA off,
short pauses between turns, Library Author/Series navigation, and sleep/wake is
useful; recordings and cache deletion are not required for r50.

## Resume workflow

1. Revalidate branches, remotes, worktree status, SDK revision, and the flash
   pointer. Fetch upstream `develop` and inspect relevant maintainer PRs before
   choosing another change. Preserve this local merge and SDK commit.
2. Review the source, not just tests or prior prose. r49 abandoned a plausible
   prewarm expansion after discovering its scope freed prepared glyphs on exit.
   Do not reintroduce idle glyph/image work without proving retained benefit.
3. Next useful work is profiling remaining cold EPUB-open and large-Library scan
   costs with real parser/index/page fixtures. Measure I/O, allocations and work
   counts before changing caching, sorting or refresh policy. Do not manufacture
   a speed percentage from an isolated helper benchmark.
4. Keep pending upstream #3705 deferred while its documented varying-width-font
   cold-layout regression remains. Draft #3675 image prefetch had unresolved input
   responsiveness concerns; it was reviewed, not imported.
5. Formatting: only `./bin/clang-format-fix -g`. Use a temporary index when including
   untracked files; do not overwrite the real index. Never use raw clang-format.
6. Validate the final source with full retained test-name inventories and the
   relevant target firmware build once. Publish exactly one next final BIN only
   after all checks pass; update `build/FLASH-LATEST.md` last.

## Local validation setup

Compiler mirror and runtime live at
`/Users/danielyang/.local/share/crosspoint-build/`:

- `source/`: local build mirror; the authoritative checkout is
  `/Volumes/workspace/projects/crosspoint-reader`.
- `venv/bin/{python,cmake,ctest,pio}`: configured tools.
- `test-audit-r23-native-build/` and `test-audit-r23-sanitized-llvm/`: full host builds.
- `epub-r50/`: baseline/candidate evidence and reproducible validation/package scripts.
- `/opt/homebrew/opt/llvm@22/bin/`: sanitizer compiler; native uses Apple CLT.

The historical r50 release scripts intentionally assert the pre-commit root/SDK
base HEADs. After this merge, copy/adapt them for a future release, recording the
new local HEADs separately from the upstream base. Do not edit the published
package or silently weaken its source/test/dependency checks. Restore all retained
untracked sources from its archive when reconstructing an older release; a tracked
patch alone was insufficient before this wrap-up commit.

Ignore generated I18n/HTML headers, `.pio/`, `platformio.local.ini`, build artifacts,
and AppleDouble `._*` sidecars when staging. Quarantine a sidecar only after checking
its `00 05 16 07` magic. Keep the existing SDK nested assets intact.
