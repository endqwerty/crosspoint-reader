# Series grouping r27

Based on develop `6743b68353dcc692e87826f5320c5480392f9e5b` and SDK
`111fdcc7f0176c3ee38391a160ee296bf492dbd8`, retaining all r26 features.
Upstream develop was checked live and remains at this revision. Open PR #3651
(file-as sorting) remains unmerged at `8552ecaaa543dbaa56c83463671b3562a63c846e`;
this refactor does not change sort keys, metadata fields, reader APIs or formats.
It follows ROADMAP Phase 1 consolidation within the existing activity.

## Mechanism

`LibraryListActivity::buildGroupStarts` used `seriesFor` for every book, reading
the same shared series-table entry once per volume. It now reads every book's
ordinal and series reference, then validates the shared entry once when a new
contiguous series group starts. The open index is immutable. A missing or corrupt
reference still aborts grouping, and a failed shared entry discards the partial
map. Standalone books require no series entry. Distinct identities with identical
labels remain separate. Disjoint runs revalidate their entry on each boundary.

There is no new resident cache, heap allocation, class member or background work.
The existing group map and scratch string are reused. Each invocation validates
shared entries afresh; no validation state survives an index reopen or rebuild.
`seriesFor` still reads the entry when the UI actually needs a label or volume
position. Existing display and child-navigation behavior remains unchanged.

## Synthetic evidence

Tests compile the actual grouping method through the established UI extraction
harness, with a counted index dependency. A 4,096-book fixture in 64 groups
previously requested 4,096 shared series entries; it now requests 64, a 98.4%
reduction in that request category. All 4,096 series references are still read.
This is not a 98.4% reduction in total I/O or elapsed time. With every other row
filtered out, entry requests fall from 2,048 to 64. Both sort directions have the
same budgets. The baseline ran against the saved r26 activity implementation.

Four additional tests cover large/filtered/reverse group budgets, failed ranks or
references within existing groups, revalidation on repeated builds, standalone
books and repeated identity runs. Existing UI failure/navigation cases remain.
Backend suites separately validate actual on-card records and corruption paths;
the UI fixture does not measure HAL bytes, physical sectors or FreeRTOS timing.
See the package logs for full registry, sanitizer, firmware and resource results.
Clean-upstream validation is retained from r25 for the identical base revision.

## Verification on device

Use the final BIN named in `build/FLASH-LATEST.md` for Xteink X4 Pro. Start with
anti-aliasing off. Open Series with many volumes, reverse sort direction, search
or filter, enter a series and return, then resume a book and check sleep/wake.
No migration or cache deletion is needed. Device latency, physical ghosting and
peak heap remain unmeasured. Retain original reading-progress files.
