# Personal Libron reader font

This firmware pins EPUB and dictionary text to the built-in Libron family,
including when settings on the SD card name Noto Sans, Noto Serif or an SD/vector
font. Loading normalizes the stored family and clears the SD font name once;
rendering also resolves Libron directly, without consulting the SD resolver.
Settings menus and text previews expose the same single family as **Reader Serif**.
Libron is a reserved font name; its OFL requires a different primary name for
converted firmware fonts. The released TTF files retain their original names. UI fonts retain
their existing roles. The unused Noto Serif and Noto Sans reader families are not
registered, so the linker can discard those flash assets and fit the firmware
partition. Noto Sans 18 remains registered for `UiSliderDialog` title text. Matching-style Noto Sans sources fill only codepoints absent from
Libron during font generation, including Cyrillic, punctuation and U+FFFD.
Native Libron glyphs take priority; measuring, rendering, prewarming and cache
identity all use the same combined built-in font without runtime redirection.

The four styles are rasterized at 12, 14, 16 and 18 points using the existing
2-bit, compressed built-in font pipeline. Available sizes and tie-breaking match
other built-ins. Unsupported saved sizes snap to the nearest size (ties choose
the smaller size); line-spacing choices use the built-in serif scale. Other
reader typography options keep their saved values. Libron font IDs are derived
from the generated assets by `build-font-ids.sh`, so old Noto/SD section layouts
use a different cache key and rebuild through the existing cache mechanism.

Source provenance, the release pin, checksums and OFL attribution are beside the
TTF files in `lib/EpdFont/builtinFonts/source/Libron/`. Regenerate with the Libron
block in `convert-builtin-fonts.sh`, then regenerate `src/fontIds.h` using
`build-font-ids.sh`.

Glyph bitmaps, intervals and metrics use the converter's flash-resident constant
arrays. Sixteen small `EpdFont` wrappers and four family wrappers use static
storage, replacing the previous Noto Serif reader wrappers. Registration uses
the same four map entries once during startup; the map is the renderer's existing
public font lookup interface. Unused Noto Sans 12/14/16 reader registrations
are removed; its 18-point UI registration remains. There is no new heap allocation in font selection, normalization or
glyph fallback; a fontstack resolves missing codepoints at generation time.
Glyph decompression uses the existing bounded shared font cache;
SD/vector reader font loading is bypassed. Font chooser storage reserves one
entry and reuses the existing settings lifetime.

Host regression coverage checks forced selection, saved-settings normalization,
size snapping, resolver bypass and cache identifiers. Firmware builds establish
static RAM/flash fit; serial heap, visual glyph quality and physical e-ink
readability remain unverified. On the device, an existing saved alternative font
must open as Libron, all four emphasis styles should display, and the font menus
should name Reader Serif after reboot.
