# Deferred library title fallback r39

Based on freshly fetched develop `93e98bb78702e29868a16a13b80c40e6b36ccdff`
and SDK `111fdcc7f0176c3ee38391a160ee296bf492dbd8`. All r38 changes are retained.

## Mechanism

LibraryBuilder::stageRecord constructed a filename-stem string before looking up
cached metadata or reading EPUB metadata. That temporary was overwritten when a
book supplied a title. A cached record with no embedded title constructed the
same stem a second time, although its folded filename key was already stored.

Title starts empty now. Fresh records construct the filename fallback only when
metadata supplied no title. Reused records retain the existing stored folded key
and do not materialize the fallback. The filename remains in its own path field;
no title is substituted into it. Missing, failed and disabled metadata retain their
existing fallback and metadata-status rules. New titles still replace stale metadata.

No allocation, resident buffer, field or SD operation is added. This removes
unnecessary string allocation/copy/free work from indexing and reconciliation.
It does not change the cache format, sorting policy, author inference, UI labels,
refresh cadence or active-reading path.

## Measurements and tests

The new host target includes the unchanged production builder translation unit to
exercise its private stageRecord function without exposing a firmware API. It uses
the real index reader, serialization and text helpers with simulated SD/EPUB data.
Five tests cover cached titles, cached missing titles, a positive allocation-counter
control, 16 fresh-record metadata/name combinations, and failed reused metadata.
The existing complete builder tests cover index publication, retries and rebuilds.

Two isolated 2,000-call staging workloads use a 180-character filename stem and
short cached metadata. Only stageRecord is counted; setup, fake storage flushes and
assertions are outside the interval. Before the change, normal and ASan builds
both observe 2,000 allocations with a cached title and 4,000 without one. The
candidate normal fixture observes zero in both cases. This is the isolated staging
step, not a claim of zero allocations for an entire library scan. The full sanitizer
run and source-bound release report record final verification.

Each workload still performs 6,000 reads and 6,000 seeks: 284,000 bytes with a title
and 274,000 without. Sixteen fresh staging-record fingerprints and semantic checks
are compared against the preserved baseline. A long-string positive control ensures
the counter is not blind to libc++ allocation calls. No device timing, fragmentation
or peak-heap measurement is inferred from these host counts.

## Upstream alignment

Develop is unchanged after a fresh fetch. PR #3651 remains open at
`8552ecaaa543dbaa56c83463671b3562a63c846e`; PR #3707 is closed without merge at
`1448be1f2a426e10ca0062213c4ee4e76326198a`. Their file-as/language sorting proposals
are not imported. The current project's filename fallback and stored sort keys
remain authoritative. No commit, push or PR is created.

The retained independent clean-upstream test run is r29 at ef08c3a. A checked Git
comparison through 93e98bb changes only the release workflow, with identical
reader/test trees and SDK pin. This is not represented as a new clean-upstream run.

## Device check

Flash the final X4 Pro image named in build/FLASH-LATEST.md, initially with AA off.
Refresh Library, inspect long filenames and books without embedded titles, and open
a book. Check author/series order and preserved reading progress. Existing caches
remain compatible. Recordings and cache deletion are unnecessary. Physical SD
latency, peak heap and panel behavior remain outside these host measurements.
