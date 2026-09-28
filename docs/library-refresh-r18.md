# r18: retain Library context after refresh

Based on CrossPoint develop `1d61100f90d2e7e32965c14301320d9989a7ab71`
and FreeInk SDK `111fdcc7f0176c3ee38391a160ee296bf492dbd8`. The upstream
branch was rechecked before validation; no newer develop commit was present.

## Behavior

Explicit Library refresh remembers the selected book by its exact SD path.
After rebuilding through the existing LibraryBuilder, the selection follows that
book in the current order and filter. Expanded Authors/Series views follow the
book into its current group, including updated metadata headings. Names-only
directories stay collapsed. Optional Title letter groups retain their letter.

If the book disappears or no longer matches the filter, prefer its next neighbor
from the previous view, or its previous neighbor when it was last. Expanded
views capture neighbors only within the same group. When neither anchor survives,
return to a valid nearby directory/list row. Empty views focus the tabs. Refresh
never opens a book automatically. Tab focus inside a group keeps that group;
root tab focus and Recent selection require no identity lookup.

A failed rebuild can retain the selection in the old valid index and keeps the
session stale for retry. Reopen, filter or allocation failures expose only valid
navigation; no partial lookup result is published after an SD read failure.

## Implementation and resource bounds

This extends LibraryIndexFile and LibraryListActivity rather than introducing a
parallel catalog, saved-selection format or alternative navigation component.
Upstream sort/group keys, builder reconciliation and cache formats are unchanged.
The state is transient and used only by explicit refresh; nothing is retained or
executed during active reading.

The snapshot holds at most two paths, their persisted hashes and file-size hints.
It uses existing bounded index path reconstruction (255-byte folder and filename
fields) on this cold path. The strings survive only through the refresh. Fixed
stack copies would exceed the C3 stack budget and persistent member buffers would
retain that memory outside refresh. The backend allocates one checked, nothrow
4 KiB chunk, reused for 32-record reads and permutation reads. It is freed on every
exit, including errors. No table proportional to library size is added.

The first pass uses filename length and old file size to avoid most individual
hash reads. A second pass considers changed-size records for unresolved paths,
so editing an EPUB in place does not hide it. Hash matches are confirmed against
the full path; a hash collision cannot select another book. Permutations are
checked for out-of-range values and duplicate/missing matched ordinals before
publishing results. Existing filter rows are searched in memory.

A 4,096-book synthetic fixture sets the two anchors at the end and uses equal
filename lengths. Its regression budgets are at most 144 HAL reads with distinct
file sizes, or 4,240 with all file sizes equal, and at most 570,000 bytes in either
case. This measures the added lookup work, not the complete rebuild or physical
SD latency. Changed-size/missing anchors may require a second record scan. Many
folders also increase the existing path reconstruction work. No wall-clock speed
or peak-heap improvement is claimed from these counts.

## Verification

Backend tests compile the complete production index implementation and cover all
eight sort directions, hash collisions, changed/unknown file sizes, absent paths,
invalid input, OOM, short reads, seek errors, failure at every read, malformed
permutations/path offsets and the 4,096-book storage budgets. UI tests extract the
actual production capture/restore/refresh code and cover reordering, deletions,
author metadata changes, series directories, filters, Recent, tab focus, optional
Title groups, failed rebuilds and degraded/OOM/reopen failures. The existing touch
refresh test now expects its expanded group to survive, intentionally replacing
its former reset-to-directory expectation; its test name is retained.

Release requires the complete previous test registry plus these tests in native
Release and LLVM22 ASan/UBSan, SDK runners, scoped static analysis and the X4 Pro
build/image check. Actual gate results and source fingerprints are in the package.

On device, select a book, then use Library Options / Refresh and check selection,
Back and Open in Title, Authors and Series, including an active filter. After an
SD library change, check that refreshing a surviving book retains it and a removed
book selects a neighbor or returns to the directory. Start with anti-aliasing off
and check normal page turns, dark boot and night-mode wake. No recordings are
needed. Host tests do not establish physical ghosting, SD timing or device peak
heap. Existing r17 caches, saved positions and bookmarks remain usable.
