# Critical unit-test audit after r7

The follow-up fixes are documented in [r8](reader-reliability-r8.md). This file
records the original audit findings; its r7 results remain historical evidence.

This pass changes tests, test fixtures and CI only. Production source and the
published `firmware-x4pro-epub-r7-final.bin` remain unchanged. The new fault
contracts stay enabled and fail for real defects; they are not disabled or
marked as expected successes. Results are recorded in the local
`build/test-audit-r7/RESULTS.md` and its native/sanitized CTest logs.

## Fix priorities from the tests

1. **Report parser allocation failures and reject incomplete cache builds.**
   Real TextBlock, text-arena and PageLine allocation failures can discard text
   while parsing reports Done/success. `ParsedText.cpp` logs and drops affected
   layout output, and `ChapterHtmlSlimParser::finishParse()` returns true.
   `Section::finalizeBuild()` also ignores the finish result, so repairing the
   parser alone would be incomplete. The tests compare visible words, require
   the fault to fire, and verify that a fresh fault-free parse restores the text.
2. **Check all page/cache writes and preserve committed files on failure.**
   Page/TextBlock scalar and style serialization ignores write errors. Section
   cache publication ignores footer/header writes and header seeks; replacing
   the old cache by delete-then-rename can lose it on a failed rename. A failed
   append-cursor restore also needs to stop further writes to that partial cache.
   Decoding must reject truncated style/coordinate fields and ruby lengths beyond
   the remaining file instead of accepting incomplete data or padding a string.
   Tests exercise actual serialization and extracted complete Section methods,
   and load valid committed fixtures through the real SectionPageReader.
3. **Keep a valid Library backup until live records or a replacement validate.**
   Library recovery trusts a valid header and deletes the backup before reading
   records. A malformed live record then aborts reconciliation, leaving neither
   a usable live index nor its valid backup. The regression accepts either a
   safe refusal retaining the backup or successful verified recovery.
4. **Make ActiveHigh BUSY failures explicit and prevent subsequent writes.**
   Command/ISR waits can return with the BUSY pin still active after timeout.
   When semaphore allocation fails, the refresh fallback omits delayed-assertion
   grace and can return before the pending waveform starts. These are ActiveHigh
   paths; the UC8179/UC8279 level-based path passes a simulated 45-second wait.
   An eventual error-return API must be tested through driver recovery as well
   as the bus. This audit does not change timeout policy or waveforms.
5. **Initialize unused bytes in cached link fields.** PageLink initializes only
   the first href byte; adding a short link leaves the tail unspecified, but
   serialization persists the whole fixed-size field. A separate deterministic
   regression explicitly seeds the live field tail and checks that those extra
   bytes are not written. Constructor initialization alone does not satisfy this
   stronger serializer contract.
   This is lower priority than the content-loss and error-propagation findings.

The first two items can affect displayed text and rebuildable section caches.
The Library finding concerns its index/recovery history. These tests do not
demonstrate deletion or corruption of original EPUB files.

## Production evidence

The findings above are grounded in these current source locations:

| Area | Source and lines | Mechanism |
| --- | --- | --- |
| Parser | `lib/Epub/Epub/ParsedText.cpp:1603-1610,1627-1633`; `lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp:2105-2127,2187-2190`; `lib/Epub/Epub/Section.cpp:639-652` | Failed layout allocations drop output, finalization reports success, and Section ignores its result. |
| Page data | `lib/Epub/Epub/Page.cpp:153-192`; `lib/Epub/Epub/blocks/TextBlock.cpp:333-365,437-463` | Scalar/style writes and trailing reads do not propagate failures. |
| Cache installation | `lib/Epub/Epub/Section.cpp:555-636,736-753` | Unchecked header/footer operations, delete-before-rename, and unchecked append-cursor restoration. |
| Library recovery | `lib/LibraryIndex/LibraryBuilder.cpp:145-179`; `lib/LibraryIndex/LibraryIndexFile.cpp:140-151` | Backup removal precedes validation of live records. |
| Display waits | `freeink-sdk/libs/display/FreeInkDisplay/src/bus/EpdBus.cpp:232-242,352-354,382-390` | ActiveHigh timeout has no failure result; the semaphore-allocation fallback lacks refresh assertion grace. |
| Link fields | `lib/Epub/Epub/PageLink.h:17`; `lib/Epub/Epub/Page.h:111-113`; `lib/Epub/Epub/Page.cpp:182,313` | Unused href bytes are unspecified, persisted, then partly normalized on read. |

## Why the added coverage matters

- Parser tests now compile the vendored Expat with `XML_GE=0` and
  `XML_CONTEXT_BYTES=1024`, matching firmware, and use the actual HTML entity
  table. Full-file and incremental cases cover malformed XML/UTF-8, read errors,
  codepoints split across reads, cancellation/destruction and file release.
- Scoped parser fault injection fails one checked nothrow allocation of the
  selected kind/size on the current thread. GTest, STL and Expat allocations are
  unaffected, so test infrastructure failure cannot masquerade as a parser bug.
- Storage fixtures can persist a partial write, fail a seek, and fail both
  installation and rollback renames. Passing tests verify later retry and the
  integrity of previously committed data, not just a returned boolean.
- The new BUSY harness compiles the complete SDK bus with virtual GPIO edges,
  interrupt modes and semaphore tokens. Earlier driver tests counted calls to
  a fake bus and could not exercise this implementation.
- `CROSSPOINT_TEST_SANITIZERS=ON` adds AddressSanitizer and
  UndefinedBehaviorSanitizer to host compilation/linking, including the
  refresh-sequence subprocesses. CI runs native and sanitized configurations;
  its CTest run has bounded parallelism and per-test timeouts.

## Coverage boundaries

Host tests do not model peak ESP32 heap headroom, all STL/throwing-new failures,
flash-cache-disabled ISR execution, real FreeRTOS interleavings, power-loss
durability inside an SD controller, or optical ghosting. The bus fixture also
does not claim ESP32-width millisecond-counter rollover coverage. Section tests
use supplied parser metadata for its persistence methods; they do not yet link
the complete parser-to-Section build lifecycle into one integration fixture.

The next test additions should accompany fixes: propagate a failed final page
through Section without installing its cache, verify that a reported BUSY error
stops driver writes, and test recovery across every cache-install step. Additional
corrupt-byte corpus testing and host allocation sweeps should follow these
deterministic contracts rather than replace them.

Reproduction commands and fixture details are in `test/README`,
`test/parser_failure/README.md`, `test/section_persistence/README.md` and
`test/epd_bus/README.md`. The local audit report records actual counts and failures.
No manual recording or new flash image is needed for this test-only pass.
