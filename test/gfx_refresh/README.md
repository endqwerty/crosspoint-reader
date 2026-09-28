# Page composition regression tests and host benchmark

`GfxRefreshTest` links the production renderer, `EpdFont` / `EpdFontFamily`,
font scan/prewarm cache, compressed-font decoder, `InflateReader`, bundled uzlib,
UTF-8 and bidi code. Its fixtures use the actual compressed Noto Serif 14 font in
all four styles. Hardware, memory-pressure callbacks and unused SD-font entry
points are stubbed; any unexpected SD-font call terminates the test instead of
returning fake glyphs. Font buffers use the SDK host allocator. These built-in
font fixtures keep the TrueType compile gate off; `test/ttf_epd_font` separately
links the real TrueType adapter and FreeType backend.

The reference is the previous 80-row traversal of each grayscale plane. Tests
compare its output byte-for-byte with one full-height traversal per plane and
check that both leave the monochrome framebuffer unchanged. Cases include all
four orientations, overlay and absolute encoding, no-prewarm and prewarmed fonts,
Latin ligatures, French, Polish, Vietnamese, Cyrillic, combining marks, clipping,
overlapping glyphs, white-on-black text, rotated text, superscript/subscript and
line/rectangle decoration primitives. These are renderer fixtures, not complete
EPUB pagination or `TextBlock` layout tests.

The text grayscale detector also uses production font lookup and rasterization
with small uncompressed 1-bit and 2-bit fixtures. All four orientations cover
normal/rotated text, combining marks, superscript/subscript and both ink colors.
The tests verify unchanged BW bytes and require empty overlay masks whenever the
detector returns false. Separate cases cover resolved styles, CJK fallback,
replacement glyphs, prewarm/status-bar exclusion and reset between pages. Any
2-bit font or white text conservatively keeps grayscale, even if clipping or
scaling happens to produce no intermediate shades. The detector adds two boolean
members and no buffers or heap allocation; it examines the resolved font once per
text draw, never scans pixels. These tests do not model complete reader activities.

Run the normal host suite, or just this executable:

```sh
cmake -S test -B build/host-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/host-tests --target GfxRefreshTest -j4
build/host-tests/gfx_refresh/GfxRefreshTest
```

Enable the optional benchmark:

```sh
cmake -S test -B build/host-tests -DCMAKE_BUILD_TYPE=Release \
  -DCROSSPOINT_BUILD_PAGE_BENCHMARK=ON
cmake --build build/host-tests --target GfxPageBenchmark -j4
build/host-tests/gfx_refresh/GfxPageBenchmark --samples 21
```

It prints CSV with median host composition microseconds, page traversals,
`drawText` calls, and the production decoder's bitmap-fetch and decompression-miss
counters. Every measured result must still match both reference planes and the
original BW bytes. It discards one warm-up pair and alternates the two approaches
to reduce ordering bias. The sample count defaults to 11. Timing covers only the
two plane compositions; base rendering, font prewarming, assertions and result
comparisons are outside the timed interval. The firmware instrumentation clock
is inert so per-glyph host clock reads do not inflate the benchmark.

No elapsed-time threshold is used in CI. The repeatable quantities are the
operation counts and exact bytes; timing depends on the host compiler, CPU and
load. In particular, band culling skips many glyph bitmap fetches before decoding,
so reducing twelve page traversals to two does **not** mean six times fewer
bitmap fetches or six times faster page turns.

This measures CPU-side composition only. The host hardware stub has no SPI
transfer time, e-paper BUSY duration, panel waveform, optical ghosting, ESP32
memory-pressure simulation or SD font I/O. Matching plane bytes protects this
optimization against changing the requested shades; it does not measure residual
ink. The benchmark allocates host result/timing buffers; none is firmware code.

## Glyph raster parity and CPU benchmark

`GlyphRasterParity` compares two separately linked renderer executables. The
reference replaces only `renderCharImpl` with the frozen implementation from the
verified r11 package (`R11GlyphRaster.inc`). The current executable uses the new
upstream Frame-based packed-glyph rasterizer, including its rotated-text path.
Both use the same current font lookup, shaping,
decompression, `drawPixel`, clipping and fixture code. Reference generation fails
if it cannot identify the production glyph dispatch; there is no firmware test
flag or alternate firmware rendering mode.

The test compares all 27,648,000 bytes from 192 scenarios, each containing the
complete BW framebuffer and both grayscale planes. The matrix covers four
orientations, three scenes, prewarm enabled/disabled, overlay/absolute encoding,
full/logically clipped pages, and 80-row/full-height bands. The scenes exercise
all four real compressed font styles, ligatures, combining marks, white ink,
overlap, explicit rotation, superscript/subscript and decoration primitives. An
additional uncompressed 1-bit glyph has a seven-pixel width, negative bearing,
transparent pixels, both ink colors and partial screen clipping. Separate
`GlyphBitmapTest` cases compare 23,064 guarded packed-bitmap results against a
per-pixel reference. Upstream cases cover normal and rotated glyph axes; the
retained normal-axis matrix also covers the previous tighter clip rectangle.
Empty and wholly clipped glyphs must leave every byte untouched.

Absolute-mode fixtures select that mode after `prepare()`, which resets rendering
to BW, and assert that it is actually active before plane composition. They clear
absolute planes to white and overlay planes to zero. The original framebuffer
must remain unchanged throughout either composition.

```sh
cmake -S test -B build/host-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/host-tests --target GfxGlyphReferenceProbe GfxGlyphProbe \
  GlyphBitmapTest GfxRefreshTest -j4
ctest --test-dir build/host-tests \
  -R 'GlyphRasterParity|GlyphBitmap|RealCompressedFonts' --output-on-failure
python3 test/gfx_refresh/compare_glyph_rasters.py \
  build/host-tests/gfx_refresh/GfxGlyphReferenceProbe \
  build/host-tests/gfx_refresh/GfxGlyphProbe 11 > glyph-raster-benchmark.csv
```

The final argument is the number of timed samples per scenario (1–101); the
CTest parity run uses one. Each scenario discards one warmup and reports the
median. BW redraw and the two-plane grayscale composition are timed separately.
Fixture preparation, prewarming and comparisons are outside those intervals.
With prewarming disabled, preparation clears decoded bitmap caches after its
initial draw, but lookup and other fixture setup have already run. `no-prewarm`
therefore does not mean a cold book open or cold font load. Early captured CSVs
called this same workload `cold-font`; interpret that label as `no-prewarm`.

The script runs the complete reference matrix followed by the current matrix;
these are paired scenarios, not interleaved trials. Host load, CPU state and
compiler layout can affect results. Keep slower cases in any assessment and
report prewarmed and no-prewarm workloads separately. No timing threshold is a
test gate. The r11 comparison isolates rasterization CPU work; it does not measure
page loading, parsing, SD-font I/O, ESP32 execution, panel latency or ghosting.
