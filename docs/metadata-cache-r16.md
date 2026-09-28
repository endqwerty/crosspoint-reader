# r16: checked book metadata and fewer load operations

r16 stays on CrossPoint develop `1d61100f90d2e7e32965c14301320d9989a7ab71` and
SDK `111fdcc7f0176c3ee38391a160ee296bf492dbd8`. The upstream baseline was rechecked
before work. This adapts the existing BookMetadataCache loader and keeps the
upstream serialized format and cumulative-size cache. No second cache format or
reader implementation is introduced.

## Reproduced problem and changes

The previous loader accepted a metadata string whose declared length exceeded
its remaining payload, then reported `isLoaded() == true`. Header fields, seeks
and entry reads also lacked success checks. The complete production module is now
covered by host tests using its own writer and a fault-injecting HAL.

- Validate header/count/offset arithmetic and available bytes before allocating.
- Bound metadata/path fields to 4 KiB, reject embedded NULs, and ensure fields stay
  within their declared record. Normal OPF display metadata is already capped at
  512 bytes. Longer TOC captions are safely shortened at a UTF-8 boundary while
  preserving their full destination; paths cannot be silently shortened.
- Validate spine LUT positions, cumulative sizes and TOC references while loading.
  TOC records are checked when requested; no eager scan of every TOC caption.
- Discard the handle, metadata, counts and chapter-size cache on a failed load.
  Failed reloads cannot leave the prior book's data visible. A failed individual
  entry read returns an entirely empty entry, retaining already validated counts
  and sizes so the reader cannot mistake an SD fault for the end of the book.
  Subsequent calls can retry through the same checked reader. Invalid caller
  indices also return an empty entry without changing loaded book state.
- Keep the same four bytes per chapter for cumulative sizes, using a single
  fallible allocation instead of a vector reserve that can abort on OOM. Temporarily
  reuse that array for LUT validation before replacing each offset with its size.

Book metadata remains version 10; section caches remain complete 49 / partial 233.
Normal existing caches, reading positions and bookmarks do not need a reset.
Caches that fail initial loading use the existing rebuild/error path. This work checks
read-side acceptance; it does not claim to audit all temporary-file writer paths
or every allocation elsewhere in the EPUB engine.

## Load optimization and resource cost

The cumulative-size load skips href strings without constructing one per chapter.
It uses the existing BufferedFileReader with one transient 4 KiB allocation,
released on return. If that allocation fails, the existing unbuffered fallback
still performs checked reads. A stack buffer was rejected because 4 KiB exceeds
the C3 stack budget; a permanent buffer would keep memory after loading finishes.

On the same synthetic 1,000-chapter fixture, the saved r15 implementation makes
4,014 HAL reads and 1 seek; r16 makes 23 reads and 7 seeks. Total read/seek calls:
4,015 to 30. Cumulative progress queries make zero reads/seeks in both versions.
This is an operation-count measurement, not a claim about physical SD latency,
page-turn timing or ghosting. The new method trades a temporary buffer and six
additional seeks for batched reads and no per-chapter href string allocation.

## Verification and follow-up

Require full native and LLVM22 ASan/UBSan registries, retained previous/upstream
tests, SDK checks, scoped static analysis and an X4 Pro firmware/image check before
publishing the BIN. The package records the actual results, source manifest and
original failure reproduction. A regression also combines the full cache with
production Epub count/progress accessors and the reader's EOF predicate; a read
fault must not change a valid chapter into EOF. No physical validation is claimed.

On device, open several existing books, a large omnibus and a book without a TOC;
check chapter selection, percent progress, resume after sleep and return to the
Library. Existing r15 features remain. No recordings are needed.

Remaining Library roadmap: on-demand book title/path details and stable selection
after a full index rebuild. Keep those lazy, use existing theme controls, and
measure restoration I/O before adding a path scan. Keep upstream develop as the
baseline and review overlapping upstream changes before new work.
