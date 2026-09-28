# Storage cache candidate review

Historical investigation snapshot. See `storage-cache-r25.md` for the subsequent
integration; final flash eligibility is determined by `build/FLASH-LATEST.md`.

Status: isolated host candidate, not included in the firmware. The intended flash
image remains `build/x4pro-epub-r24/firmware-x4pro-epub-r24-final.bin`.

The review started from CrossPoint develop
`4a6283db9c9692e059eaf2d57d5b7a36d85a2f4d`. During final verification, upstream
advanced to `6743b68353dcc692e87826f5320c5480392f9e5b`: translation updates
(#3396) and UTF-8-safe screenshot folder names (#3750). These were fetched and
reviewed, but have not yet been applied to the working branch.
This follows Phase 1's storage, memory, reader performance and consolidation work.
No alternative filesystem, background reader task, cache format or UI is added.

## Candidate and evidence

[CrossPoint PR #3685](https://github.com/crosspoint-reader/crosspoint-reader/pull/3685)
by Sung-jin Brian Hong (`serialx`), head
`be6543fd379d14670977b80cf996e3aabc20b623`, is open as reviewed on 2026-09-26–27.
It enables SdFat's existing separate FAT cache, guards its configuration override,
and invalidates a sector cache after a failed fill. Its patch hook checks the
exact SdFat 2.3.1 source bytes. Adoption is not a maintainer decision yet.

The installed dependency justifies investigation:

- `SdFat/src/common/FsCache.cpp:41`: a failed read can overwrite buffer contents
  while the cache retains the preceding sector identity.
- `SdFat/src/FatLib/FatPartition.h:196`: the library already supports an inline
  second cache for FAT lookups; no new cache implementation is needed.
- `SdFat/src/FatLib/FatFile.cpp:871` and `:913`: directory reads pass a null output
  pointer but still increment it. UBSan reproduced this with both original and
  proposal sources. A separate experimental guard leaves that unused pointer
  unchanged. It is not part of PR #3685.
- `freeink-sdk/libs/hardware/SDCardManager/src/SdmmcBlockDevice.cpp:132`: X4 Pro's
  single-sector read copies from its DMA buffer only on success. The partial-fill
  corruption result must not be presented as an observed X4 Pro failure.

## Test design

The harness compiles the real installed SdFat 2.3.1 sources, rather than a cache
reimplementation. Its only substituted hardware is a sparse in-memory block
interface and a deterministic advancing clock. The file API, formatter, cluster
chains, directory parsing, cache, writes and remounts are real library code.

Six builds separate the effects: original/fill-fixed/directory-hardened sources,
each with the separate FAT cache disabled and enabled. The configuration guard
is present in each experimental source tree so both values are selectable;
original mode 0 retains ESP32's original cache policy.

Each mode has 3,082 cache scenarios: all failed-fill lengths from 0 through 512,
three subsequent access paths (prepare, cache-safe read, mirrored write), both
partial-overwrite and copy-on-success transports, plus dirty primary/mirror
write-back retries, successful write-back followed by a failed read, and reserve
without read. Seven filesystem workloads add FAT16/FAT32 fragmented files,
multiple FAT sectors, interleaved handles, backward seeks, writes, remounts,
complete payload checks, FAT-copy parity, exFAT, and 1,000-book directories.

The original sources fail 1,536 corruption scenarios in each cache mode. The
proposal fixes all those assertions, but the directory-pointer UBSan finding
remains. Adding the small guard passes **3,089 scenarios in each cache mode**,
with ASan, leak checking and UBSan configured to stop on errors, without
suppressions. These scenario counts are separate from r24's 1,452 firmware tests.

Thirteen tests of the exact proposal patch hook pass: source hashes,
idempotence and unchanged mtimes, unknown versions/source/patches, atomic
preflight, path containment, selected dependency and SDK-only handling. They
exercise real patch application in temporary directories. No production
dependency was patched.

## Synthetic storage results

All data and name checks match. Counts are block-interface sector reads, not
elapsed device time. The library simulation enumerates 1,000 long-name files
and reads 512 bytes from each; it does not run the entire Library activity or
parse 1,000 EPUBs.

| Workload | Shared cache | Separate cache |
| --- | ---: | ---: |
| FAT16, 400 scattered small reads | 790 | 401 |
| FAT32, 400 scattered small reads | 790 | 401 |
| FAT16, 3,000 reads across multiple FAT sectors | 6,310 | 3,576 |
| FAT32, 3,000 reads across multiple FAT sectors | 6,630 | 4,026 |
| FAT16, 1,000-book enumeration and small payload reads | 5,163 | 3,903 |
| FAT32, 1,000-book enumeration and small payload reads | 3,969 | 2,967 |
| exFAT, 24 scattered small reads | 46 | 46 |

The benefit comes from keeping FAT lookup data while reading directory/file
sectors. It is smaller when chains span multiple FAT sectors; exFAT has separate
existing cache policy and shows no change in this workload. No panel speed or
ghosting improvement is implied.

The second cache is an inline object, not a heap allocation. Host FatPartition
size grows by 536 bytes, including 64-bit pointer/padding overhead; that is not
an ESP32 memory measurement. Actual X4 Pro static RAM and firmware size still
need a firmware build. Host vectors/maps are test-only and are not proposed for
firmware.

## Findings during harness development

The initial ARM64 host build selected SdFat's native unaligned-load shortcuts.
The final build undefines the host unaligned-access feature macro to select the
same byte-decoding path used on ESP32. Alignment diagnostics from the first
host configuration are retained separately. The null-pointer arithmetic finding
persisted with the correct endian path and was fixed in the candidate.

A constant clock stub exhausted short-name alias choices in the common-prefix
1,000-file fixture. The final stub advances deterministically, and all names and
contents are checked. Initial hook tests also exposed a diff-extraction mistake
in the test setup; proposal files were fetched from their immutable GitHub
revision and checked against their Git blob/source hashes before the passing
hook run. Neither setup error changed production code.

## Integration work still required

First integrate the new upstream commits. A three-way preview found ten textual
translation conflicts but no conflicting key values: the reviewed resolution
retains every upstream value and appends twelve local-only keys across eleven
translation files. The other four files match upstream without local conflicts.
The preview is evidence for integration, not an applied or tested rebase.

Promote the source-pinned dependency tests into the normal test runner, then
integrate the reviewed patch hook and cache flag with explicit author provenance.
Include the independently tested directory-pointer guard or re-evaluate if
upstream supplies an equivalent fix. Preserve the existing HAL mutex and SDK
storage backends. Review the exact dependency source delta and measure X4 Pro
RAM/flash before packaging a new image. Run all retained firmware tests, SDK
checks and the firmware/image gates after the final executable edit.

Evidence is under `build/storage-cache-review-r25/`: harness sources, exact
proposal files, dependency source archives, commands, hashes and all result
logs. This review does not change `build/FLASH-LATEST.md` or justify reflashing.
Future device checks can use ordinary opening, library navigation and sleep/wake;
no recordings or intentional card removal are required.
