# Section cache publication failure tests

This target extracts and compiles complete production methods from
`lib/Epub/Epub/Section.cpp`: `onPageComplete`, `commitBuildFile`, and
`loadPageDuringBuild`, `getPageForVisibleTextOffset`, and
`getVisibleTextOffsetForPage`. It also extracts the version constants and the temporary
path method from `Section.h`. Configuration fails if an extraction boundary
moves. The fixture supplies only the owning fields, LUT entries, and parser
anchor output; it does not implement publication or read-cursor policy.

The linked `EpubPageTurnHost` supplies actual production `Page`, `TextBlock`, and
`SectionPageReader` functions with one consistent fault-injectable HAL. Healthy
complete and partial commits must publish decodable page bodies and correct
lookup metadata. Partial commits must exclude anchors on the unfinished page
and preserve the byte-progress watermark. Rejection controls cover a zero page
offset, an already-detected page serialization error, and failed destination
removal. Successful in-progress reads must restore the append cursor.

The prior-cache preservation tests first create a cache through the real commit
method and decode it with the real `SectionPageReader`, then attempt a rebuild
with I/O failures. Every injected write, seek, remove, and rename test asserts a
nonzero fault counter. The HAL's rename refuses an existing destination, as
SdFat's `FatFile::rename` does with `O_CREAT | O_EXCL` (bundled
`FatFile.cpp:973-980`). A failed rename itself changes neither file; deleting
the old cache is an action taken by the production method.

On the audited r7 production source, six enabled desired-contract tests fail:

- Exhausted footer writes still report a successful commit and replace the old
  cache (`Section.cpp:565-622`).
- A failed page-offset write still stamps the committed version (same method).
- Either header-patching seek can fail without aborting publication
  (`Section.cpp:612,621`).
- A failed final rename loses the previously readable cache
  (`Section.cpp:628-633`), contrary to the method's preservation contract at
  line 554.
- A failed append-cursor restore returns a page while leaving the active writer
  positioned before later cached pages (`Section.cpp:746-753`).

These regression assertions pass with the checked publication/recovery path.
They are not skipped, inverted, or registered with `WILL_FAIL`.

`SectionLookupTest.cpp` tests the production visible-offset lookup used when
restoring a reading position or resolving search results. On a 1,024-page
chapter, sequential 32-entry reads (upstream #3899) reduce the complete lookup
from 1,027 HAL read calls to 35, keeping the same 4,103 bytes, three seeks and
one open. The 128-byte stack buffer adds no owned heap storage. Tests cover first/last ties
across chunk boundaries, the maximum 65,535-page count, partial-cache limits,
in-memory build results with no SD access, and fallback to a longer partial.
Every negative read error, short read and failed seek returns no position;
truncated/overflowing LUTs are rejected before seeking into them. The HAL's
file handles are host stubs, so its zero C++ allocation count describes lookup
work, not the real HAL's existing file-handle allocation.

The reverse page-to-offset lookup is checked independently. Its healthy path
still uses four reads, three seeks, and 11 bytes regardless of chapter length.
Failed/short reads and failed seeks cannot produce a saved offset. The complete
LUT and selected entry use 64-bit bounds checks before the entry seek, including
when the requested entry survives a truncated table. Tests cover all four read
and three seek failures, positive short reads, zero and maximum offsets,
65,535-page tables, active-build reads without SD access, and valid partial
fallbacks. The truncated-table regression failed before the fix and remains an
ordinary enabled assertion.

This deliberately narrow harness does not link the full EPUB/parser/activity
construction graph, run `startBuild`/`suspendBuild`/`finalizeBuild`, or verify
`loadSectionFile` render-settings acceptance. Header render-setting bytes are
fixture placeholders; version, page count, bodies, LUTs, anchors, and partial
watermarks are written by the tested production methods. The lookup tests
supply their own version/count/offset header fields. Throwing STL OOM, concurrent SD access, FAT flush
failure, and interruption during rename remain outside this coverage. The HAL
models byte-level failures and return values, not sector-level SD durability.

```sh
cmake -S test -B build/host-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/host-tests --target EpubPageTurnTest SectionPersistenceTest -j4
ctest --test-dir build/host-tests --output-on-failure \
  -R 'PersistenceFault|SectionPersistence|SectionLookup'
```

The test-only vectors, parser placeholders, and fixed 64-slot allocation tracker
do not add firmware heap usage. Host results do not establish SD failure rates,
device peak heap, physical page-turn latency, or optical ghosting.
