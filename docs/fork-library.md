# Library: offline index, browsing, search and book state

The Library is an SD-backed, offline catalog of every book on the card. It sorts,
groups, filters and searches thousands of books without opening them and without
holding a book collection, cover images or per-book strings in RAM. Favorites and
reading status live in separate per-book sidecars. Browse Files separately offers a
bounded recursive filename search that needs no index.

The byte layouts of `library.idx` (CLX1) and the book-state records are defined in
[file-formats.md](file-formats.md); this document does not repeat them.

## User-visible behavior

### Tabs, options and book actions

- Four tabs: **Recent** (reading history), **Added**, **Title**, and a last tab that
  is **Author** or **Series** (`SETTINGS.libraryGroupBySeries`). Recent entries whose
  files have disappeared are pruned on entry, after the refresh, so renamed books
  are relinked first.
- Library Options opens from the header icon or by holding Confirm while the tab
  strip has focus ("Hold: Options"): Grouping (Title, Author, Series, Added, Recent),
  Filter (All, Favorites, Unread, Reading, Finished), Reverse sort, Refresh library.
- Holding a book row (or Confirm) opens book actions: Favorite/Unfavorite, Mark
  Unread/Reading/Finished, Delete (confirmed), Book details, and either Show groups
  (Title letter index) or Remove from recents.
- Indexed tabs support text search and ascending/descending order. The search
  keyboard accepts at most 48 bytes.
- Titles, authors and series come from EPUB package metadata when "use metadata"
  is enabled; otherwise, or when a book has no title, the filename stem is used.
  The filename itself is never altered and remains the path used to open the book.
- Status line text: "Could not rebuild the index" after a failed rebuild, "Library
  (some files excluded)" for a partial scan, "Library (unsorted)" when ranks are
  degraded.

### Author and Series directories

- Author and Series open as directories: one name per row, no titles or subtitles.
  Selecting a name opens only that group's books; the header shows the selected
  name. Back returns to that name and its previous viewport.
- Books without author metadata are grouped under **Unknown author**; books without
  a series under **Standalone books**. Both remain reachable through Title and
  Added.
- Series children show their volume numbers. Fractional positions (e.g. 2.5) sort
  between integers; unnumbered books sort after numbered siblings.
- Author headings show the chosen spelling's curated author sort (e.g. "Le Guin,
  Ursula K.") unless the sort is written only in capitals while the name is not.
  Otherwise the heading is formed by moving the segment after the last ASCII space
  to the front with ", " (whole UTF-8 segments are moved). Names with no usable
  last space are shown unchanged.
- Search and shelf filters apply before grouping, so a group contains only matching
  books. Back from a child returns to the filtered directory; a further Back clears
  a text query. Changing a filter or sort direction or deleting a book returns to
  the rebuilt directory. An unfiltered favorite/status change keeps the group open.
  Opening a book leaves the Library; navigation is not persisted across reader
  sessions.

### Search

Library search matches the stored folded title prefix first, then the full title
(including the displayed title when a curated title sort differs), author, and
series name. Reading-state filters are evaluated for every text candidate.
Matching folds case and Latin accents and applies Unicode NFC/NFD normalization.

### Book details

Book details shows the full displayed title and absolute SD path, wrapped across
pages (previous/next buttons or vertical swipes). Back returns to the same shelf,
group, filter and selection; Open enters the book. It reads no book content and
triggers no metadata rescan. It addresses upstream issue 1170 (long filenames).

### Refresh and retained selection

- Reconciliation runs on the first Library entry after boot, after known SD
  mutations (a persistent `/.crosspoint/library.dirty` marker set by Browse Files
  deletes, web uploads and OPDS downloads), when the index is invalid or its
  metadata mode differs from the setting, or through **Refresh library**. Other
  entries reuse the validated index.
- Files copied to the card while the device stays powered on require Refresh
  library. After editing the card on a computer, eject safely, reinsert and reopen
  Library; external deletions are not otherwise detected.
- Explicit refresh remembers the selected book by exact SD path. After the rebuild
  the selection follows that book in the current order and filter; expanded
  Author/Series views follow it into its current group (with updated headings);
  collapsed directories stay collapsed; Title letter groups keep their letter.
- If the book is gone or no longer matches, selection moves to its next neighbor
  from the previous view, or the previous neighbor if it was last (expanded views
  only consider neighbors in the same group). Otherwise a valid nearby row is
  chosen; empty views focus the tabs. Refresh never opens a book.
