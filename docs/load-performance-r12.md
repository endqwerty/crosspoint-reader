# r12: page composition and Library loading

Target: Xteink X4 Pro, ESP32-S3, `x4pro-gh_release`.
Version: `1.6.5rc02-x4pro-r12-6c83edd`.
Intended application image: **`firmware-x4pro-epub-r12-final.bin`**.
Use `build/FLASH-LATEST.md` to identify the verified package after validation.

## Changes

Cached EPUB pages read adjacent geometry, text headers, and style fields in small
groups instead of issuing a separate HAL read for each scalar. The serialized
layout and cache version stay the same. Signed fields use aligned arrays or
`memcpy`; all enum, boolean, length, allocation, and truncation checks remain.
This reduces storage mutex entries and HAL/SdFat calls without enlarging the
page cache or adding heap allocations.

Resume/search position lookup scans the existing visible-offset table in chunks
of sixteen entries using a 64-byte stack buffer. It preserves first/last duplicate
offset selection and incomplete-chapter behavior. Every seek and read is checked;
64-bit bounds checks reject truncated or overflowing tables. Active builds still
resolve known offsets in memory without SD access. An early match can read up to
60 extra table bytes; a full-table scan reads the same byte count as before.
The reverse page-to-offset lookup also checks all I/O and the complete table's
bounds. A reproduced failure previously accepted an early entry from a truncated
table, and unchecked seeks could return unrelated bytes as a saved position.
Unreadable or malformed tables now return no position. Healthy reverse lookup
still uses four reads, three seeks, and eleven bytes.

The Library index tracks its file cursor with one `uint32_t`, avoiding seeks
before consecutive reads. Failed operations, closing, and reopening invalidate
the cursor. A shared metadata reader collects title plus canonical/source author
in one pass through a 64-byte stack window. It reuses the caller's strings, clears
both outputs on failure, and distinguishes empty fields from failed reads.
Visible labels and unchanged-book reconciliation use this reader. Title headings
also reuse the record already read for the label. The index format, full
recovery validation, manual SD-file reconciliation, and bounded visible window
remain unchanged. There is no whole-library RAM cache.

