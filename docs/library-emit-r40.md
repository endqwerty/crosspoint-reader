# Bounded author reads during index output r40

Based on freshly fetched develop `93e98bb78702e29868a16a13b80c40e6b36ccdff`
and SDK `111fdcc7f0176c3ee38391a160ee296bf492dbd8`. All r39 changes are retained.

## Mechanism and scope

The record-offset and name-blob output passes read each staged book, then read
another full staged record to get its chosen author spelling. Often both reads
addressed the same book. A staged record is 840 bytes in the host fixture; only its
one-byte author length and 128-byte author buffer are needed from the second one.

Both passes now reuse the current record when it supplies its own author spelling.
When another book supplies it, a checked read fetches only those 129 adjacent bytes
into the existing scratch record. A compile-time layout assertion protects their
adjacency. Both offset sizing and actual output use the same chosen spelling.

No allocation, buffer, class field, format version or public API is added. The
existing two scratch records, storage abstraction, author voting and transactional
install remain. Every shortened read must complete in full; a short read prevents
publication and keeps the prior index. Reads stay byte-based, with no new unaligned
integer access. There is no active-reading or background work.

This affects initial index creation and rebuilds that publish a changed library.
An unchanged library already returns before output; that path retains r39's staging
allocation improvement but gains no additional I/O reduction from this change.

## Synthetic measurements

Two complete 512-book host builds use production builder/index/text code and fake
SD and EPUB transport. Metrics cover the full build before verification reads:

| Fixture | r39 reads / seeks | r40 reads / seeks | r39 bytes read | r40 bytes read |
| --- | ---: | ---: | ---: | ---: |
| Distinct authors | 5,122 / 5,121 | 4,098 / 4,097 | 1,928,194 | 1,068,034 |
| Shared author with spelling variants | 6,146 / 6,145 | 6,144 / 6,143 | 1,944,236 | 1,215,914 |

Index sizes and whole-index fingerprints match the baseline. Tests read back every
book's title, path, canonical author and original author spelling. Counts describe
HAL-level requests, not physical SD sectors or device elapsed time. The package
records both final native and sanitizer results. No speedup or peak-heap estimate
is inferred from host counts.

Four additional tests cover both large fixtures, four positive-short-read positions
across the two output passes (prior index retained and retry succeeds), and author
cleanup after truncation. Existing all-read/all-seek failure sweeps remain required.

## Author cleanup remains necessary

The considered shortcut of skipping cleanup for cached author names was rejected.
A valid source name can be truncated at the 128-byte storage limit, exposing a
trailing space. A real cached-metadata fixture reproduces this and verifies that
the existing cleanup still removes it. Reading fewer unused fields avoids changing
that behavior or introducing a second normalization policy.

## Upstream and device verification

Develop remains unchanged after the fresh fetch. Current author voting, sorting,
cache bytes and failure handling are preserved. No PR implementation or new sorting
policy is imported, and no commit, push or PR is created.

Independent clean-upstream evidence remains the r29 run at ef08c3a. Checked Git
comparison through 93e98bb changes only the release workflow, with identical reader
and test trees and SDK pin; this is not a new clean-upstream run.

Use the final X4 Pro image in build/FLASH-LATEST.md, initially with AA off. After
copying books onto SD, refresh Library, check author groups and titles, and open
books. Existing caches and progress remain compatible; cache deletion and recordings
are unnecessary. Physical SD timing, peak device heap and ghosting remain unverified.
