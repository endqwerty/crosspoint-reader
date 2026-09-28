# Cached page read batching r41

All r40 features remain on CrossPoint develop `93e98bb` and SDK `111fdcc7`.
This release focuses on cached-page loading performance.

## Mechanism

TextBlock formerly read each word's four-byte annotation length separately, even
when all annotations were empty. A 64-byte stack buffer now batches these small
reads. Read-ahead never exceeds the minimum bytes still required by the remaining
strings. Payloads consume buffered bytes first, then read their remainder directly.
Nothing is read twice; no seek-back or speculative read into the following style
fields is needed. Lengths use memcpy into aligned variables.

The existing per-word wire format, word arena, cumulative annotation byte budget,
lazy annotation vector and HAL storage locking remain. No new heap allocation,
resident cache, public API, cache version or renderer behavior is introduced.
Nonempty strings are decoded directly into their existing destination slots.

## Performance evidence

The existing host page-turn fixture executes production section loading, page and
text decoding, real-font rendering and BW/grayscale composition. Its 18-line pages
contain five words per line, a rule, a footnote and a link.

| Complete cached page load | r40 read calls | r41 read calls | Bytes read | C++ allocations |
| --- | ---: | ---: | ---: | ---: |
| Prose | 193 | 121 | 2,616 | 59 |
| Annotated text | 198 | 126 | 2,870 | 64 |

Both retain five seeks and one open. Each removes 72 read requests and therefore
72 HAL read-lock acquisitions. Allocation requests/bytes and retained-page budget
remain identical. Already-prefetched page acquisition still performs zero reads.
These are HAL requests, not physical SD-sector reads or measured device latency.
Host microsecond timings are retained as context, not used to claim device speed.
The e-ink refresh sequence is unchanged; this does not claim reduced ghosting.

## Verification

Three added tests enforce the operation/allocation budgets and byte-exact block
round trips. Empty annotation runs cover 0, 1, 15, 16, 17, 31, 32, 33 and 127 words.
Variable annotation lengths cover 1 through 255 bytes at selected boundaries,
including split length prefixes, split payloads, mixed empty strings and multiple
buffer refills. Total bytes and final cursor must match the existing format.
Existing cached-page rendering tests still cover all four orientations and both
scenes; existing short-read and allocation-failure regressions remain required.

r40 library optimizations are retained. The unfinished author-validation work was
set aside and is not part of this release. No commits, pushes or PRs were created.

Use the single final X4 Pro BIN linked in build/FLASH-LATEST.md. Start with AA off;
open cached EPUBs, turn forward/back and cross chapters, checking text and saved
progress. Include a ruby-annotated book if available. Existing caches remain valid;
no cache deletion or recordings are needed. Device timing, peak heap and panel
behavior remain unmeasured.
