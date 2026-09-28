# Reading and library reliability (r11)

The intended image is **firmware-x4pro-epub-r11-final.bin**, version
`1.6.5rc02-x4pro-r11-6c83edd`, in `build/x4pro-epub-r11/`.
`build/FLASH-LATEST.md` points to it only after the release gates pass.
Use the web flasher's **Xteink X4 Pro -> Custom .bin** option.

## Navigation and saved position

Percentage jumps and backward jumps into an unfinished previous chapter schedule
the target, start the existing parser, and return from rendering without requesting
foreground pagination. The normal reader loop advances the build with a two-page
request under the existing free-heap/largest-block thresholds. Complete chapter
caches resolve immediately. No new background task is created.

This adapts [upstream PR #3605](https://github.com/crosspoint-reader/crosspoint-reader/pull/3605),
reviewed at `f4b875e878404e837f92ac21a72a9c5488402c4d`. The original human author is
**asterism (0137)**, `1546578+0137@users.noreply.github.com`. The adaptation removes
the full-chapter sentinel and dead full-build branch and integrates cancellation,
progress guards, and failure handling with the existing reader changes.

The two-page value is a requested pagination budget, not a hard wall-clock or page
output limit: the parser yields at its existing input/paragraph boundaries. HTML
inflation and parser startup remain synchronous. Host tests do not establish
physical page-turn speed or Kindle parity.

Automatic turning waits for a pending jump and starts a fresh interval after it
resolves. Allocation, startup, or background-build failure shows an indexing error
and stops automatic retries until explicit navigation. Deferrable work takes the
existing nonblocking render lock and yields when heap or rendering is unavailable.
The reader saves progress only after a target page successfully reaches the display.

The bounded three-entry link history now retains page count and an optional exact
visible-text offset. Returning from a footnote or leaving the reader preserves its
origin through reflow. An explicit chapter, bookmark, percentage, search, or toolbar
jump retires that temporary origin so exit cannot overwrite the new position.
A saved position in spine zero resumes instead of being treated as a first launch.
Exact percentage chapter boundaries select the following chapter.

## Author groups

Author identity uses the complete normalized token sequence. The existing 12-byte
record field contains a four-byte prefix and eight-byte fingerprint; the builder
compares the normalized source identities before sharing a canonical spelling.
A fingerprint collision fails the new index build and retains the prior index.
Authors such as Christopher Tolkien, Christopher Priest, and Christopher Paolini
therefore stay separate even though their old 12-byte keys matched.

All words participate in normalization, retaining the existing spelling rules for
initials, accents, and inverted names. Author directory scans compare record keys
instead of truncated display captions. Names-only Author and Series browsing and
bounded child lists from r10 remain in place. Fold/order revision 4 triggers one
Library rebuild; the 128-byte record format and 14-byte-per-book sort scratch stay
the same. Book progress and favorites are separate from this index.

## Persistence and bookmarks

Shared JSON persistence writes to `.new`, checks write/close and readback, retains
the prior primary as `.bak`, and installs the replacement. The transaction commits
only when backup cleanup succeeds. A retained backup is the committed copy used by
readers; a retry restores it before beginning another write. Failed partial writes,
readback, rename, rollback, or cleanup cannot report a successful replacement.
This protects software-visible transactions; FAT metadata and SD controller
power-loss durability still depend on the device.

The existing 50,000-byte persistence limit now applies symmetrically to writes and
reads. Oversized or incomplete JSON is rejected before replacing the prior file.
Reads reserve once and check each 128-byte HAL read and String append, avoiding the
SDK convenience reader's silent truncation and unchecked per-byte append path.

Bookmark additions, removals, and list deletions serialize the proposed edit before
changing the live list. Rename failure restores the prior name. Failures display a
translated error instead of a success notification. Bookmarking requires a rendered,
committed page with a known content offset. Changing page or failing a render
invalidates that offset until a successful page render supplies the new one.
Missing bookmark files are distinguished from unreadable or malformed existing
files. An unavailable cache is retried on an explicit edit; an unsuccessful retry
blocks the edit instead of replacing unreadable bookmarks with an empty list.

## Resource cost and validation

Navigation adds only bounded history metadata and scalar state. It allocates no
new framebuffer, per-page work list, or task. Library normalization temporarily
holds reserved strings only while indexing; group browsing reuses its existing
bounded map. These Library resources are released when the activity exits.

Atomic persistence uses a 128-byte local read/verify buffer, a shared transaction
mutex, and one checked `makeUniqueNoThrow<char[]>` allocation for variable-length
suffix paths. A fixed path buffer would either truncate valid SD paths or retain
its maximum size permanently. JSON serialization reuses the existing full String;
there is no second full-file readback buffer. Existing bookmark vectors and a
single proposed entry are reused without cloning the list. These costs occur
when loading or saving state, not as a new page-render operation. Existing STL
allocations and actual peak device heap remain outside the host proof.

The package includes full Release and LLVM22 AddressSanitizer/UndefinedBehaviorSanitizer
runs with matching test registries, the X4 Pro release build, scoped static analysis,
image identity checks, source hashes, and review notes. New tests execute production
navigation methods, complete persistence implementations with ArduinoJson 7.4.2,
and production bookmark actions. They cover synthetic large chapters, cancellation,
resume formats, exact offsets, allocation failures, corrupt/short I/O, interrupted
transactions, rollback, and malformed bookmark documents. Parser integration tests
complement the reader's deterministic Section doubles.

After flashing, let Library rebuild and check authors with similar first names.
Resume an early chapter, follow a footnote, then jump elsewhere and reopen the book.
Try percentage and backward chapter jumps, then add, rename, and delete a bookmark
and reopen the book. Keep anti-aliasing off for the requested reading trial.
No recording is needed. This release changes no waveforms; r8 refresh safety and
r9's always-dark boot screen remain. Optical ghosting and physical SD failure
recovery are not measured by host tests.

## Next test-led work

Two separate existing limits remain: title ordering uses only a 12-byte folded
prefix, and bookmark filenames flatten slashes to underscores, so distinct SD
paths can share a legacy bookmark filename. Correcting the latter needs an
explicit migration strategy that preserves existing bookmarks; transaction safety
alone does not resolve path identity. These are follow-up candidates for the next
library/persistence iteration.
