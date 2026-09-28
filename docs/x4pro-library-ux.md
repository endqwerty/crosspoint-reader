# X4 Pro Library and touch integration

This local integration retains the recursive Browse Files search and adds the
larger keyboard, Always Next reader taps, and indexed Library. Target:
`x4pro-gh_release` (Xteink X4 Pro, ESP32-S3). Branch:
`feature/x4pro-library-ux`, based on `e5dcc64fd4f9cfefc33f1a0917ea1fb2a5ca183b`.

## Pinned upstream contributions

| Change | PR | Integrated head |
| --- | --- | --- |
| Larger keyboard | [3506](https://github.com/crosspoint-reader/crosspoint-reader/pull/3506) | `5a07daadfcb76d9c38097cb6f7a4e8bb0e03bbf1` |
| Always Next taps | [3513](https://github.com/crosspoint-reader/crosspoint-reader/pull/3513) | `f41d313b9c21ad71806b5d70f26ed6aef66d9db7` |
| Indexed Library | [3366](https://github.com/crosspoint-reader/crosspoint-reader/pull/3366) | `ad949bdd2c43d85fe53d81e08bb73a7874b1c25f` |

The selected changes were applied relative to each PR's merge base with
`origin/develop`, preserving existing local search changes. This is not a full
update to develop. The keyboard requires FreeInk SDK
`c881d219b05520d42ff93c20c2bf3a4c401c64fe`; that SDK revision also contains display
and touch changes. Its host UI suite passed, but physical display/touch behavior
has not been verified.

Human authors verified from Git commits: Justin Mitchell
`justin@jmitch.com` (keyboard); piJoe `git@r3l.dev` (Always Next);
Uri Tauber `uritaube@gmail.com`, Aurelien Sorce / oreglio
`aurelien.sorce@gmail.com`, Leopoldo Pla Sempere
`leopoldoplasempere@gmail.com`, and Justin Mitchell (Library integration).
Preserve applicable human co-author credit if this work is later committed.
No commit or remote publication has been made.

## Refresh behavior and integration fixes

Entering Library reconciles the SD card before displaying indexed views
(`src/activities/library/LibraryListActivity.cpp:83`). This is necessary because
external SD deletion does not update the firmware's existing index. Safely eject
the card after computer edits, reinsert it, and reopen Library. Recent entries
are pruned as part of entry. The four tabs are Recent, Added, Title, and Author;
indexed views support search and ascending/descending order.

A failed directory open now aborts reconciliation and retains the previous index
(`lib/LibraryIndex/LibraryBuilder.cpp:430`). Library displays a rebuild error.
Deleting the last book correctly installs an empty index and counts all removals.
Changed EPUB metadata is parsed from the package rather than a stale path-keyed
reader cache (`lib/Epub/Epub.cpp:596`). Unchanged size/mtime entries reuse indexed
metadata; a same-size replacement that preserves its timestamp may require a
manual index reset to force fresh metadata.

Library input dispatch owns one render lock across index seeks, tab changes,
filter/group maps, and touch callbacks
(`src/activities/library/LibraryListActivity.cpp:100`). Activity-result callbacks
lock independently because the manager invokes them unlocked. Changed row maps
invalidate old touch routing. Recent removal keeps its button-release event, and
degraded sorting retains touch access to the tabs.

## Resource tradeoffs

No periodic scanner or background task was added. Entry scans still enumerate
the card and write staging files; unchanged metadata reuse avoids repeated EPUB
parsing, not all SD I/O. Opening Library can therefore take longer on large cards.

The imported builder caps the index at 4,096 books and scans at most five folder
levels below the root. It streams records to SD instead of retaining book strings
for the entire collection. Reconciliation uses a checked, temporary 16-byte
record per prior book (up to 64 KiB); sorting uses a separate 14-byte key per book
(up to 56 KiB). Walk-only allocations are released before sorting. The temporary
arrays cannot reasonably fit on the task stack, and allocation failures preserve
the old index or degrade sorting. Search/group maps use checked 16-bit arrays
(up to 8 KiB each), released with the activity. Visible-row vectors are reserved
before filling. These are allocation bounds, not measured peak runtime heap.

The retained Browse Files search has separate limits documented in
[book-browser-search.md](book-browser-search.md).

## Verification and device checks

- 312 host tests passed, including external deletion/addition in every indexed
  sort order, deleting every book, unreadable root/subfolder preservation, retry,
  metadata freshness, index formats, and existing recursive search.
- FreeInkUI host suite: 200,168 checks, zero failures.
- Independent GPT-5.6 Sol reviews covered Library indexing/input synchronization
  and keyboard/Always Next integration. Confirmed findings were fixed and
  re-reviewed; no remaining blocking findings were reported.
- Formatting uses `./bin/clang-format-fix -g`. Because that wrapper includes
  deleted paths, the successful run used a temporary Git index excluding the
  deleted RecentBooks files; the real index was unchanged. Whitespace checks pass.
- The packaged verification directory records firmware build, static-analysis,
  image-inspection logs, source patch, pinned revisions, and checksums.

After flashing, check the version, type/edit/cancel search using the new keyboard,
enable **Settings → Controls → Touch Reader Controls → Tap - Always Next**, and
test both outer page zones plus the center reader menu. Test Library search,
tabs, sort toggles, grouping and scrolling in all four orientations. Power off
before removing the card, delete a nested book on the computer, safely eject it,
reinsert, boot and reopen Library; the deleted entry should disappear. Repeat
with an added book and with the last book removed. Check existing frontlight
brightness/warmth controls and repeated Library entry/exit with a large card.

Physical flashing, e-ink rendering, touch accuracy, real SD errors and peak heap
remain unverified. A diagnostic build and serial measurements should confirm
free heap above 50 KiB and stable largest free block over repeated operations.
