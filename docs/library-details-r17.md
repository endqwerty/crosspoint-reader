# r17: lazy book details

Based on CrossPoint develop `1d61100f90d2e7e32965c14301320d9989a7ab71`
and FreeInk SDK `111fdcc7f0176c3ee38391a160ee296bf492dbd8`. The upstream
branch was rechecked before work. The file-as sorting PR 3651 and language-aware
article PR 3707 remain open; this change does not duplicate their sorting work.

## Library behavior

Hold a book row and choose **Book details**. Its full displayed title and absolute
SD path wrap across pages. Navigate with the normal previous/next buttons or
vertical swipes. Back returns to the same shelf, group, filter and selected book;
Open enters that book. Author/Series directory rows still enter the group.

The view addresses the long-filename use case in upstream issue 1170. It reuses
UITheme's header/status band and Back target, orientation-safe area, FreeInkApp,
theme body text and the SDK TextArea component. It introduces no new settings or
cache format, no metadata rescan and no book-content parsing.

The Library releases its index handle while the details child is open and
restores it through the existing child-return path. The text is prepared only
when Details is selected: one exact-size nothrow allocation, capped at 8 KiB and
freed with the activity. Oversized or embedded-NUL inputs fail visibly instead
of being silently shortened. A stack buffer would exceed the C3 stack budget;
per-line strings or a line table would retain unnecessary copies. Paging borrows
spans of the one buffer. The SDK host object follows the same allocation/lifecycle
conventions as existing dialogs. Nothing is retained or executed by active reading.

## Shared wrapping fix

The SDK TextArea walker previously measured only the first 220 bytes of a span
without limiting the line's actual length; drawing then copied only 220 bytes.
Wide viewports or zero-width text could make subsequent bytes unreachable.
Measurement also tested incomplete UTF-8 prefixes, allowing an over-wide glyph
to split across lines. The walker now measures complete codepoints and wraps at
the same 220-byte bound used by drawing. Oversized glyphs still make progress.
The change stays in the existing SDK component rather than adding another wrapper.

## Verification

The dedicated host suite compiles the full details activity and the real SDK text
layout, with a fake display and input. It extracts the production UITheme safe-area
helper, so all four orientation checks exercise that actual calculation. Tests
cover full title/path reachability, UTF-8 boundaries, page-edge no-op behavior,
button/swipe equivalence, Back/Open results, and buffer allocation/input failures.
Library tests exercise the production menu/callback code for lazy construction,
group/selection restoration, opening the resolved path and OOM recovery.
SDK tests cover the line-byte cap and codepoints wider than the viewport.

Release requires the complete previous test registry plus new tests in native
Release and LLVM22 ASan/UBSan, SDK runners, scoped static analysis and the X4 Pro
build/image check. Actual results and the source manifest belong in the release
package; this document alone does not establish that they passed.

On device: hold a long-named book, open Details, page to the end of its path, go
Back, then open from Details. Repeat in Authors/Series and after changing orientation.
No recordings are required. Host checks do not establish physical input, display
quality, SD latency or peak device heap. Existing r16 caches and saved positions
remain usable. Stable selection across a complete Library rebuild remains next.