- A failed rebuild keeps the old valid index and the selection within it, and
  leaves the session stale for retry.

### Reading state

- Opening a book marks it Reading. Leaving the reader while at the end of the book
  (not from a footnote) marks it Finished. Unmarked books can be classified manually
  or by opening them.
- State is keyed by the raw complete path. Renames performed through Browse Files
  on the device, and the "Move finished books to /Read" move, carry the state
  sidecar (with bookmark and cache files) and roll back on failure
  (`moveBookWithState()` in `src/util/BookStateMove.cpp`). The /Read destination
  skips names that already have leftover state.
- Books renamed or moved outside the device (a Calibre re-export after a title or
  author edit) keep their state when the book names a UUID (see "Relinking after
  external renames" below); a file without a UUID does not.

### Relinking after external renames

The Library refresh already pairs a vanished entry with a new one by book UUID (a
size match is the fallback and only keeps the "Added" position). For each pair
matched by UUID, `buildLibraryIndex` journals the old path during reconciliation
(`/.crosspoint/library.stage.r`, removed on every exit), and once the new index is
installed calls the caller's `RenameHandler` with the old path and the new path
read back from the installed record, which must carry the same UUID. Size-only
matches are never reported: a wrong pairing would give one book another's
bookmarks and progress.

The handler used by the Library and Home refreshes (`relinkRenamedBook`,
`src/util/LibraryRelink.cpp`) calls `relinkBookState()`
(`src/util/BookStateMove.cpp`), which moves the reader cache, the bookmark files
and the reading state to the new path with rollback on failure, deletes the old
reading state, repoints the Recent entry and the open-book path. It relinks only
when the old book is gone, the new book exists, the old path has state and the
new path has none: state already built at the new path is left alone and the old
state stays orphaned. The Library entry runs the refresh before pruning missing
Recent entries, so a renamed book stays in Recent.

Cost: one journal record per UUID rename (about 20 bytes plus the old path) on SD,
nothing kept in RAM, and a handful of existence checks per renamed book. Not
covered: books without a UUID, and a rename plus a same-UUID copy elsewhere (the
first match wins).

### End-of-book suggestions

The "Continue with" menu on a book's last page (EPUB and XTC readers) offers up to
three books from the Library index, so it works when every book sits in its own
folder (Calibre's `Author/Title/file.epub` layout), where a sibling-file scan
finds nothing (`lib/LibraryIndex/LibraryFollowOns.cpp`, used by
`EndOfBookOptions`):

1. The later volumes of the book's series, in series order (fractional positions
   included).
2. If none, the same author's later titles in author order. Books with no author
   identity (initials-only names) get none.
3. If the index has no answer (not built, book not indexed, nothing follows), the
   earlier behavior: later files in the same folder, in file-browser order.

Finished books and files no longer on the card are skipped. The menu is built once
per reader session at the end of the book from the index as last built; it does
not trigger a rebuild. Series membership needs an index built with book metadata
on. Cost: one index open, two bounded record scans to locate the book (the second
only for the author fallback) and at most 16 rows examined after it; nothing
library-sized stays resident.

### Browse Files: "Search folders and files"

- The first Browse Files row. Enter prefixes of one or more filename words; the
  open folder and all subfolders are searched, matching folder names and supported
  filenames (ASCII case and Latin accents ignored). Results show relative paths, so
  duplicate names stay distinct. Tapping a folder browses it; a file opens it.
- Back cancels an in-progress scan or clears completed results. Cancelling the
  keyboard keeps the query; entering a folder clears it; empty input restores
  normal browsing. Search stays available with no results.
- Book contents and metadata are not inspected. A matching parent does not make its
  children match. The firmware picker is unchanged.
- The search row is indexed separately from results; opening, deletion,
  return-to-folder selection and the firmware picker account for that offset. The
  query is drawn in the full-width subtitle slot (a trailing value slot overflowed
  the row with long queries).

## Index design

### Build pipeline (`LibraryBuilder`)

- **One walk** over the card (`.epub`, `.txt`, `.md`, `.xtc`; hidden entries and
  macOS `._*` sidecars excluded), at most five folder levels below the root (a
  corrupted FAT can contain a self-referencing directory). Records stream into
  `/.crosspoint/library.stage` in discovery order; nothing proportional to the
  library stays resident during the walk.
- **Duplicate dirents** are dropped using a fixed 1,024-key, fallible 8 KiB
  buffer per directory. If allocation fails or the cap is exceeded, the build
  continues with `DEDUP_DEGRADED`; a damaged FAT may then show duplicates but no
  real book disappears.
- **Metadata** parsing stops at the end of `<metadata>`, before the manifest; no
  spine, TOC, CSS or section cache is built. Changed EPUBs are parsed from the
  package, never from a stale path-keyed reader cache. Metadata is reused only when
  the path fingerprint, size, nonzero FAT timestamp, format/fold version, metadata
  mode and expected extraction status all match.
- **Title fallback** is materialized only when metadata supplied no title; reused
  records keep their stored folded key without rebuilding the stem.
- **Unchanged library**: if every record reuses metadata, counts match and no limit
  or unreadable entry was seen, staging files are discarded and the live index stays
  byte-for-byte unchanged. Refresh is a freshness check, not a forced reread.
- **Reconciliation** with the prior index reads all prior records contiguously
  (temporarily storing `nameOff` in the `PriorEntry::pathHash` slot), then resolves
  path hashes in a second pass, then sorts. This avoids alternating between the
  record and name-blob regions. Books whose path is new are matched first by book
  UUID (never pairing two different UUIDs), then by exact file size, to keep their
  place in Added; otherwise they are new. Arrival order uses file modification
  time with `firstSeen` breaking ties.
- **Install** is write-then-rename: `library.new` is written and verified against
  `selfSize`, the previous index moves to `library.bak`, and a failed rename rolls
  back. An interrupted install is recovered on the next build. Any staging, sort
  allocation (where not degradable), read/seek or short-read failure leaves the
  previous index in place.
- Long phases call `delay(1)` every 32 work units so the idle task runs; the
  reconciliation pass counts two reads per unit to keep that cadence.

### Author identity, spelling vote and surname order

- Each record's 12-byte author key is a 4-byte prefix plus an FNV-1a fingerprint of
  the complete normalized identity (cleaned, initials ignored, words sorted), so
  "Victor Hugo" and "Hugo Victor" group together without merging unrelated authors
  that share a prefix. A conflicting fingerprint during spelling verification fails
  the rebuild.
- A per-run vote (up to 16 spellings) chooses one display spelling per identity.
  The canonical source maps to itself; unknown/initials-only authors and degraded
  spelling allocation keep self-mappings. The surname sort key is computed only at
  canonical positions and then copied to the other members (all canonical keys are
  filled before copying, so sources may precede or follow destinations).
- Surname order uses its own 12-byte prefix, then canonical identity, then title
  ordinal, keeping each author's books contiguous even when surnames share a prefix.
- Output passes reuse the current staged record when it supplies its own chosen
  spelling; otherwise they read only the adjacent author length and 128-byte author
  field (a compile-time layout assertion protects that adjacency). Author cleanup is
  still applied to cached names, because a valid name truncated at 128 bytes can
  expose a trailing space.
- The fold/sort revision is bumped whenever folding or ordering changes; an older
  revision rebuilds once while preserving `firstSeen` and reading positions.

### Series

- Supports Calibre series metadata (which wins when both are present) and EPUB 3
  collections/refinements. Up to four collections and eight refines are staged so
  a series-typed collection is preferred over a box set. Series attribute/text
  values over 255 bytes are ignored rather than merged by truncated prefix; general
  metadata text is capped at 1,024 bytes (Expat may still buffer a larger token).
- Series identity is a digest of the full normalized name persisted before display
  truncation, so unrelated series sharing a long prefix do not merge. The ordering
  key is a 16-byte folded prefix plus that digest; this is deterministic but not
  full lexicographic order for names sharing 16 bytes, and not collision-proof
  against adversarial names.
- Series data is not added to reader Page/Section objects. Standalone-only
  libraries allocate no series maps or keys. Malformed series references stop
  installation rather than being converted to standalone books.

### Read side (`LibraryIndexFile`)

- Holds one file handle and header and reads individual fixed 128-byte records.
- **Rank cache**: a fixed 32-entry (64-byte) member window batches adjacent
  Author/Added/Series ranks; Title order is arithmetic. Both directions share the
  window; absolute offsets distinguish the tables. It is published only after a
  complete fill and discarded on close and on any failed seek/read. A random access
  can read up to 64 bytes for one rank.
- **Folder paths**: resolving folder N uses a fresh 64-byte stack window for nearby
  length bytes, never extending past the folder section or the current 512-byte
  sector. Failures leave the returned path empty.
- **Field reads** (`readFields`): one 64-byte stack buffer combines short fields
  and length bytes; spans of at least 64 bytes remaining are read directly into the
  pre-sized output string. Length checks precede copying; failure clears all
  outputs.
- **Read contracts**: `readAuthor` and `readSourceAuthor` return true for a valid
  empty value and false only for malformed records, closed handles or I/O failure.
  `readTitle` keeps false-on-empty because callers use it for filename fallback.
  Every caller (heading, drill-down, search, grouping) must check the return value;
  an unreadable author must never be shown as Unknown author.
- **Recent lookup** (`recentRowsFor`): up to 16 identities in two chunked passes
  with a fallible temporary 4 KiB buffer; no lookup cache is kept.
- **Refresh anchors** (`rowsForPaths`): at most two paths. One checked 4 KiB chunk
  is reused for 32-record and permutation reads. The first pass uses filename length
  and old size to avoid most hash reads; a second pass covers changed-size records
  so in-place edits are still found. Hash matches are confirmed against the full
  path; permutations are checked for out-of-range, duplicate and missing ordinals
  before results are published.

### UI side (`LibraryListActivity`)

- One render lock is held across input dispatch (index seeks, tab changes,
  filter/group maps, touch callbacks). Activity-result callbacks lock independently
  because the manager invokes them unlocked. Changed row maps invalidate old touch
  routing.
- The index handle is released while a child activity (menu, details, keyboard)
  is open and reopened on return.
- **Grouping** (`buildGroupStarts`) reads every book's ordinal and series reference
  but validates the shared series entry only once per contiguous run; disjoint runs
  revalidate, and no validation state survives a rebuild. Distinct identities with
  identical labels stay separate. A missing or corrupt reference, or a failed author
  read, aborts grouping and shows "Library view unavailable". Group scans yield every
  32 books.
- **Search** (`filterBooks`) remembers the last consulted series ID and its match
  result within one pass; this resets for every new pass. `foldInto` reuses a
  caller-owned output string (input must not alias the output; empty input clears
  it). Partial results are discarded on any read failure.
- Only the selected group's caption and the visible window's row strings are
  materialized. The author heading is built in place in the retained row string
  (reserve final size, rotate, insert) with no temporaries.
- **Details** prepares its text only when opened: one exact-size nothrow buffer,
  capped at 8 KiB; oversized or embedded-NUL input fails visibly rather than being
  shortened. Paging borrows spans of that buffer. It uses the SDK TextArea, whose
  walker measures complete codepoints and wraps at the same 220-byte bound used for
  drawing, so no bytes become unreachable and oversized glyphs still progress.

### Book state (`LibraryBookState`)

- One 16-byte record per book under `/.crosspoint/library-state/`, separate from
  the rebuildable index. A missing record means unread and not favorite. Corrupt
  records fail rather than silently resetting.
- Writes use exact-sized path buffers and no extra heap; they validate a closed
  `.new`, recover a valid `.bak` before replacing a corrupt main record, and install
  by rename with rollback. Unchanged state is not written.

### Refresh session (`LibrarySession`)

Two atomic 32-bit generations. A build captures `refreshToken()` before scanning
and reports `reconciled(success, token)`; a mutation that arrives during a
successful build leaves refresh pending. The persistent dirty marker is cleared
only if no newer invalidation occurred.

## Memory and I/O budgets

These are allocation bounds, not measured device peaks. File handles, firmware
state, allocator overhead and temporary strings are additional.

| Item | Bound | Lifetime |
| --- | --- | --- |
| Prior-index reconciliation | 16 B/book (64 KiB at 4,096) | Released before sorting |
| UUID match table | per unmatched prior entry | Rebuild only |
| Duplicate keys | 8 KiB | Walk only |
| Title / author sort keys | 14 B/book each (57,344 B), phase-local, sequential | Rebuild only |
| Series sort keys | 24 B/series book (98,304 B max) | Rebuild only |
| Worst-case simultaneous series-phase arrays | 147,456 B (series keys, three u16 series maps, title order, arrival order, resolved `firstSeen`) | Rebuild only |
| Modification-time array | 4 B/book; falls back to `firstSeen` if unavailable | Rebuild only |
| Staging record, name buffer | one heap `StagedEntry`, 512 B | Rebuild only |
| Rank cache | 64 B + bookkeeping inside the index object | While Library open |
| Filter row map, group-start map | u16 per row, 8 KiB each max | Released with the activity |
| Refresh / Recent lookup chunk | 4 KiB | Per call |
| Details text | ≤ 8 KiB | Details activity |
| Browse Files search | 256 results, 256 pending dirs, 16 KiB path bytes each, 1,024 B max path; ~44 KiB during a scan on 32-bit | Search activity |

Design reasons:

- Sort arrays and staging records exceed the task stack budget; static storage
  would pin scarce DRAM, so they are fallible heap allocations released between
  phases. The metadata parser is also heap-allocated because its bounded staging
  tables exceed the stack.
- The fixed 128-byte record lets record *k* be found at `recordStart + 128k` and 32
  records tile a 4 KiB aligned read; series data lives in separate sections rather
  than widening the record.
- The refresh snapshot holds two strings only during refresh; fixed stack copies
  would exceed the C3 stack and member buffers would retain memory outside refresh.
- Browse Files search reserves both work vectors to their bounds up front, walks
  iteratively (queued relative paths, no recursion), and holds at most one directory
  handle and one entry handle. Each input-loop pass does at most eight steps and
  stops after 20 ms; one SD call can still block. Traversal never holds the render
  lock; the sorted list is published under the lock. Results keep raw SD bytes for
  opening and deletion; NFC is applied only to display copies. Every scanner close
  is guarded by a valid-handle check (closing a default `HalFile` asserts).

Nothing in the Library runs during active reading: no background task, no search
heap, no periodic scan. All Library allocations are released when the activity is
destroyed to open a book.

## Failure handling

- Unreadable book entries are skipped and mark the index partial; an unreadable
  directory aborts the scan and keeps the previous index, with a rebuild error shown.
- `LIMITS_REACHED` marks a partial scan (another eligible book beyond 4,096, depth
  beyond five, or a skipped entry). Exactly 4,096 books is not partial.
- Sort allocation failure sets `RANKS_DEGRADED`: affected orders fall back to walk
  order and the header says "unsorted"; tabs remain touch-reachable.
- Deleting the last book installs an empty index.
- Allocation, index-read or degraded-sort failures in grouping or search show an
  unavailable view rather than a partial directory or unrelated books; Title and
  Added navigation remain available.
- Refresh lookup failures (OOM, short read, seek error, malformed permutation or
  path offset) publish no partial result.

## Limits and known gaps

- Hard ceiling of 4,096 books (u16 ordinals), pending an external-sort design.
- Entering Library after a card change enumerates the card and writes staging
  files; metadata reuse avoids EPUB parsing, not all SD I/O, so large cards take
  longer to open.
- A same-size replacement that preserves its FAT timestamp reuses stale metadata
  until the index is reset.
- State and progress follow raw paths; external renames and moves keep them only for books that name a UUID.
- Series and author digests are probabilistic identities.
- The HAL does not distinguish end-of-directory from an entry read failure, so that
  SD error is invisible to the Browse Files walker. Unreadable directories, skipped
  subtrees (pending limit) or a result limit (stops at the first excess match) show
  "Partial results. Try a smaller folder."; truncated or unreadable names are
  skipped.
- Not established by host tests: physical SD latency, e-ink/touch behavior, peak
  device heap and largest free block (target > 50 KiB free during repeated Library
  and search entry/exit on X4 Pro).

## Code and tests

Code:

- `lib/LibraryIndex/` — `LibraryFormat.{h,cpp}`, `LibraryBuilder.{h,cpp}`,
  `LibraryIndexFile.{h,cpp}`, `LibraryText.{h,cpp}`, `LibraryBookState.{h,cpp}`,
  `LibraryFollowOns.{h,cpp}`, `LibrarySession.h`
- `lib/Epub/Epub/parsers/ContentOpfParser.{h,cpp}`, `lib/Epub/Epub.cpp` (metadata-only
  parse)
- `src/activities/library/` — `LibraryListActivity`, `LibraryMenuActivity`,
  `LibraryBookDetailsActivity`
- `lib/FolderSearch/FolderSearch.h`, `src/activities/home/FileBrowserActivity.cpp`
- `src/activities/reader/EpubReaderActivity.cpp` (Reading/Finished marks, /Read move)
- `src/activities/reader/EndOfBookOptions.{h,cpp}` (end-of-book suggestions)
- `src/util/BookStateMove.{h,cpp}` (book move with cache, bookmarks and state; relink after an external rename)
- `src/util/LibraryRelink.{h,cpp}` (rename handler for the Library refresh)
- SDK TextArea wrapping lives in `freeink-sdk`.

Host tests (`test/`): `library_format`, `library_index_file`, `library_builder`
(including `LibraryStagingTest.cpp`, which compiles the production builder TU to
reach `stageRecord`), `library_text`, `library_book_state`, `library_ui`,
`library_details`, `library_follow_ons` (real index builds with one folder per book), the `LibraryRenameRelinkTest` cases in `library_builder`, the `RelinkBookStateTest` cases in `file_browser`,
`content_opf_parser`, `folder_search`, `file_browser`.

`test/library_ui` extracts production methods (e.g. `readAuthor`, grouping,
filtering, refresh capture/restore, `formatAuthorHeading`) through `extract.py`;
changes to the index source regenerate the extraction, so UI tests exercise the
real read contracts rather than duplicated logic. Allocation-count tests include a
positive control so a blind counter fails; under ASan they use the allocator hook
because libc++ dylib allocations bypass the operator-new counter.
`test/library_details` extracts the production UITheme safe-area helper so its four
orientation checks exercise the real calculation.

## Attribution

- **Series metadata and indexing** adapt upstream
  [PR #3059](https://github.com/crosspoint-reader/crosspoint-reader/pull/3059),
  reviewed head `8a51da298ca19a6b69290b05012970a080974c6b`, by
  **Kenton Hamaluik <kenton@hamaluik.ca>**. Retain this human co-author.
- **Indexed Library**: [PR #2878](https://github.com/crosspoint-reader/crosspoint-reader/pull/2878)
  is oreglio's original indexed Library implementation (later split into smaller PRs);
  [PR #2885](https://github.com/crosspoint-reader/crosspoint-reader/pull/2885) is the
  standalone text core merged into the Library integration branch;
  [PR #3366](https://github.com/crosspoint-reader/crosspoint-reader/pull/3366) is
  Uri Tauber's Library integration, integrated at head
  `ad949bdd2c43d85fe53d81e08bb73a7874b1c25f` (upstream as `652ae0d8`).
  `lib/LibraryIndex/LibraryText.{h,cpp}`, its original host tests and
  `utf8DecomposedBase()` were first reused from PR #3366's head
  `9601f6f0ba9891ad5a6b7de13b19b418ae65322b` under the project's MIT license.
  Human authors verified from Git: Uri Tauber `uritaube@gmail.com`, Aurelien Sorce /
  oreglio `aurelien.sorce@gmail.com`, Leopoldo Pla Sempere
  `leopoldoplasempere@gmail.com`, and Justin Mitchell `justin@jmitch.com`.
  The Browse Files search integration and `FolderSearch` tests are local additions.
- **Larger keyboard** formerly used by Library and Browse Files search:
  [PR #3506](https://github.com/crosspoint-reader/crosspoint-reader/pull/3506), head
  `5a07daadfcb76d9c38097cb6f7a4e8bb0e03bbf1`, by Justin Mitchell `justin@jmitch.com`.
  Superseded by upstream's keyboard redesign (#3755); the fork now uses upstream's
  keyboard unchanged.
- **Always Next taps**, integrated alongside the Library:
  [PR #3513](https://github.com/crosspoint-reader/crosspoint-reader/pull/3513), head
  `f41d313b9c21ad71806b5d70f26ed6aef66d9db7`, by piJoe `git@r3l.dev`.
- **Calibre title/author sort and book UUID** parsing rules follow upstream PR #3757;
  the fork commit credits **Nolan Hawkins <nolanhhawkins@gmail.com>** (from the
  commit trailer; not listed in the revision docs).
- Prior art reviewed but not imported: [CrossVi](https://github.com/tvhdc/crossvi)
  (X3/X4 firmware with Library search); upstream PRs #3651 (file-as sorting) and
  #3707 (language-aware articles).
