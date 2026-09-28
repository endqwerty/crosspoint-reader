# Library sort cache r26

Based on develop `6743b68353dcc692e87826f5320c5480392f9e5b` and SDK
`111fdcc7f0176c3ee38391a160ee296bf492dbd8`, preserving the r25 reading and library features.

## Mechanism and compatibility

`LibraryIndexFile::ordinalForRow` previously issued one two-byte HAL read for each
Author, Added or Series rank. Interleaved record reads prevented sequential access.
A fixed 32-entry (64-byte) member cache now batches adjacent ranks. Title order
remains arithmetic. Both directions share the same window, and absolute file
offsets distinguish the three sort tables. Public APIs and on-card formats are
unchanged; this is a refactor within the existing index reader.

The cache has no separate heap allocation and no background task. It enlarges the
existing index object by its fixed storage and bookkeeping; this is not free RAM.
The window is discarded on close and failed seeks/reads, and is published only
after a complete fill. Final windows read only the remaining entries. Invalid
ordinals are rejected when selected, matching prior behavior. Random access can
read up to 64 bytes for one selected rank; sequential library navigation is the
intended tradeoff. The cache does no work during active book reading.

## Host evidence

A real-index-reader test interleaves all 4,096 sorted ranks with their records in
six orders (Author, Added, Series; ascending and descending). Before: 8,192 HAL
reads per scan. After: 4,224, a 48.4% reduction in calls. Each scan reads the same
532,480 bytes. This measures HAL calls, not physical sectors or elapsed SD time.
The baseline was run against the saved r25 reader implementation.

Eight new tests cover the scan budget, final partial windows, direction sharing,
sort-table separation, reopen/failed-open invalidation, failed/short/seek reads,
other index I/O failures, corrupt ordinals and title/invalid-row bypasses.
Existing tests and the pinned upstream registry remain required by release gates.
See the package verification logs for full native/sanitizer results and resource
figures. Clean-upstream validation is retained from r25 for the identical base.

## Device verification

Flash the single final image identified in `build/FLASH-LATEST.md`, start with
anti-aliasing off, browse Authors/Series/Added in both directions and cross page
boundaries, open books and return, then check sleep/wake. Existing library and
reading caches remain compatible; do not delete progress. Actual SD latency,
physical ghosting and peak heap require device checks and are not claimed here.
