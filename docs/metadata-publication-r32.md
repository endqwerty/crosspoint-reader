# Metadata cache publication r32

Based on develop `ef08c3ad19787fecda7b8b4ab1ad1e1d17e10862` and SDK
`111fdcc7f0176c3ee38391a160ee296bf492dbd8`, retaining r31 behavior and tests.
A fresh fetch confirms the same develop revision. No rebase or upstream source
replacement is needed. This change stays within BookMetadataCache, preserving
its public API, streaming assembler and version-10 binary layout.

## Problem and mechanism

The previous builder truncated book.bin before assembly and ignored the final
close result. A failed rebuild could destroy a usable cache or report success
when SdFat's close-time sync failed. OPF and TOC staging closes were also unchecked.

The builder now streams into book.bin.new, checks flush and close, renames the
previous primary to book.bin.bak, installs the replacement, then removes the
backup to commit. A surviving backup is authoritative for reads. A later build
restores it before attempting another replacement. Failed rollback leaves the
backup available; failed pending-file cleanup leaves an ignored orphan that a
later build truncates. Readers never publish a pending file and do not mutate
files during recovery. Corrupt authoritative backups fail existing validation
rather than exposing an uncommitted replacement. Close failures latch a failed
staging pass until a fresh beginWrite.

This follows the backup/rename convention already used for persistence and library
publication. The whole-document AtomicFile helper is capped at 50 KB and needs
a complete buffer, so metadata keeps its existing streaming writer. No common
persistence API or cache format is changed. Metadata builds retain their existing
single-owner lifecycle; this is not a new concurrent-writer facility.

## Resources and limits

One checked temporary allocation holds three variable-length paths:
3 * (cache directory byte length + 13) bytes, freed at load/build return.
A fixed stack path array would impose a path bound or consume unnecessary stack;
a resident table would cost memory during reading. There are no new class fields
or allocations in chapter/page lookup. Existing 4 KB streaming buffers remain.
Backup publication temporarily needs space for both old and new metadata on SD.
No new complete-file buffer or eager TOC scan is added.

This is an I/O failure and interrupted-operation recovery improvement, not a
speed improvement. It does not prove FAT power-loss durability, physical SD
behavior or device peak heap/stack. Cache identity remains path-based; replacing
EPUB content at the same path still requires existing cache invalidation.

## Tests

Nine new tests cover failed OPF/TOC/final closes; every observed assembly read,
seek and write failure with a committed predecessor; rename, rollback, backup
cleanup and restore failures; interrupted file states; corrupt backups; path
allocation failure; first publication failure; failed cleanup/open and retry.
Prior no-predecessor fault tests remain and explicitly start without a primary.
The previous implementation fails the two safe baseline close/recovery tests.
All prior and pinned upstream test names remain mandatory, with no supersessions.
Clean-upstream test evidence is retained from r29 for the identical revision.

## Device verification

Use the one final X4 Pro image named in build/FLASH-LATEST.md, initially with AA
off. Copy a book to a new filename, open it to build metadata, reopen it and check
TOC/chapter navigation. Repeat with a large book; check Authors/Series, bookmarks,
normal page turns and sleep/wake. Preserve original book/progress caches.
No cache migration or intentional SD removal/power interruption is needed.