Ordinary font glyphs resolve clipping, rotation, and framebuffer coordinates once
per glyph, then paint packed pixels directly into the existing framebuffer or
gray strip. Scaled superscript/subscript and explicitly rotated text keep their
existing paths. This follows [upstream PR #3633](https://github.com/crosspoint-reader/crosspoint-reader/pull/3633),
head `92f76969b4e30ef93073c348c528468d99ec5932`, authored by
Sung-jin Brian Hong `<serialx@serialx.net>`. Preserve that human attribution if
this adaptation is committed. The raster helper adds no heap allocation or
framebuffer and does not change panel waveforms, refresh scheduling, or BUSY
recovery.

Long-title truncation keeps one mutable candidate instead of allocating a new
temporary for every removed character. The output string already belongs to the
existing API; reserving room for its ellipsis once replaces repeated allocations.
Width measurement, UTF-8 boundaries, exact-fit behavior, shaping, and font
fallback retain their previous semantics. [Upstream PR #3573](https://github.com/crosspoint-reader/crosspoint-reader/pull/3573)
identified the same hotspot; its binary-search implementation was not adopted
because font fallback and contextual glyph metrics can change between prefixes.
This refactor reduces allocation churn, not the number of width measurements.

## Deterministic host measurements

All comparisons use the verified r11 source as the baseline. Counts describe
host fixtures running production code, not elapsed SD-card or panel time.

| Workload | r11 | r12 |
| --- | ---: | ---: |
| Uncached 18-line prose page: HAL reads | 487 | 193 |
| Annotated cached page: HAL reads | 492 | 198 |
| Last-page lookup in a 1,024-page section: HAL reads | 1,027 | 67 |
| Sequential 4,096-record Library scan: seeks | 4,096 | 1 |
| Ten representative labels: reads / seeks | 60 / 60 | 20 / 20 |
| Unchanged 4,096-book reconciliation: reads / seeks | 45,057 / 45,056 | 20,481 / 20,480 |
| 512-character Latin title truncated to 120 px: observed C++ allocations | 494 | 1 |

Page fixtures preserve 2,598/2,852 bytes read, five seeks, and 59/64 allocation
calls respectively. Retained-page reuse still performs zero storage reads and
zero allocations. The 1,024-page lookup reads 4,103 bytes, performs three seeks
and one open, and allocates nothing in both versions.

The Library window trades some extra bytes for fewer calls. The ten-label
fixture reads 1,620 → 1,920 bytes; unchanged reconciliation reads
1,245,248 → 1,359,920 bytes (about 9.2% more), with zero EPUB metadata reparses.
Long metadata may require more reads but fewer seeks: at the writer's maximum
author/title/source lengths (128/255/128 bytes), display metadata changes from
5 reads + 5 seeks to 7 reads + 1 seek; source metadata changes from 7 + 7 to
8 + 2. The sequential record scan reads the same 524,288 bytes.
These operation counts do not establish a fixed speedup for a physical SD card.

The title fixture observes 135,361 → 520 cumulative allocation bytes on the
native host STL. This counter measures calls to the executable's replacement
operator new; out-of-line libc++ string operations can bypass it under ASan.
Allocation-budget tests include positive controls and do not interpret zero
observed calls as zero heap use. These figures are neither peak memory nor ESP32
heap-size measurements. No extra persistent title cache is introduced.

The paired glyph benchmark compares 192 scenarios with eleven timed samples per
scenario on an Apple M4 using Apple Clang 17. Across the 96 prewarmed scenarios,
the median CPU-time reductions were 52.30% for BW redraw and 37.47% for gray
composition; without prewarming they were 6.30% and 5.55%. All results are retained:
22 BW and 36 gray scenarios were slower, with worst regressions of 12.30% and
18.11%. The baseline ran before the optimized executable, rather than interleaved,
so timing drift is possible. Setup/prewarming, SD I/O, SPI, panel BUSY, and optical
settling are excluded. These figures are neither device timing nor complete
book-opening latency. See `test/gfx_refresh/README.md` for reproduction and the
package's `verification/glyph-raster-summary.json` for the full breakdown.

## Regression coverage and validation

Production-code host tests cover every truncated page byte and injected page
read failure; invalid boolean/style bytes; signed geometry; maximum section
page count; duplicate offsets across chunk boundaries; partial-cache fallback;
failed and positive-short section reads; Library cursor retry/reopen; maximum
and empty metadata; malformed blobs; backup recovery; visible row fallback;
and unchanged-library metadata reuse. Truncation is compared with the frozen
r11 algorithm at every pixel-width boundary for Latin, accented, combining,
Cyrillic, mixed RTL, missing-glyph, and malformed UTF-8 fixtures across six styles.
Both directions of page/visible-offset conversion have fault and overflow tests.

The raster checks compare complete framebuffer bytes against the previous
per-pixel implementation, as well as a scalar reference for packed glyphs.
The package records native Release and LLVM22 ASan/UBSan results, scoped static
analysis, the production firmware build, source hashes, and ESP32-S3 image
inspection. Packaging publishes the latest pointer only after those gates pass.

For a device check, flash the named application BIN using the web flasher's
**Xteink X4 Pro → Custom .bin** selection. Keep anti-aliasing off for the first
reading pass. Open a previously read book, turn forward/back through cached
pages, resume near a long chapter's end, and open a Library containing many
books. Check Titles, Authors → books, and Series → books, including a long title.
Try anti-aliasing afterward if desired. Recordings are unnecessary; report any
wrong text, missing rows, visual differences, stalls, or resets. Real heap/stack
headroom, SD timing, panel timing, and optical ghosting still require the device.

Earlier dark boot, Library grouping, lazy Find in Book, refresh safety, and
transactional persistence behavior are retained. The freeink SDK is unchanged
from r11. This release makes no claim of Kindle parity or reduced panel time.
