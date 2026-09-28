# Reuse repeated series search results r42

All r41 page-loading and library improvements remain on develop `93e98bb`
and SDK `111fdcc7`. This change reduces repeated work during library search.

## Mechanism

After a title and author miss, each book used to read, fold and search its series
name again. The series table is immutable during the filter pass, and every book
with the same series ID refers to the same entry. Two local scalar variables now
retain the last consulted series ID and its match result for that pass.

A different consulted series replaces the remembered result. Books matching a
title or author still bypass series lookup, and standalone books do not match a
remembered series. Reading-state predicates still run separately for every text
candidate. Every new query/filter pass starts with no remembered series.

No heap allocation, member field, persistent cache, background task, format change
or alternate matching policy is introduced. Existing HAL-backed index methods and
fold/matchesQuery remain. This follows the existing group builder's practice of
reading an immutable series entry once per group.

## Measurement

A host fixture executes the production filterBooks method against instrumented
index/state transports. It contains 4,096 books in 64 runs of 64 books. Half the
series names match the query; every returned row is checked in both directions.

| Per search | r41 | r42 |
| --- | ---: | ---: |
| Book-record requests | 4,096 | 4,096 |
| Author requests | 4,096 | 4,096 |
| Series-reference requests | 4,096 | 4,096 |
| Series-name requests and folds | 4,096 | 64 |
| Matching books | 2,048 | 2,048 |

The test measures index API requests, not raw SD operations or elapsed device
time. Production readSeries reads one 64-byte entry via the existing readAt
method; the avoided requests remove that work and repeated name normalization.
The reduction is largest for long same-series runs. A library of all distinct
series does not gain this reduction. It is not a claim of a 98% faster search.
There is no added work during active reading or unfiltered library opening.

Three tests cover both large search directions, query changes, reading-state
filters, interleaved identities, standalone books, and independent title/author
matches. Existing library and page-loading regressions remain required.

## Device check

Use the one final X4 Pro image linked in build/FLASH-LATEST.md. Search a series
name, reverse the sort, change the query and combine it with a reading-state
filter. Results should remain correct. Existing caches remain compatible; no
recordings or cache deletion are needed. Actual device latency remains unmeasured.
No commits, pushes or PRs were created.
