# Cold EPUB indexing I/O — r51

The large-book TOC index now reads the staged spine through a transient 512-byte
BufferedFileReader instead of making separate HAL reads for every record field.
It applies at the existing 400-spine-item threshold. The buffer is allocated once
with the existing nothrow reader, freed before TOC parsing, and falls back to
checked unbuffered reads on allocation failure. A 512-byte stack buffer would
exceed the project's 256-byte local-buffer limit. There is no persistent buffer,
cache-format change, or change to ordinary reading/prefetch policy.

Measured using production Epub::load, OPF/nav parsers and BookMetadataCache against
in-memory archive/storage doubles (CSS and ZIP decompression are not profiled):

| Chapters | Cold HAL reads before | After | Bytes read, unchanged | Warm HAL reads, unchanged |
| --- | ---: | ---: | ---: | ---: |
| 32 | 2,255 | 2,255 | 17,780 | 13 |
| 128 | 33,556 | 33,556 | 230,899 | 13 |
| 512 | 4,139 | 2,117 | 143,128 | 16 |
| 2,048 | 16,512 | 8,426 | 580,676 | 26 |

Seeks and writes are unchanged. Native replacement-new instrumentation observes
one additional allocation per indexed cold open, with unchanged overall fixture
peaks (23,901 and 57,693 bytes at 512 and 2,048 chapters). These are host-model
numbers, not total device heap, SD transactions, elapsed-time or ghosting results.
The mechanism reduces HAL mutex acquisitions/read calls for the sequential pass.

Tests compare buffered and OOM-fallback cache bytes, verify complete TOC mappings
after a recoverable short read, inject hard read errors at each refill, and retain
the allocation-failure matrix. The cold/warm fixture checks every spine/TOC mapping.

The smaller-book linear TOC search remains a separate bottleneck: the 128-chapter
fixture performs more reads than the indexed 512-chapter fixture. Changing that
policy needs its own memory/correctness comparison. Full ZIP/container discovery,
CSS parsing and initial page layout are outside this fixture's measurement scope.

Device check: with AA off, open a previously uncached long EPUB, exercise TOC
jumps, close and reopen it, then sleep/wake. Cache deletion and recordings are not
required. Physical timing, peak heap, ghosting and power-loss behavior remain
unmeasured. See the release package for source-bound validation results.
