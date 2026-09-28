# EPUB parser failure audit

The initial `ChapterHtmlSlimParserTest` audit ran 35 cases: its original
10 tests plus 25 real-file parsing, failure, and lifecycle cases. The audit run
on the unchanged r7 production parser passed 26 and failed 9. The failures are
active desired-contract regressions; they were neither disabled nor marked
`WILL_FAIL`. The r8 parser fix preserves those assertions and adds nine cases,
for 44 tests. The focused Release and LLVM22 AddressSanitizer/UndefinedBehaviorSanitizer
runs pass all 44.

The target compiles the real ChapterHtmlSlimParser, ParsedText, TextBlock arena,
Page, CSS, UTF-8, bidi code, and HTML entity table. Its Expat is the vendored
`lib/expat` with flags read from `platformio.ini`, matching firmware. It
previously linked unspecified host Expat and replaced every named HTML entity
with a null lookup.

## Original r7 failure

Input:

```xml
<html><body><p>lostword</p><p>survivor</p></body></html>
```

Fail one checked allocation for TextBlock, its text arena, or PageLine. The
parser returns success and emits only `survivor`. The same happens for Focus
Reading TextBlock/arena, incremental PageLine allocation, and a two-cell table's
first TextBlock. Every case verifies exactly one injected failure; a fresh parse
without a fault emits both words.

Production evidence:

- `lib/Epub/Epub/ParsedText.cpp:1603-1610` and `1627-1633` return from a void line
  emitter when TextBlock/arena allocation fails. `layoutAndExtractLines()` still
  erases the corresponding input at `737-752`.
- `lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp:2187-2190` logs failed PageLine
  allocation and returns without reporting failure to the caller.
- `parseStep()` at `2065-2091` reports XML/read failure but has no layout-failure
  result. `finishParse()` at `2105-2127` unconditionally returns true.
- `lib/Epub/Epub/Section.cpp:641` ignores `finishParse()`'s return before committing
  the cache. A fix confined to that return value would therefore be incomplete.

These line references describe the r7 source audited before the fix.

Two additional regressions isolate the public lifecycle boundaries:

- `ParserFailureTest.CheckedPageLineFailureDuringStepIsReportedBeforeDone`: a
  checked allocation fails during `parseStep()`, which returned `Done`
  instead of `Error`.
- `ParserFailureTest.FinalPageAllocationFailureDoesNotReportSuccessfulFinish`:
  parse `<html><body>lostword</body></html>`, then inject failure only during
  `finishParse()`. It returned true after emitting an empty page.

## r8 propagation and lifecycle coverage

`ParsedText::layoutAndExtractLines()` and its line consumer now return `bool`.
A failed TextBlock/arena allocation or rejected line stops further extraction.
The paragraph has already moved words into that attempted line; callers must
discard the failed build and retry with a fresh parser, rather than retrying
the partially consumed paragraph in place.

`ChapterHtmlSlimParser` latches failure in a one-byte lifecycle enum, without
allocating a status object or replacement text. Checked Page, ParsedText, rule,
image, and PageLine construction failures enter the same path. During an Expat
callback, `markFailed()` stops XML parsing but leaves destruction to the outer
parse lifecycle. `parseStep()` returns `Error`, and `finishParse()` cannot later
publish a successful result. A page consumer can call `markFailed()` to reject a
failed cache write; no later page callback is issued. Successful finalization is
idempotent, failed or premature finalization remains unsuccessful, and a fresh
parser can retry from the original file position.

Additional production-backed cases cover rejection during parsing and on the
last page, cancellation, repeated finalization, empty chapters without null page
delivery, consumer rejection after the first line, and a 1,000-word paragraph
whose soft flush fails before its closing tag. A mixed paragraph/TOC/rule/table
chapter is first observed without faults, then each checked allocation is failed
in turn. Every injection must fire exactly once, reject the build, preserve the
failure state, avoid null callbacks, and close its input file. A clean retry must
reproduce every original word and page offset.

An image-layout case supplies synthetic PNG header bytes through the EPUB input
double, then executes the real header probe, dimension validation, and parser
image layout. Both 1-by-32,767 and 32,767-by-1 source images retain a one-pixel
minor axis after scaling to the viewport. This prevents valid thin images from
being rejected by the cache writer's positive-dimension checks. It does not
decode the image payload or test visual interpolation.

## Passing coverage

- One-shot and incremental parsing of 180 numbered paragraphs produce all 180
  words in order and identical page offsets.
- Missing input, empty input, truncated XML, mismatched tags, and invalid UTF-8
  are rejected without flushing an unfinished final page.
- Initial and mid-chapter read failures are injected below the real parser. The
  signed `int` result is `-1` for an SD I/O error, matching HalFile and both SdFat
  backends. Separate defensive cases return zero while unread bytes remain,
  exercising the parser's zero-stall check rather than claiming SD uses that
  error code. Both results are rejected; aborting preserves the already delivered
  page count and closes the file.
- Destruction of a paused parser closes the file without publishing another page.
- All three splits inside a four-byte UTF-8 codepoint at a 1,024-byte read boundary
  preserve the text.
- Valid XHTML named entities with its declared external DTD, numeric entities,
  XML entities, and the existing tolerated junk after `</html>` preserve text.
  The nonbreaking space remains the renderer's explicit continuation space token.

## Reproduction

From the checkout root, configure the focused harness with an already cached
GoogleTest source directory:

```sh
cmake -S test/parser_failure/harness -B build/parser-audit-tests \
  -DGTEST_SOURCE_DIR=/path/to/googletest-src -DCMAKE_BUILD_TYPE=Debug
cmake --build build/parser-audit-tests --target ChapterHtmlSlimParserTest
build/parser-audit-tests/chapter_html_slim_parser/ChapterHtmlSlimParserTest
```

The original r7 audit runner exited 1 because of the nine allocation regressions;
the r8 parser runner succeeds. Use
`--gtest_filter='CheckedOom/*'` for the seven end-to-end reproductions. All cases
remain registered in the ordinary root test target; no extra root CMake entry
is required.

## Injection and coverage limits

`ScopedAllocationFailure` replaces only the host executable's nothrow object and
array new overloads. It is scoped to the current thread, fails a selected matching
allocation, then lets subsequent allocations succeed. An observation-only scope
counts allocations for the exhaustive mixed-chapter loop. The original throwing allocator backs
successful allocations, so the normal matching delete remains valid. GTest
assertions execute after the fault scope; its allocations, STL growth, and Expat
malloc are not intentionally failed. Arena sizes are selected from their actual
single-word format (14 bytes, or 17 with Focus Reading). The storage double uses
real temporary files and scoped read errors; file counts check teardown.

This is test-only code and adds no firmware heap, stack, persistent data, or
active-reading work. Host renderer metrics are deterministic substitutes; image
decoding, hyphenation dictionaries, actual SD timing, framebuffer interaction,
and device heap fragmentation remain outside this fixture. The parser's formerly
bare Page/ParsedText allocations now use the existing checked allocation helper;
the mixed-chapter loop exercises these failures too. STL allocation failure and
Expat allocator failure are not injected. The enum adds no dynamic allocation;
its exact object-size effect depends on target padding. Existing containers and
their allocation budgets are unchanged.
The tests do not prove Section cache transaction safety or save/progress behavior.
