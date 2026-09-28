# Library reconciliation read order r47

All r46 improvements remain on develop `93e98bb` and SDK `111fdcc7`.
This change reduces seek calls when preparing a library refresh.

## Mechanism

The prior-index preparation loop read one 128-byte record, then its eight-byte
path hash in the distant name-blob section, before returning to the next record.
For a large library this repeatedly switches between two file regions.

The preparation now has two passes. The first reads every record contiguously,
storing nameOff temporarily in the existing PriorEntry::pathHash slot alongside
the unchanged size, arrival sequence and ordinal. The second resolves those
stored offsets through readPathHash and replaces each temporary value with the
actual hash. Only then is the prior table sorted or handed to the directory walk.
Either failure returns before staging, preserving the existing committed index.

No additional buffer, heap allocation or persistent table is needed. PriorEntry
stays 16 bytes. The existing record and hash methods still validate the same
fields and use HAL storage. A two-read counter retains one scheduling work unit
per two reads across the two passes, including odd library sizes. No index format,
sort policy, metadata matching or active-reading behavior changes.

## Synthetic evidence

The unchanged-index refresh was run before and after with the same real builder
and index reader, using reversed title order so record ordinals differ from file
order. Every saved index remains byte-for-byte identical, every metadata record
is reused, and no EPUB parsing or index replacement occurs.

| Books | Reads, unchanged | Seek calls before | Seek calls after | Bytes, unchanged | Yield calls, unchanged |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 6 | 5 | 5 | 373 | 0 |
| 33 | 166 | 165 | 133 | 10,997 | 3 |
| 512 | 2,561 | 2,560 | 2,049 | 170,025 | 48 |
| 4,096 | 20,481 | 20,480 | 16,385 | 1,359,913 | 384 |

At the format limit, the complete unchanged rebuild makes 4,095 fewer seek
calls, about 20 percent. Reads and bytes are unchanged. These are HAL stub call
counts, not measured physical SD transactions or device time. Consecutive record
reads avoid switching file regions and should improve cache locality, but no
physical cache-hit or timing measurement is claimed.

The test freezes output, metadata reuse, reads, bytes, seeks and yield budgets.
Existing changed-library, sort-order, arrival-history, metadata-migration and
interrupted-rebuild tests remain required.

## Device check

Flash the single final X4 Pro image linked by build/FLASH-LATEST.md. Refresh an
unchanged Library, then refresh after adding or removing books on the SD card.
Check titles and sort order. No recordings or cache deletion are required.
Start with AA off for reading. Device timing and peak heap remain unmeasured.
No commits, pushes or PRs were created.
