# Bounded first-open EPUB indexing (r23)

## Upstream foundation

Based on CrossPoint `develop` 4a6283db9c9692e059eaf2d57d5b7a36d85a2f4d,
verified against the remote branch on 2026-09-26, and FreeInk SDK
111fdcc7f0176c3ee38391a160ee296bf492dbd8. The r22 integration stays intact:
upstream main-loop/render-lock scheduling, reader toolbar/font policy, leading-word
Library sorting, and unreadable-book wake recovery. No remote branch is modified.

This applies the bounded sizing approach from open upstream PR #3733
(0ec5d6ad471b978e516eb4443d33e974128e1242), authored by
Tuan Q. Nguyen <tuan@tenor.vn>. Its heap checks alone cannot make throwing
containers safe on a fragmented heap. The local adaptation replaces those
indexing containers with checked, fallible chunks and adds cleanup on failure.
The chunk primitive and its original four tests come from open PR #3027
(1d069ae1c21433a4a3f4a0614bb8bca9d123288b); random-access iteration is added for
existing sort/lookup algorithms. Neither proposal is represented as merged upstream.
Their unrelated Section/anchor changes are not applied.

## Behavior and resource ownership

- OPF manifest and large-book TOC indexes use lazily allocated chunk directories.
  Chunk growth never copies existing entries, and failed allocations stop the
  pass instead of invoking throwing `new`. Metadata-only Library scanning never
  creates either index.
- Each directory has 256 pointers (1 KiB on the device). Entry chunks start at
  32 elements and grow to 256; maximum payload requests are 3 KiB for manifest
  entries and 4 KiB for TOC entries. These document-sized tables cannot live on
  the constrained stack. The two production template instantiations are shared
  across their respective call sites; no global pool or permanent buffer is added.
- `buildBookBin` allocates three checked arrays once per build and reuses them
  across chunks. The chunk budget leaves 32 KiB of reported free heap. The
  manifest/TOC guards reserve 16 KiB; these are admission guards, not a guarantee
  of actual device peak heap. Fragmentation can still require a clean refusal.
- Large books may need multiple TOC and ZIP-directory scans. Books fitting one
  chunk retain one batch ZIP scan; books below 400 spine items keep their
  individual size lookups. No new background work is added during active reading.
- Index storage is released after parsing. Size workspaces are released when the
  cache build returns. Fallible output-buffer allocation retains the existing
  unbuffered fallback. Failed builds remove incomplete `book.bin` output.
- More than 32,768 spine or TOC entries, or cumulative uncompressed chapter sizes
  beyond UINT32_MAX, are rejected rather than wrapping the existing cache fields.
- ZIP batch lookup accepts non-owning spans over the caller's arrays. ZIP trailer
  decoding uses `memcpy` for possibly unaligned fields, a scoped fallible scan
  buffer of at most 1 KiB, and checked reads before interpreting bytes.

The `book.bin` format stays at version 10. This change adds no cache migration,
setting, UI control, connectivity feature, or persistent reading allocation.
Normal cache-loading validation and the r22 Library migration remain unchanged.

## Evidence and limits

The synthetic fixture drives the real OPF, navigation, metadata-cache, UTF-8 and
bundled Expat code over simulated storage and ZIP-directory lookup. It tracks
`operator new` requests, not Expat/storage `malloc`, allocator overhead,
fragmentation behavior on silicon, or task stacks. Its constrained 5,000-chapter
case previously exceeded a 112,640-byte cap during `book.bin` construction;
the bounded build passes. Ordinary 300/400/2,000-chapter cases retain their scan
counts. A sparse TOC crossing chunk boundaries produces byte-identical output
to a single-chunk build.

Regression tests fail each nothrow allocation in turn, impose maximum allocation
sizes, check metadata-only laziness, and reject index/size overflow. Separate
ZIP tests compile the real ZIP reader and inflater with simulated HAL storage;
they exercise span matching, duplicates, output bounds, repeated chunks,
unaligned trailers and trailer I/O failure. Chunk tests cover capacity, nontrivial
values, sorting/lookup across boundaries, and allocation retry.

The release package contains the complete native and LLVM22 ASan/UBSan test
registries, firmware/image checks, static analysis, source snapshot and hashes.
All r22 test names remain required. Host tests do not establish physical ghosting,
SD latency, input timing, electrical recovery, or peak device heap. The synthetic
ZIP model is not a full end-to-end ZIP allocation measurement.

## Device verification

Use the single image identified by `build/FLASH-LATEST.md`, with anti-aliasing off
initially. Open an uncached large EPUB (a fresh filename creates a separate cache),
check chapter navigation, reopen it, and compare with an ordinary book. Check
Authors/Series drill-in, sleep/wake and bookmarks. No recordings are needed.
If serial diagnostics are available, inspect free heap during first open and
again after returning to Library; host caps are not substitutes for this check.
Keep original books and their progress caches when creating fresh test copies.
