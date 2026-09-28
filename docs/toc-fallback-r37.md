# Clean TOC fallback r37

Based on develop `93e98bb78702e29868a16a13b80c40e6b36ccdff` and SDK
`111fdcc7f0176c3ee38391a160ee296bf492dbd8`. The latest fetch resolves to the same
revision. All r36 features and the completed heap-test calibration audit are retained.

## Confirmed defect

The actual loader prefers EPUB 3 nav, falls back to NCX on failure, and permits
reading without a TOC. Nav/NCX parsers stream entries into a shared staging file.
If XML allocation or parsing fails after emitting an entry, the old loader kept
that entry. NCX fallback appended to it, or the loader published a partial TOC when
no parser succeeded. The fault fixture reproduced four entries for a three-entry
book, and partial TOCs when neither source completed.

A failed parser attempt now closes its TOC pass and starts a fresh one. Starting
the pass resets the entry count along with the existing file truncation. Closing
or reopening failures stop publication. The same nav preference, NCX fallback,
and ability to read without a TOC are preserved. No cache-format or public-API
change is introduced; existing caches and reading positions remain compatible.
Already indexed books keep their caches. The fix applies when metadata is rebuilt.

## Resources

No resident buffer, class field, background task or reading-loop work is added.
Only a failed TOC attempt restarts the existing pass. Existing temporary writer
and large-book href-index allocations are released before they are recreated;
they retain the existing checked allocation and unbuffered-writer fallback paths.
Restarting can reread the spine and flush discarded entries on this error path.
Successful first indexing and active page reading do not perform that extra work.
This is a correctness fix, not a claimed panel or device-speed improvement.

## Tests

The new fixture compiles the production Epub::load, parseContentOpf, parseTocNavFile
and parseTocNcxFile methods unchanged, together with the real OPF/nav/NCX parsers,
BookMetadataCache, serialization and firmware Expat profile. Test-only redirection
of XML_ParserCreate supplies Expat's public memory suite; every observed allocation
point in each parser is refused in turn. ZIP transport and SD storage are simulated;
container discovery, CSS and cover decoding are outside this fixture's scope.

Eight tests cover OPF failure with retry over retained staging files, nav fallback,
nav without NCX, NCX-only failure, both sources failing, malformed XML, failures
closing/reopening the TOC staging file, and a 512-chapter book using the href index.
They check parser allocation release, file-handle release, exact TOC contents,
every spine mapping, safe publication and successful retry. Existing low-level
Expat, real-ZIP, storage-fault and complete reader/UI suites remain required.

The baseline allocation matrix observes 37 OPF allocation points, 38 nav points
and 34 NCX points for the three-chapter fixtures; the large nav fixture has 38.
The real-parser tests expose behavior missed by the older large-book helper,
which stops on nav failure instead of taking the loader's optional-TOC path.

## Related upstream work

[Ryan Jarvis (Cabalist), PR #2603](https://github.com/crosspoint-reader/crosspoint-reader/pull/2603)
also identifies partial-nav accumulation and resets TOC counters as part of a
broader sparse-nav selection proposal. Reviewed head:
`d49f9bdbcde0427e64ce9f02d79455aacb3e6d1b`. Credit is retained for the shared
restart/reset approach. r37 retains the selection rule currently on develop;
the unmerged sparse-nav heuristic and its extra state are not introduced.
Any future commit adapting that work should retain the human co-author trailer
recorded in the package attribution file. No commit or push is performed here.

Independent clean-upstream evidence remains the r29 run at ef08c3a. Rechecked Git
comparison proves only the release workflow changed through 93e98bb, with identical
reader/test trees and SDK pin. It is not represented as a new clean-upstream run.

## Device verification

Flash the one intended r37 X4 Pro image in build/FLASH-LATEST.md, initially with
AA off. Open a newly indexed EPUB, inspect its chapter list and jump to chapters.
Check ordinary page turns and preserved reading progress. No recordings or forced
on-device memory exhaustion are required. Host tests do not validate physical
SD power-loss, peak device heap, display latency or ghosting.
