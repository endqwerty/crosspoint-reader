# Page prefetch headroom r50

All r49 improvements remain on develop `93e98bb` and SDK `111fdcc7`.

## Avoided idle work

The reader previously allowed idle page decoding with more than 24 KiB free heap
and a largest free block above 16 KiB. Retaining that page requires at least
80 KiB free and a 32 KiB largest block. Under memory pressure, this could read and
decode a page only to discard it, then mark the current page as already attempted.

A shared compile-time PagePrefetchPolicy now holds the existing retention floors.
The reader uses it before loading; the cache uses it after decoding and when
releasing a page under memory pressure. Existing public cache constants remain
aliases. Cache size limits, ownership and binary formats are unchanged. The helper
adds no state or allocation. It also avoids setting the attempted-page marker
while memory is insufficient, allowing one attempt later if memory recovers.

The post-decode check remains mandatory: the page itself consumes memory, and
other work can change available heap. This change does not reserve memory or
promise that every attempted page can be retained. It avoids starting when the
retention floors are already unmet. Foreground page loading remains unchanged.

The attempted-page check also precedes the prefetch eligibility heap queries.
After one attempt, later idle ticks retain the mandatory cache-release check but
skip redundant eligibility queries. The post-decode memory check remains fresh.

## Host evidence

The reader test compiles the actual production idle block and the shared policy.
A 100-tick fixture alternates low free heap and a fragmented heap, both above the
old idle floors. Baseline: one page read/decode attempt, zero retained pages.
Candidate: zero reads and zero retention attempts. Restoring sufficient memory
allows one successful prefetch. This is an operation count with platform doubles,
not a measured device time or allocation count.

Across 100 healthy-memory idle ticks, each heap-statistics query falls from 201
calls to 102: 100 cache-release checks plus one pre-load and one post-load check.
The same one page is read and retained. These are instrumented ESP query counts,
not measurements of allocator lock time or CPU latency.

A second regression models memory consumed during page decoding and verifies
that the post-load check rejects retention. Exact threshold boundaries, lock,
debounce, UI, active-parser and end-of-section tests remain. The real cache's
ownership, budget and heap-floor tests run in the full suite; they compile the
same policy, not a copied test predicate.

## Upstream and device check

Develop was fetched and remains `93e98bb`. Draft PR #3675 was rechecked; its image
prefetch work remains deferred. No draft changes are imported in this release.

Flash the single final X4 Pro image linked by build/FLASH-LATEST.md. With AA off,
pause briefly between page turns and confirm navigation remains normal. No
recordings or cache deletion are needed. Physical ghosting, device elapsed time
and peak heap remain unmeasured. No commits, pushes or PRs were created.
