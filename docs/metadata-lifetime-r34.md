# Temporary metadata lifetime r34

Based on develop `93e98bb78702e29868a16a13b80c40e6b36ccdff` and SDK
`111fdcc7f0176c3ee38391a160ee296bf492dbd8`, retaining r33 behavior and tests.
Upstream's new release-workflow tooling pin is integrated unchanged. Both local
PlatformIO entry points report 6.1.19. The GitHub release workflow itself has not
been run or published from this branch.

## Memory lifetime

On a cached-book CSS rebuild, Epub::load previously copied all five cached
metadata strings into a local object, then kept that object alive while parsing
CSS and reloading the cache. parseContentOpf overwrites all five output fields;
none requires the cached input. The output now starts empty and its scope ends
before CSS discovery/parsing. The cached values used by the reader still come
from the same book.bin reload.

For a newly indexed book, the parsed metadata's scope now ends after publication
and temporary-file cleanup. Its title, author, language and cover/text-reference
strings are released before CSS parsing and metadata reload, including when
external CSS is disabled. The streaming index builder and all failure policies
remain unchanged.

This removes an unnecessary metadata copy and shortens the lifetime of existing
string allocations. No new allocation, class field, resident cache, public API or
binary format is introduced. The explicit scopes use existing stack objects;
there is no new background or page-turn work. The benefit depends on string sizes
and capacities; no fixed heap saving, device latency or peak-heap figure is claimed.

## Validation scope

Four new lifetime tests compile the complete production load method with
instrumented collaborators. They use nonempty cached values and large parsed
strings, check that assembly receives all five exact fields, and count live
metadata objects during CSS parsing and reload. Every test fails on r33 and passes
with the new scopes, including failed OPF/CSS paths and CSS-disabled loads.
The existing 15 loader tests retain allocation, cleanup/retry and CSS/TOC policy
coverage. Real-code metadata/cache/parser tests remain in the full required suite.
These counts are ownership evidence from host collaborators, not device heap
measurements.

All prior and pinned upstream tests remain mandatory. The independent clean-
upstream reference remains the r29 run at ef08c3a. A recorded Git comparison
proves the only change through 93e98bb is .github/workflows/release.yml: reader
code, test tree and SDK pin are identical. The package verifies this relationship
instead of relabeling the earlier run as a new upstream test run.

## Device verification

Use the single final X4 Pro image identified in build/FLASH-LATEST.md, initially
with anti-aliasing off. Open a fresh copy of an EPUB, reopen it and verify title,
author, TOC and chapter navigation; repeat with embedded styles enabled/disabled.
Check a large or heavily styled book and preserve original progress caches.
No cache migration or manual recording is needed. Physical ghosting, SD timing,
peak heap/stack and power-loss behavior remain unmeasured.
