# Built-in Libron reader font

The fork's built-in reader fonts are Libron and Noto Sans. Libron takes the place
of upstream's built-in Noto Serif: settings store it as upstream's serif slot
(`NOTOSERIF = 0`), and `getReaderFontId()` returns the Libron font IDs for that
slot. Menus and the text preview show it as **Reader Serif**. Libron is a reserved
font name, and its OFL requires a different primary name for converted firmware
fonts. The released TTF files keep their original names.

SD-card fonts work as upstream ships them. Noto Serif (`NotoSerifExtended`),
Literata and other prebuilt `.cpfont` families come from the
[crosspoint-fonts releases](https://github.com/crosspoint-reader/crosspoint-fonts/releases);
copy a family folder to `/fonts/` on the card. The X4 Pro has external RAM, so
`.ttf` and `.otf` files also load directly, for example
`/fonts/Bookerly/Bookerly-Regular.ttf`. `.cpfont` files render faster. Bookerly is
proprietary and must not be committed here; the owner supplies their own copy.

## Why not three built-in families

On 2026-10-14 the X4 Pro image with Libron, Noto Serif and Noto Sans built in
linked 7,292,066 bytes against the 6,553,600-byte app partition. Measured from
the linked font data, Libron uses 1,309,158 bytes, Noto Serif 965,837 bytes and
Noto Sans 961,741 bytes at 12 to 18 pt. Libron and Noto Sans link at 6,325,546
bytes. Libron stays built in rather than moving to the card because it was the
owner's chosen font and built-in bitmaps are the fastest path; upstream's
prebuilt SD releases did not include Libron yet (upstream added it to the
download list in #3971 on 2026-10-14).

## Settings migration

From 2026-10-06 to 2026-10-14 the fork forced Libron and saved it as family 2.
Upstream reads a saved 2 as the legacy OpenDyslexic setting and moves it to an SD
font. The fork removes that branch: a saved 2 clamps to the serif slot, which is
Libron, and the settings file is saved again once. Saved SD font names are no
longer cleared. Libron font IDs did not change, so existing section caches stay
valid.

## Assets

The four styles are rasterized at 12, 14, 16 and 18 points with the existing 2-bit
compressed built-in font pipeline. Matching-style Noto Sans sources fill only
codepoints absent from Libron during font generation, including Cyrillic,
punctuation and U+FFFD. Native Libron glyphs take priority. Font IDs come from
the generated assets through `build-font-ids.sh`.

Source provenance, the release pin, checksums and OFL attribution are beside the
TTF files in `lib/EpdFont/builtinFonts/source/Libron/`. Regenerate with the Libron
block in `convert-builtin-fonts.sh`, then regenerate `src/fontIds.h` using
`build-font-ids.sh`.

Glyph bitmaps, intervals and metrics are flash-resident constant arrays. The
sixteen `EpdFont` wrappers and four family wrappers have static storage and
replace upstream's Noto Serif wrappers one for one. There is no new heap
allocation.

## Checks

`test/reader_font_sizes/ReaderFontPolicyTest.cpp` runs the production
`getReaderFontId()`, line-spacing and settings-loading code: the serif slot
renders Libron and the sans slot Noto Sans at every size, sizes snap, an installed
SD font wins and a missing one falls back, the old saved value migrates once, and
saved built-in and SD choices are kept. Line spacing follows upstream: the same
percentage of font size for every family, so each family wrapper is given its
real point size; four-option saves move up one slot once. `LibronCoverageTest` checks glyph
coverage. Firmware builds establish flash fit. On the device, unverified: each
built-in and an SD font render in a book, the font menus list Reader Serif, Noto
Sans and the installed SD families, and the previous Libron setting survives the
update.
