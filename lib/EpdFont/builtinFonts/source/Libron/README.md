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

## License and displayed name

The original TTFs and the derived `../../libron_*.h` font data are licensed under
SIL Open Font License 1.1, as supplied in `LICENSE`. Preserve that file and
`COPYRIGHT` with redistributed font assets and firmware source/packages.

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
(cd lib/EpdFont/builtinFonts/source/Libron && shasum -a 256 -c SHA256SUMS)
cd lib/EpdFont/scripts
for size in 12 14 16 18; do
  for style in Regular Italic Bold BoldItalic; do
    lower_style=$(printf '%s' "$style" | tr '[:upper:]' '[:lower:]')
    font_name="libron_${size}_${lower_style}"
    python fontconvert.py "$font_name" "$size" \
      "../builtinFonts/source/Libron/Libron-${style}.ttf" \
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
font pipeline. Unsupported codepoints are omitted by the converter; each style
exports 563 glyphs in 11 groups. These released fonts do not provide Cyrillic or
Greek coverage, and no fallback font was added.

## Generated data and verification

Each header emits the existing `static const` bitmap, glyph, interval, group,
kerning, ligature and `EpdFontData` objects. They use the same read-only storage
pattern as the existing built-in fonts. No new firmware allocation mechanism is
introduced; decompression continues through the existing built-in font path.

All 16 headers passed the existing compression verifier, were regenerated
byte-identically, and had every decompressed glyph bitmap compared byte-for-byte
with the same converter's uncompressed output (9,008 glyph comparisons).
`generation-results.json` records exact commands, output hashes, converter
stderr and per-asset sizes. `GENERATED-SHA256SUMS` checks all generated headers.
Shell syntax validation passed with `bash -n`.

Total generated header text: **4,769,442 bytes**. Total compressed bitmap
payload: **334,125 bytes**. Bitmap sizes exclude the read-only glyph and other
metadata arrays; text-file sizes are not firmware flash usage. The largest
uncompressed group is **28,284 bytes**. Actual linked flash/RAM sizes require the
parent's target build; no firmware build or physical-device checks were run here.

| Header | Header text bytes | Compressed bitmap bytes | Largest uncompressed group bytes |
| --- | ---: | ---: | ---: |
| libron_12_regular.h | 229,144 | 14,063 | 10,836 |
| libron_12_italic.h | 275,750 | 17,016 | 12,315 |
| libron_12_bold.h | 246,563 | 14,675 | 11,462 |
| libron_12_bolditalic.h | 300,407 | 17,745 | 12,814 |
| libron_14_regular.h | 248,068 | 16,800 | 14,578 |
| libron_14_italic.h | 300,339 | 20,335 | 15,872 |
| libron_14_bold.h | 264,972 | 17,463 | 15,042 |
| libron_14_bolditalic.h | 326,909 | 21,761 | 16,602 |
| libron_16_regular.h | 269,264 | 19,184 | 18,054 |
| libron_16_italic.h | 327,057 | 24,052 | 19,977 |
| libron_16_bold.h | 284,412 | 20,088 | 19,262 |
| libron_16_bolditalic.h | 349,342 | 24,808 | 21,082 |
| libron_18_regular.h | 292,455 | 22,919 | 24,201 |
| libron_18_italic.h | 360,120 | 29,072 | 26,557 |
| libron_18_bold.h | 307,859 | 23,627 | 25,844 |
| libron_18_bolditalic.h | 386,781 | 30,517 | 28,284 |
