# CrossInk as a feature source

[CrossInk](https://github.com/uxjulia/CrossInk) (remote `crossink`, fetch only)
is a single-maintainer fork of CrossPoint. It does not take pull requests, merges
upstream only occasionally and has replaced fonts, themes, network code and much
of the reader, so it is read for ideas and ported by hand, never merged or
rebased onto. Evaluated at `crossink/main` 9914146e (v1.6.1, 2026-10-04).

Port rules: re-implement against this tree's current upstream code, keep the
upstream interfaces, count RAM, and add the CrossInk author as `Co-Authored-By`
only when code was actually adapted.

## Ported

| Feature | Here | Notes |
| --- | --- | --- |
| Auto page turn in seconds | `AutoTurnIntervals.h`, reader menu and More panel | 5 to 120 s instead of 1/3/6/12 pages per minute. CrossInk also remembers the interval per book and has a stepper dialog; this keeps the existing popup. |
| Stable page numbers | `lib/Epub/Epub/ReferencePages.h`, status bar, Go to Page | CrossInk needs pre-processed EPUBs (`META-INF/crossink/optimizer-v1.json`); this derives pages from the uncompressed spine bytes already in `book.bin`, so it works on any book. See [fork-reader.md](fork-reader.md). |

## Already in upstream or this fork

Strikethrough, `<hr>` section breaks, Focus Reading, paragraph indentation
(CrossInk's "Force Paragraph Indents" is the same idea; upstream's setting
applies to every paragraph), tilt page turn, touch controls, a cover grid
theme, bookmarks, moving finished books to `/Read`. Upstream is porting
CrossInk's configurable controls itself (`official/feature/crossink-controls-port`);
take them from the next sync rather than from CrossInk.

## Declined

- Guide dots (2026-10-08): not wanted.
- Time left, reading stats, stats sleep screen and two-device sync, bookmarks
  and clips/highlights additions (2026-10-08): not wanted.
- Marking books finished, the 99% finished prompt, Recent Books 3x3 grid: low
  value for the owner; revisit only on request.
- Minimal and Dashboard themes, Lexend/Bitter/Inter fonts (the fork enforces
  Libron, see [fork-libron-font.md](fork-libron-font.md)), dictionary, USB
  serial transfer, frontlight schedule, firmware flasher changes.
- Pinned favorite sleep image: possible later UI work, not done.
