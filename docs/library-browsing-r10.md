# Author and series browsing (r10)

Library's Author and Series views open as directories: one name per row,
without book titles or author subtitles beneath it. Selecting a name opens only
that group's books. Back returns to the selected name and its previous viewport.
Books without metadata remain accessible through Unknown Author or Standalone
books, as well as the existing Title and Added shelves.

The four-tab layout is retained. The last tab can be switched through Library
Options -> Grouping -> Author or Series. On a button-only device, hold Confirm
while the tab strip has focus to open Options. Recent, Added and Title retain
their existing book lists; Title's optional Groups action remains a letter index.

Author/series children show the selected name in the header. Series books retain
their volume numbers. Search and shelf filters apply before grouping, so a group
contains only matching books. Back from a child first returns to those filtered
groups; another Back clears a text query. Changing a filter or sort direction,
refreshing the index, or deleting a book returns to the rebuilt group directory.
An unfiltered favorite/status change keeps the current group open. Opening a book
leaves the Library activity; this change does not persist Library navigation
across reader sessions.

The index's surname ordering also keeps a canonical author's books contiguous
when different surnames share the stored 12-byte prefix. This uses the existing
canonical-author array during indexing, with no additional per-book allocation.
The fold/order revision is incremented so a previous index rebuilds once on the
first Library entry after updating; book progress and favorites are separate.

## Resource and failure behavior

The existing fallible group-start array is reused, at most 8 KiB for 4,096 books.
An active search/shelf filter retains its existing separate 8 KiB maximum row map.
A child uses a scalar group selection and the existing saved navigation state;
its row mapping is an offset into the parent list, not another book container.
Only one selected-group caption and visible row strings are materialized.
All of this belongs to Library and is released when it is destroyed for reading.

Group scans yield every 32 books. Allocation, index-read or degraded-sort failures
show an unavailable view instead of publishing a partial directory or opening
unrelated books. Existing Title and Added navigation remains available. No cache
format or code used for page refresh is changed by this release.

## Verification and firmware

Production-method host tests cover names-only rows, exact group membership,
non-first groups, forward/reverse sorting, filtered offsets, Back/viewport
restoration, book actions and deletion, missing metadata, 4,096-book bounds, and
allocation/read failures. Builder regressions cover author prefix collisions and
old-order index regeneration. Release packaging requires the complete native and
LLVM AddressSanitizer/UndefinedBehaviorSanitizer suites, X4 Pro release build,
scoped static analysis and chip/board/version image checks.

The intended application image is **firmware-x4pro-epub-r10-final.bin** in
`build/x4pro-epub-r10/`. `build/FLASH-LATEST.md` changes only after verification.
Use the web flasher's **Xteink X4 Pro -> Custom .bin** option.

After flashing, enter Author, select an author with several books, and check
that Back returns to its name. Switch Grouping to Series and repeat, including a
series whose books have volume numbers. Search and reverse the list once. No
recording is needed. Host tests do not establish device heap peaks, SD timings or
physical display behavior; the prior refresh and always-dark boot changes remain.
