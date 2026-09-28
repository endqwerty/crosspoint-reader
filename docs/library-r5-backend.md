# Library r5 backend

The Library remains an SD-backed index with a fixed 4,096-book ceiling. It does
not load a complete collection of book objects or cover images into RAM.
Titles, authors and series come from EPUB package metadata; filenames remain
unchanged for opening books. Metadata parsing stops before manifest/layout work.

Series support adapts upstream PR [#3059](https://github.com/crosspoint-reader/crosspoint-reader/pull/3059),
reviewed head `8a51da298ca19a6b69290b05012970a080974c6b`, by
**Kenton Hamaluik <kenton@hamaluik.ca>**. Preserve that human co-author if committing
the adaptation. The implementation supports Calibre series metadata and EPUB 3
collections/refinements, including fractional positions and unnumbered books.

## Resource bounds

- The read-side index holds one file handle/header and reads individual fixed
  records from SD. Book state is a separate 16-byte record rather than a
  library-wide in-memory collection.
- Reconciliation uses 16 bytes per prior entry, then releases that array, the
  8 KiB duplicate-key buffer, and walk scratch before sorting.
- Title/author key arrays are phase-local: 14 bytes per book, at most 57,344
  bytes. The series key array uses 24 bytes only for books that name a series,
  at most 98,304 bytes. Standalone-only libraries allocate no series maps/keys.
- At the 4,096-book limit with every book in a series, the explicitly counted
  simultaneous sort arrays are 147,456 bytes: series keys, three u16 series
  maps, title order, arrival order and resolved first-seen values. This is an
  array budget, **not a measured total-device peak**; file handles, existing
  firmware state, allocator overhead and temporary strings are additional.
  Allocation failure preserves the previously committed index.
- The metadata parser is allocated fallibly on the heap because its bounded
  collection/refinement tables exceed the small task-stack budget. New series
  attribute/text values are limited to 255 bytes; oversized values are ignored
  rather than merged by truncated prefixes. General metadata text results are
  capped at 1,024 bytes. Expat itself may temporarily buffer a larger XML token;
  these bounds cover retained parser values, not the XML library's peak heap.
- The state writer uses exact-sized 48-byte path buffers (rather than three
  80-byte buffers), an explicit 16-byte wire record, and checked file operations.
  No extra heap buffer is needed for state encoding.
- The boot/session refresh policy uses two atomic 32-bit generations. A build
  captures `refreshToken()` before scanning and passes it to
  `reconciled(success, token)`. A mutation arriving during a successful build
  leaves refresh pending instead of being erased by completion.

## Migration and failure behavior

The Library index advances to version 4 to store full normalized series-name
identity before shortening display names. That avoids grouping unrelated series
that share a long prefix. A digest plus display-prefix key remains probabilistic;
this is not collision-proof identity for adversarial names.

Versions 2 (the prior local build) and 3 are accepted only for reconciliation.
Their arrival ranks survive while metadata is reparsed. EPUB reading-position,
bookmark and page-layout caches are independent of this index migration.
The schema is documented in [file-formats.md](file-formats.md).

The scan reports a partial index when an additional eligible book exceeds 4,096,
a directory exceeds the depth bound, or an unreadable/unrepresentable entry is
skipped. An exactly-full complete collection is not marked partial. Duplicate
tracking retains its independent 1,024-key bound and degraded flag.

Series allocation and staging read/seek failures stop installation; malformed
series references are not silently converted to standalone books. State-sidecar
writes validate staging and recover a valid backup before replacing a corrupt
main record, so an installation failure does not destroy the only good copy.
Unchanged book state avoids writes. Favorites/status currently follow raw paths;
there is no automatic sidecar migration for external file renames.

## Verification

The focused host suites exercise actual LibraryBuilder, LibraryIndexFile,
LibraryFormat, LibraryText, LibraryBookState, LibrarySession and ContentOpfParser
code, using fault-injectable storage for library/state operations and real Expat
for package metadata. Coverage includes 4,096/4,097 books, deep paths, metadata
reuse, long-name series separation, fractional ordering, v2/v3 arrival migration,
allocation failures and every read/seek failure position in a small series rebuild,
state corruption/short-write/close/rename/readback faults, unchanged state writes,
and invalidation during reconciliation.

These are logical and synthetic storage tests. They do not measure SD throughput,
physical display quality, or device-wide heap peaks. Firmware compilation and
integration tests are recorded with the final build package.
