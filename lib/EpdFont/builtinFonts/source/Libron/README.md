# Libron released sources and built-in raster assets

## Source provenance

The four TTFs are unmodified files from the official **v0.30** desktop release:

- Repository: https://github.com/nicoverbruggen/libron
- Release: https://github.com/nicoverbruggen/libron/releases/tag/v0.30
- Tag commit: `15d764ead028c6979cdbf2b5be6da66200156d85`
- Published: `2026-10-06T17:27:35Z`
- Archive: https://github.com/nicoverbruggen/libron/releases/download/v0.30/Libron.zip
- Archive size: 479,403 bytes
- Archive SHA-256: `9441937b9dabd9cdffc9e0ed329685bb730f214fe79e5c8ae749b174c3e05e1d`

The downloaded archive matched the GitHub release asset's published SHA-256.
`LICENSE` and `COPYRIGHT` are unmodified files from the tag commit. `SHA256SUMS`
covers these files and all four TTFs. `provenance.json` records the source and
converter identities and the generation environment.

Each style also uses the repository's unmodified matching-style **Noto Sans
2.015** TTF as a lower-priority glyph source. These files were imported in reader
commit `bf7bffd506c997b3ce679d286b9b5f2b37f1d730`; their embedded copyright
identifies the [Noto Project](https://github.com/notofonts/latin-greek-cyrillic).
`FALLBACK-SHA256SUMS` pins the four TTFs and `../NotoSans/OFL.txt`; their identities
are also recorded in `provenance.json`.

## License and displayed name

The original TTFs and the derived `../../libron_*.h` font data are licensed under
SIL Open Font License 1.1, as supplied in `LICENSE`. The Noto Sans glyphs are
also SIL OFL 1.1, copyright 2022 The Noto Project Authors. Preserve `LICENSE`,
`COPYRIGHT` **and `../NotoSans/OFL.txt`** with redistributed combined font assets
and firmware source/packages. The Libron notices remain unmodified.

Copyright belongs to the Newsreader Project Authors (2020) and Nico Verbruggen
(Readerly and Libron, 2026). See `COPYRIGHT` for the exact upstream notice.

**Libron is a Reserved Font Name.** These built-in assets convert and subset the
released TTFs, so they are a Modified Version under the supplied OFL. Without
written permission from the copyright holder, use a different primary font name
in the firmware UI, such as **Reader Serif**. Internal file and C symbol names
retain `libron` to identify the source; they are not the displayed primary name.
The OFL FAQ specifically addresses converted firmware fonts:
https://openfontlicense.org/ofl-faq/#1-21

## Reproduction

Use Python 3.14.8, `fonttools==4.62.1`, `freetype-py==2.5.1`, and
`zopfli==0.4.1`. Generation used the macOS arm64 `freetype-py` wheel's FreeType
2.13.2. Rasterization depends on the FreeType version, so use that same version
when checking byte-identical output. The converter SHA-256 is recorded in
`provenance.json`.

From the repository root, verify source hashes, then generate only Libron:

```sh
(cd lib/EpdFont/builtinFonts/source/Libron && shasum -a 256 -c SHA256SUMS && shasum -a 256 -c FALLBACK-SHA256SUMS)
cd lib/EpdFont/scripts
for size in 12 14 16 18; do
  for style in Regular Italic Bold BoldItalic; do
    lower_style=$(printf '%s' "$style" | tr '[:upper:]' '[:lower:]')
    font_name="libron_${size}_${lower_style}"
    python fontconvert.py "$font_name" "$size" \
      "../builtinFonts/source/Libron/Libron-${style}.ttf" \
      "../builtinFonts/source/NotoSans/NotoSans-${style}.ttf" \
      --2bit --compress --pnum --zopfli > "../builtinFonts/${font_name}.h"
  done
done
(cd ../builtinFonts/source/Libron && shasum -a 256 -c GENERATED-SHA256SUMS)
python verify_compression.py ../builtinFonts/
```

`convert-builtin-fonts.sh` also includes this Libron conversion in the full
built-in font regeneration. No font outlines, hinting, line metrics, Unicode
intervals or converter code were edited. Native TTF hints, 150 dpi, proportional
numerals, 2-bit grayscale and Zopfli group compression use the existing reader
font pipeline. The converter takes the first font with a glyph for each codepoint: native
Libron glyphs always win, and matching-style Noto Sans fills only missing
glyphs. The combined assets export 1,076 glyphs for Regular/Bold and 1,075 for
Italic/BoldItalic in 13 groups, including the Cyrillic U+0400–04FF block,
additional punctuation/spacing/combining marks and U+FFFD. The converter's
existing Unicode intervals remain unchanged: Greek is disabled, and scripts
outside those intervals or absent from both sources remain unsupported.
Kerning is extracted only between codepoints served by the same source face;
cross-source pairs receive no kerning. There is no runtime fallback map.

The comprehensive verifier independently generates the combined compressed
and uncompressed fonts, Libron-only fonts and matching-style Noto Sans-only
fonts. It compares pixels and width/height/advance/bearings/data-length metrics
for every glyph, verifies source priority and union coverage, checks kerning
integer bounds, and performs two byte-identical compressed generations:

```sh
python lib/EpdFont/builtinFonts/source/Libron/verify-generation.py
```

Use `--write` to regenerate the assets and result/checksum files. To additionally
compare native glyphs against pre-fallback headers, supply `--baseline-dir`
pointing to a directory containing the 16 headers from commit `9525d9eb`.
The recorded generation used that exact baseline; its header hashes are
preserved per asset in `generation-results.json`.

## Generated data and verification

Each header emits the existing `static const` bitmap, glyph, interval, group,
kerning, ligature and `EpdFontData` objects. They use the same read-only storage
pattern as the existing built-in fonts. No new firmware allocation mechanism is
introduced; decompression continues through the existing built-in font path.

All 16 headers passed the existing compression verifier and two byte-identical
compressed generations. Every decompressed glyph's pixels and metrics matched
uncompressed conversion: **17,208 comparisons**. Font line metrics also remain unchanged. All **9,008 native glyphs**
matched both Libron-only conversion and the exact pre-fallback headers at
`9525d9eb`. All **8,200 added glyphs** matched matching-style Noto Sans-only
conversion. `generation-results.json` records commands, hashes, converter stderr,
per-asset counts and the original baseline hashes. `GENERATED-SHA256SUMS` checks
all generated headers. Shell syntax validation passed with `bash -n`.

Kerning class counts remain within the `uint8_t` representation: the maxima are
**251 left / 237 right** (18 pt BoldItalic has 251 left classes). This leaves
only four left-class IDs of headroom; adding further fallback sources or
changing source versions requires rechecking that limit. Sparse kerning row
offsets also fit `uint16_t`.

Total generated header text: **6,957,563 bytes**.
Total compressed bitmap payload: **597,330 bytes**.
Bitmap sizes exclude the read-only glyph and other metadata arrays; text-file
sizes are not firmware flash usage. The largest uncompressed group is
**49,961 bytes**, below the converter's
64 KiB cap. Actual linked flash/RAM sizes require the parent's target build;
no firmware build or physical-device checks were run here. The largest group
increased from 28,284 to 49,961 bytes (+21,677 bytes), so the existing temporary
page-decompression/hot-group buffer can request more memory. This is an asset
size change, not a new allocation path or a measured peak-heap result.

| Header | Glyphs (native + fallback) | Header text bytes | Compressed bitmap bytes | Largest uncompressed group bytes | Kerning classes (left/right) |
| --- | ---: | ---: | ---: | ---: | ---: |
| libron_12_regular.h | 563 + 513 | 329,589 | 24,863 | 20,261 | 206/218 |
| libron_12_italic.h | 563 + 512 | 389,582 | 29,774 | 21,786 | 245/233 |
| libron_12_bold.h | 563 + 513 | 353,895 | 26,538 | 22,428 | 215/220 |
| libron_12_bolditalic.h | 563 + 512 | 421,319 | 31,611 | 23,279 | 247/237 |
| libron_14_regular.h | 563 + 513 | 364,937 | 30,184 | 27,313 | 210/218 |
| libron_14_italic.h | 563 + 512 | 433,969 | 36,217 | 28,319 | 247/236 |
| libron_14_bold.h | 563 + 513 | 387,313 | 31,687 | 30,173 | 216/219 |
| libron_14_bolditalic.h | 563 + 512 | 467,609 | 38,753 | 31,283 | 248/237 |
| libron_16_regular.h | 563 + 513 | 399,208 | 34,628 | 34,821 | 213/219 |
| libron_16_italic.h | 563 + 512 | 477,266 | 42,555 | 36,677 | 245/235 |
| libron_16_bold.h | 563 + 513 | 421,937 | 36,711 | 38,711 | 216/220 |
| libron_16_bolditalic.h | 563 + 512 | 508,513 | 44,727 | 40,267 | 250/237 |
| libron_18_regular.h | 563 + 513 | 436,984 | 40,675 | 43,779 | 212/217 |
| libron_18_italic.h | 563 + 512 | 532,559 | 51,110 | 45,827 | 245/237 |
| libron_18_bold.h | 563 + 513 | 464,180 | 43,239 | 47,739 | 218/220 |
| libron_18_bolditalic.h | 563 + 512 | 568,703 | 54,058 | 49,961 | 251/237 |
