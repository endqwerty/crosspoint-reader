# Author heading allocation r36

Based on develop `93e98bb78702e29868a16a13b80c40e6b36ccdff` and SDK
`111fdcc7f0176c3ee38391a160ee296bf492dbd8`, retaining r35 behavior and tests.
The latest fetch resolves to the same develop revision. Upstream still uses the
same last-space surname display rule. The current author-related open PRs concern
OPDS and download folders, not this offline heading formatting path.

## Refactor

LibraryListActivity::formatAuthorHeading previously copied through a conditional
expression and built two substrings plus concatenations for each displayed name.
The visible row output strings already exist and are reused. The formatter now
reserves only their final required size, copies the author, rotates the surname
segment to the front, removes the original separating space and inserts the comma
and space. No temporary name strings are constructed. Empty metadata still uses
the translated Unknown Author label. Names without a usable last space retain
their exact text. Sorting, author identity and grouping rules are unchanged.

The output can grow on first use or for a longer heading; this is the same retained
row string storage required to present the heading. Reserving the final size before
copying avoids an intermediate growth. A fixed stack buffer would add copying and
impose a new length limit; a separate static or class buffer would duplicate existing
storage. This change adds neither. UTF-8 bytes are moved as whole segments delimited
by ASCII space. The operation remains linear in name length.

## Host evidence

Five tests compile the actual production formatter through the existing UI fixture.
They check representative names, 1,365 exhaustive short combinations of ASCII,
spaces and UTF-8 tokens, aliased input/output, alternating long names with a warmed
buffer, and a cold output string. Native tests reuse the existing host allocation counter. ASan tests use its
public allocator hook because allocations inside the host libc++ dylib bypass
the operator-new counter under ASan. The cold-output test is a positive control:
it must see an allocation, so a blind counter fails instead of falsely accepting
the warmed-buffer test. Fixture and reference construction are outside the
measured scope.

On the baseline, the two allocation tests fail: 2,000 alternating headings make
9,000 allocations after warmup, and one cold heading makes five. The refactor
requires zero further allocations in that warm loop and one allocation for the
cold output. These are host standard-library results, not an ESP32 heap or timing
measurement. The other tests retain exact upstream display bytes, including
leading, repeated and trailing spaces, punctuation and multilingual names.

No new active-reading work, cache format, background task, class field or resident
buffer is added. The reduction removes temporary allocation churn during library
row construction; it does not establish physical SD or panel latency improvements.

Independent clean-upstream evidence remains the r29 run at ef08c3a. The retained
and rechecked equivalence record proves only the upstream release workflow changed
through 93e98bb; reader/test trees and SDK pin are identical. This is not represented
as a newly executed clean-upstream run.

## Device verification

Use the single final X4 Pro image named in build/FLASH-LATEST.md, initially with AA
off. Browse Authors with long and non-ASCII names, open a group and book, reverse
sort, and return to Titles and Series. Check Unknown Author and preserved reading
progress. No recordings or cache deletion are needed. Device behavior and peak
runtime heap remain unverified.
