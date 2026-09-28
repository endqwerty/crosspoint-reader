# Ruby rendering allocations r48

All r47 improvements remain on develop `93e98bb` and SDK `111fdcc7`.
This change reduces temporary allocations when rendering ruby annotations.

TextBlock previously copied each annotation string into its temporary RubyDrawInfo
table alongside the calculated position and direction. Rendering now reads the
same text directly from the TextBlock-owned rubyTexts vector. The table contains
only geometry. The existing vector remains the only temporary table; this change
adds no allocation or storage. The const TextBlock retains
string ownership throughout render, so no pointer is cached beyond its lifetime.
Measurement order, drawing order, grouping, directions and cache format are intact.

## Host measurements

A five-word line includes a three-word ruby group, short and long annotations,
bold and underline. In every orientation and BW/LSB/MSB mode, its warm render drops
from three allocation requests totaling 272 bytes to one totaling 40 bytes. The
copied long strings disappear and the geometry vector shrinks. These are host
allocation-request sizes, not measurements of ESP32 peak heap.

The complete annotated page fixture performs font-cache scanning, BW rendering and
both grayscale passes. Its 20 allocation requests total 800 bytes, down from
4,000 bytes. Short annotations in that fixture fit inline, so its allocation count
is unchanged. The ordinary prose page still makes zero rendering allocations.

Tests compare captured pre-change framebuffer fingerprints for 12 isolated line
renders and eight complete page renders, covering all four orientations. BW and
both grayscale planes match exactly; serialized TextBlock data is also unchanged.
The fixture uses real built-in font rendering. It does not measure panel ghosting,
SD-font heap usage, device elapsed time or device peak heap.

## Device check

Use the single final X4 Pro image linked by build/FLASH-LATEST.md. If available,
open an EPUB with ruby annotations and check their positioning. Ordinary prose
should appear unchanged. Start with AA off; no recordings or cache deletion are
needed. All prior library and page-loading improvements remain included.
No commits, pushes or PRs were created.
