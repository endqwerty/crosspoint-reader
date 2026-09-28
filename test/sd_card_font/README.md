# SD font page-turn measurements

`SdCardFontTest` and `SdCardFontBenchmark` compile the production `lib/EpdFont/SdCardFont.cpp`. The host HAL serves a generated CPFONT v4 image, counts every open/seek/read and byte returned, and can inject failed opens/seeks and short metadata/bitmap reads. The nothrow array hook counts allocation attempts and requested bytes and can fail a selected allocation. These helpers add no firmware code or memory.

Run the regression tests with the normal host suite:

```sh
cmake -S test -B build/host-tests
cmake --build build/host-tests --target SdCardFontTest SdCardFontBenchmark
ctest --test-dir build/host-tests -R '^SdCardFontTest\.' --output-on-failure
build/host-tests/sd_card_font/SdCardFontBenchmark
```

To compare the old and current implementations and save JSON:

```sh
python3 test/sd_card_font/run_benchmark.py --output build/page-turn-font-benchmark.json
```

The comparison exports `SdCardFont.cpp` and `SdCardFont.h` from commit `e5dcc64fd4f9cfefc33f1a0917ea1fb2a5ca183b`, before the imported #3521 changes, to a temporary directory. It compiles that original implementation and the working-tree implementation with the same host compiler, fixture and scenarios. The old API has no complete-page flag; the current call passes `accumulate=false`, as the reader's complete-page prewarm does. No baseline algorithm is recreated in the benchmark. Override `--baseline-ref` if comparing another revision; the chosen commit and source hashes are recorded. `CXX` selects the compiler.

The fixture has 512 Hangul glyphs and a replacement glyph, with 32×32 monochrome bitmaps (128 bytes each), metrics and bitmap content distinct per style, and reverse bitmap order. Requests cover contiguous and scattered glyphs, single and four-style pages, same-page hits, idle prefetch followed by actual draw, and dense/sparse transitions. Every requested glyph, replacement glyph, metric and bitmap byte is checked after each measured phase. Font loading and integrity checks are outside the measured region. Kerning and ligatures are absent from this fixture.

Each scenario runs in seven fresh host processes by default. Operation/allocation counts and results must be identical across runs. The report retains median **host CPU prewarm time with an in-memory SD stub**, which is not ESP32 or panel latency. No SD speed, panel duration or optical ghosting is inferred. Free-heap and largest-block values are configured inputs to exercise policy branches, not a heap fragmentation simulator. Allocation bytes are cumulative requested bytes, not peak live heap or allocator overhead.

A run of the shipped source gives these deterministic results. Counts include the replacement glyph:

| Operation | Original | Current |
| --- | ---: | ---: |
| First 100-glyph page, reads / bytes | 202 / 14,544 | 202 / 14,544 |
| Second disjoint 100-glyph page prefetch, reads / bytes | 402 / 28,944 | 202 / 14,544 |
| Fourth disjoint 100-glyph page prefetch, reads / bytes | 802 / 57,744 | 202 / 14,544 |
| Fourth page nothrow array requests / requested bytes | 7 / 71,024 | 3 / 3,260 |
| Second page across four styles, reads / bytes | 1,608 / 115,776 | 808 / 58,176 |
| Foreground turn after successful idle prefetch, SD I/O | 0 | 0 |
| Warm prewarm nothrow array requests / requested bytes | 1 / 2,048 | 1 / 2,048 |

The mechanism is bounded complete-page replacement: earlier pages' glyphs stop joining every new glyph/bitmap read. Fitting buffers remain reusable. Warm prewarm still allocates the bounded codepoint scratch buffer, so it is not allocation-free.

There is a workload tradeoff. After a 400-glyph dense page and a disjoint 100-glyph page, the original cache retains 501 glyphs; later requests that are subsets of that union need no SD I/O. The current cache retains 101 glyphs and rebuilds later sparse pages for 202 reads / 14,544 bytes each. The benchmark deliberately reports both cases. Across its mixed sequence, original/current totals are 462,960/362,736 bytes, 6,430/5,038 reads, 15/19 opens and 258/272 seeks; the lower byte count does not establish a universal speedup.

Tests enforce exact I/O on full-page misses, zero I/O after prefetch, independent style data, scattered-read bounds, metadata-to-bitmap upgrades, the 40 KiB retention floor, fail-open/seek/read recovery, scratch-allocation failure preserving a resident page, and existing bitmap-growth/advance-cache recovery behavior. No human recording is needed to run them.
