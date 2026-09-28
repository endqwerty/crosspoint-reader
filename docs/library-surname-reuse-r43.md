# Reuse canonical surname keys r43

All r42 page-loading and library improvements remain on develop `93e98bb`
and SDK `111fdcc7`. This change reduces repeated work when building a new or
changed library index. An unchanged library already skips this output phase.

## Mechanism

The spelling vote already records which title-order book supplies each author's
canonical display name. Previously the surname-sort pass read and normalized
that canonical name again for every book. It now fills the existing sort-array
key only at canonical positions, then copies those keys to the other books.
All canonical keys are ready before copying, so sources may precede or follow
their destinations in title order. The second pass sets each book's own ordinal.

The canonical source maps to itself: initialization sets self mappings, and the
vote assigns one member of a run as the source for every member, including that
source. Unknown/initials-only authors and degraded spelling allocation keep the
existing self mappings. No new identity rules are introduced.

No allocation, scratch buffer, persistent cache or public API is added. Spelling
votes, surname normalization, tie breaks, formats and transactional installation
remain unchanged. Work servicing occurs once per computed or copied key, keeping
the original total scheduling-pause count rather than adding a pause per pass.

## Complete-build host measurements

The fixtures execute production builder/index/text logic against instrumented
SD and EPUB transports. Counts cover the complete build before result inspection.

| Fixture | r42 reads / seeks | r43 reads / seeks | r42 bytes | r43 bytes |
| --- | ---: | ---: | ---: | ---: |
| 512 distinct authors | 4,098 / 4,097 | 4,098 / 4,097 | 1,068,034 | 1,068,034 |
| 512 books sharing an author | 6,144 / 6,143 | 5,122 / 5,121 | 1,215,914 | 1,203,139 |
| 256 books / 32 interleaved authors | 3,010 / 3,009 | 2,562 / 2,561 | 596,322 | 592,290 |

Whole-index fingerprints remain identical in all three fixtures. The new mixed
fixture makes the chosen spelling's first occurrence early for half the authors
and late for the others; it checks every title, path, original/canonical author,
and ascending/descending author ordinal. Existing shared-surname collision,
degraded-allocation and storage-failure tests remain required.

These are HAL request counts, not physical SD sectors or elapsed device timing.
Normalizing the chosen surname once also avoids repeating its existing temporary
string work, but no whole-build allocation or peak-heap saving is claimed.
All-distinct-author libraries gain no I/O reduction. Active reading is unchanged.

## Device check

Use the one final X4 Pro image linked in build/FLASH-LATEST.md. After adding books,
refresh Library and check author order, grouped spelling and drill-down results.
Start with AA off for reading. Existing caches stay compatible; no recordings or
cache deletion are required. Device latency remains unmeasured.
No commits, pushes or PRs were created.
