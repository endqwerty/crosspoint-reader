# WIP handoff — X4 Pro reader, r50

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
