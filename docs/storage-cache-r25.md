# Upstream alignment and storage cache (r25)

This update is based on CrossPoint develop
`6743b68353dcc692e87826f5320c5480392f9e5b` and SDK
`111fdcc7f0176c3ee38391a160ee296bf492dbd8`. It integrates upstream translation
updates and UTF-8-safe screenshot folder names. Local-only translation keys are
retained without replacing upstream values. Existing reader/library features,
cache formats, reading positions and HAL locking remain in place.

## Storage changes

SdFat 2.3.1 already implements a separate inline FAT-sector cache. Enabling it
keeps cluster lookup data from being evicted by file and directory reads. The
extra cache is static storage, with no reading-time heap allocation. Actual X4
Pro RAM and flash totals are recorded in the packaged build log.

The dependency is pinned, and the build hook checks the exact original/patched
bytes before mutation. It leaves already-patched files untouched and rejects
unknown versions or source files. Two related correctness fixes accompany the
cache: failed fills invalidate their sector identity, and directory reads do not
advance an unused null output pointer. Dirty write-back failures preserve the
buffer for retry. No new storage layer or reader background work is introduced.

The cache override, failed-fill fix and original build hook are adapted from
[PR #3685 by Sung-jin Brian Hong (serialx)](https://github.com/crosspoint-reader/crosspoint-reader/pull/3685),
revision `be6543fd379d14670977b80cf996e3aabc20b623`. The directory-pointer guard
is local work discovered during independent sanitizer testing. Patch provenance
and removal criteria are in `scripts/sdfat_patches/README.md`.

## Validation and scope

`test/sdfat_cache` compiles the real, archive-hash-pinned dependency with the
production patch set. Both cache modes exercise 3,089 storage scenarios each,
including partial failed reads, copy-on-success reads, dirty primary/mirror
retries, FAT16/FAT32 fragmentation, backward seeks, complete payload checks,
remounts, mirrored FAT equality, exFAT and 1,000-book directories. Thirteen hook
checks cover exact sources, idempotence, preflight, path boundaries and dependency
selection. These are bundled into three CTest entries, not thousands of new
independently registered tests. All previous firmware test names remain required.

The final gates also include an independent clean-upstream reference suite for
the new develop revision, native and LLVM22 ASan/UBSan firmware host suites,
SDK display/UI/font checks, scoped static analysis, X4 Pro compilation and image
validation. A dependency gate compares every compiled SdFat source/header with
the pinned archive plus the reviewed patches.

In the isolated comparison, the 1,000-file enumeration plus 512-byte payload read
per file used 5,163 → 3,903 sector reads on FAT16 and 3,969 → 2,967 on FAT32.
The multiple-FAT-sector scattered-read workloads used 6,310 → 3,576 (FAT16) and
6,630 → 4,026 (FAT32). All payload/name checks matched; the exFAT workload was
unchanged. This is reduced simulated storage traffic, not measured device loading
time, full EPUB parsing, or a panel/ghosting improvement. The archived review in
`build/storage-cache-review-r25/` contains the original failing cases and matched
comparisons. The final package carries the integrated-source validation.

X4 Pro single-sector SDMMC reads copy from a DMA buffer only on success, so the
partial-fill corruption test is not evidence of that failure on this device.
Other boards' SPI paths and the common filesystem contract still require safe
cache invalidation. Physical SD failure recovery, peak heap and panel behavior
are not established by host tests.

## Device check

Flash only the image named in `build/FLASH-LATEST.md`, using Xteink X4 Pro →
Custom .bin. Start with anti-aliasing off. Open an uncached EPUB, navigate chapters,
reopen it, then exercise a large library, Authors/Series drill-in, bookmarks and
sleep/wake. No cache deletion, recordings or intentional card removal are needed.
Keep original progress caches when creating fresh test copies.
