# Metadata allocation r30

Based on develop `ef08c3ad19787fecda7b8b4ab1ad1e1d17e10862` and SDK
`111fdcc7f0176c3ee38391a160ee296bf492dbd8`, retaining r29 behavior and tests.
This refactors existing metadata scans under roadmap Phase 1 fragmentation work.
Upstream's long-title binary search remains unchanged. Public APIs, version 10
cache layout, active reading and retained Library features remain unchanged.

## Mechanism

BookMetadataCache::buildBookBin previously constructed string-owning SpineEntry
and TocEntry values on each loop iteration. Long fields allocated and freed new
storage for every record, repeatedly, across lookup-table construction, chapter
sizing, destination mapping and final output. The small-TOC href scan did likewise.

One record is now reused within each scan. readCacheString still resizes and
validates every field, including empty fields, and preserves all r29 failure
checks. Explicit scopes destroy the record before the next phase, especially
before the heap-based sizing-workspace budget is computed. No class member,
resident cache, background task or new allocation type is added. The existing
three transient 4 KiB streams and chunked workspace remain unchanged.

Reuse retains each string's largest capacity until that scan ends. Different
fields can peak on different records, so live heap can increase for such inputs;
this is not a universal peak-memory reduction. The existing 4 KiB field limits
bound decoded sizes, although std::string capacity includes implementation-defined
growth/overhead. Scopes prevent capacity from being retained across unrelated
phases. Original EPUB parsing and path normalization are not refactored here.

## Synthetic evidence

With long paths and contents titles, the instrumented native assembly records:

| Chapters | r29 observed allocations | r30 observed allocations | Modeled peak bytes in both |
| --- | ---: | ---: | ---: |
| 300 | 2,706 | 314 | 13,120 |
| 1,000 | 10,008 | 1,017 | 34,520 |
| 5,000 (two ZIP chunks) | 60,008 | 5,021 | 80,104 |

Counts exclude test storage and cover assembly, not total parsing. Host libc++
allocations can bypass replacement new under sanitizers; sanitizer counts are
not used to claim the above reduction. This is allocation evidence, not device
latency, measured fragmentation or physical peak-heap evidence.

Three new tests constrain allocation growth, compare complete 5,000-chapter
single-/multi-chunk output bytes, and alternate maximum-length/empty fields under
a 32 KiB assembly heap model. The latter checks every field, destination and
cumulative size and verifies all counted assembly storage is released on return.
All previous fault-injection, short-read, bounded-caption, malformed-cache,
failed-chunk and allocation-failure tests remain required. Clean-upstream test
results are retained from r29 for the identical develop revision.

## Device verification

Use the single final X4 Pro image named by `build/FLASH-LATEST.md`, with AA off
initially. Open fresh copies of an EPUB with long titles/paths and a large omnibus;
check first/middle/last contents destinations, reopen, saved position, bookmarks
and sleep/wake. Preserve original progress caches. No format migration or general
cache deletion is needed. Real SD latency, ghosting and device heap/stack peaks
remain unmeasured; see the release package for host and linker results.
