# Notices and third-party material

This fork's code is MIT-licensed under [LICENSE](LICENSE): copyright 2025 CrossPoint
Reader organization (upstream) and 2026 Daniel Yang (changes in this fork). The notes
below cover material the fork adds or depends on beyond what upstream already ships.
Upstream's own bundled components (expat, miniz, uzlib, JSZip, the Noto, Ubuntu and
OpenDyslexic fonts, and others) keep the license files and headers they came with.

## Added by this fork

| Material | Where | License and obligation |
| --- | --- | --- |
| Libron font (v0.30), by Nico Verbruggen, based on Readerly and Newsreader | `lib/EpdFont/builtinFonts/source/Libron/`, generated `lib/EpdFont/builtinFonts/libron_*.h` | SIL Open Font License 1.1. `LICENSE` and `COPYRIGHT` there must stay with the font and any copy of the generated data. "Libron" is a Reserved Font Name, so the firmware shows the font as "Reader Serif" (see [the font policy](docs/fork-libron-font.md)). |
| Noto Sans glyphs merged into the built-in Libron data | `lib/EpdFont/builtinFonts/source/NotoSans/` | SIL Open Font License 1.1 (`OFL.txt` there). |
| Code adapted from unmerged or merged CrossPoint pull requests and from the [FreeInk SDK](https://github.com/Free-Ink/freeink-sdk) | across `src/` and `lib/`; authors are listed in the attribution section of each `docs/fork-*.md` and in `Co-Authored-By` commit trailers | MIT, the license of those projects. Authors stay credited in the commit that adapts their work. |
| Ideas and behavior ported from [CrossInk](https://github.com/uxjulia/CrossInk) (MIT, copyright 2025 Dave Allie, maintained by uxjulia) | see [CrossInk as a feature source](docs/fork-crossink.md) | Reimplemented against this tree's code rather than copied. Where a commit does adapt CrossInk code, its author is added as `Co-Authored-By`. |
| Patches to SdFat 2.3.1 (MIT) | `scripts/sdfat_patches/` | Applied at build time to a hash-pinned download; they are small diffs, not a copy of the library. |

## Dependencies fetched at build time

Not stored in this repository: PlatformIO packages, the FreeInk SDK (git submodule,
[`endqwerty/freeink-sdk`](https://github.com/endqwerty/freeink-sdk), MIT), and GoogleTest
for host tests. Each carries its own license.

## Not licensed by this repository

Xteink, CrossPoint and FreeInk names belong to their owners. This fork is not
affiliated with them. Books you read with it are yours and are not part of it.
