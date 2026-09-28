# Book metadata cache regressions

Compile the complete production BookMetadataCache implementation and public
header, real serialization and buffered-file helpers, and UTF-8 code. An
in-memory HAL injects deterministic open/read/seek failures and counts calls.
The Zip fixture supplies chapter sizes to the real writer; it does not replace
the cache encoding. This suite does not test ZIP decompression or real SD timing.

The r15 loader accepted a truncated metadata string and set `isLoaded()` true;
the initial reproduction and r15/r16 operation-count comparison are preserved in
the release evidence. Tests cover writer round trips, every truncation boundary,
header/LUT/string corruption, read/seek faults, repeated failed loads, table and
buffer allocation failure, on-demand entry checks, UTF-8 caption truncation,
empty spines, absent TOCs and invalid caller indices. A 1,000-chapter fixture
bounds load I/O and verifies zero I/O for repeated cumulative-size lookups.

The optional 4 KiB read buffer may fall back to unbuffered reads. The chapter-size
array remains four bytes per chapter and allocation failure must return false.
TOC entries are checked on demand; load does not scan every caption in advance.

Entry faults must return no partial strings or destination while retaining the
validated book counts and sizes. The contract test compiles the production Epub
count/progress accessors and reader EOF predicate against the full cache. Its
minimal fixture replaces unrelated activity state; it is not a full UI test.
