# Reader reliability (r8)

This release addresses the five groups exposed by the [r7 unit-test audit](unit-test-audit-r7.md).
It retains the RC02 source baseline and the existing offline Library/EPUB features.
The intended package is `firmware-x4pro-epub-r8-final.bin`, version
`1.6.5rc02-x4pro-r8-6c83edd`. Use `build/FLASH-LATEST.md` for the final verified artifact.

## Text and cache integrity

Checked layout allocation failures now stop the chapter parser. Page delivery
checks failures from the Section writer, and Section honors the final parser
result. An incomplete build cannot be installed as a complete cache or saved as
a partial cache during exit. A fresh build can retry a transient failure.

Page, text and image metadata serialization stops at the first failed operation.
Decoding rejects truncated fields, invalid booleans/alignment, unterminated fixed
strings and oversized text/path lengths. Ruby strings share a 65,535-byte budget
per text block, and each cached image path is limited to 4,096 bytes. The writer
applies the same bounds. Fixed-size links/footnotes emit zero padding rather than
unused in-memory bytes after their NUL terminators.

Section checks its header, footer, lookup tables, seeks and close result. Reading
an in-progress page must restore the append cursor; failure closes the writer and
prevents later publication. Installation retains the prior cache as `.bak` until
the new file is installed. A failed install rolls back; a failed rollback retains
the backup for next-open recovery. Interrupted cleanup conservatively restores
the older committed layout. Progress and bookmarks live outside these files.

Section cache version 47 invalidates older layouts, including existing caches
that may already have missing text. Opening a book may therefore rebuild its
layout once. It does not require deleting reading progress or the SD library.

## Library and display recovery

Library recovery now validates every prior record and path hash before treating a
backup as obsolete. Read or seek errors preserve both copies and defer rebuilding
for a later retry. Validation streams one record at a time and yields periodically.

ActiveHigh display waits report failure when BUSY remains asserted. The bus
latches that failure and suppresses subsequent SPI command/data writes. Driver
and display state must not certify the failed frame, pending cleanup, gray state
or shadow/baseline as synchronized. Recovery requires controller reset and
initialization followed by a successful full frame. The UC8179/UC8279 level-based
wait behavior and existing waveform tables remain unchanged.

HAL and renderer expose the existing driver commit result. An EPUB render keeps
its cleanup request and refresh cadence until the complete page succeeds,
including deferred and grayscale work. A placeholder image does not certify the
actual page. Failed output stops automatic page turns and returns before saving
new progress. Queued manual refresh stays available for a later submission.
Typed grayscale recovery preserves the canvas even in dual-buffer builds.

## Resource cost

- Parser failure state uses a byte-sized enum; Section adds an I/O failure flag.
  There is no new framebuffer, background task or search work while reading.
- Existing paragraph/page allocations now use checked nothrow ownership. STL
  strings/vectors still use the existing allocator; the tests do not establish
  safe behavior for every possible throwing allocation on an exhausted ESP32.
- The existing build LUT reserves a 32-entry batch (384 bytes of entry storage)
  before page callbacks. It exists only while building a section and still grows
  as needed. TOC anchor collection reserves a small eight-entry batch, then grows only for
  anchors in the current spine.
- Cache installation/recovery uses temporary backup-path strings only at build
  or open time. This follows existing HAL path APIs without adding permanent
  paths to every page or imposing a truncating fixed stack buffer.
- Canonical link padding uses a 256-byte `static constexpr` zero block in flash;
  no 256-byte local buffer or per-link allocation is added.
- Library validation reuses a 128-byte record and scalar counters. No whole-index
  allocation is introduced. The bus/driver adds three boolean fields and the
  renderer adds two. The reader uses a small stack transaction with references
  and prior cadence/flag values; no resident page buffer or heap allocation is
  added for commit handling. Object padding determines final size changes.

## Verification

The final package records native and AddressSanitizer/UndefinedBehaviorSanitizer
runs, existing SDK display regressions, the X4 Pro build, static analysis, source
hashes and independent review. Failure scenarios remain active tests.

New Section integration tests run the real parser, layout, page serialization,
page reads and complete production build/finalization/cleanup methods together.
They supply construction metadata rather than invoking EPUB inflation/startBuild;
that boundary remains covered by firmware compilation and normal-device use.
Storage fixtures exercise short writes, failed closes/seeks/renames, failed
rollback, recovery retries and truncated/corrupt headers. Display integration
runs the actual bus, SSD driver and facade with virtual GPIO/SPI/semaphores in
single- and dual-buffer modes. Existing synthetic refresh workloads also run.

These host checks do not measure optical ghosting, peak device heap, physical SD
power-loss durability or every FreeRTOS interleaving. With anti-aliasing off, open
a book, allow its first layout rebuild, turn pages both ways, open Library, and
try sleep/wake and manual refresh. Check that position/bookmarks survive reopen.
No recording is required. A debug build can check stable heap above 50 KiB after
repeated activity exits. The release preserves the current anti-aliasing setting.
