# Find in Book

The reader's **Find in Book** command opens the existing keyboard, scans the
current EPUB for a literal phrase, and shows matching snippets with section
numbers. Selecting a result uses the reader's existing visible-text offset navigation. Back cancels
without moving the reading position. Results are transient; reopening search
starts a new scan.

## Reading resource contract

Search creates no worker, index, prefetch, timer, or polling path. During ordinary
reading it performs **zero search CPU or SD work and retains no search heap,
parser, query, buffers, or results**. The dormant bookkeeping is one nullable
atomic allocator-context pointer: 4 bytes on ESP32 and 8 bytes on the host.
There are no search fields in cached pages, sections, or the reader activity.

The dedicated activity owns the query and results. A chapter parser exists only
inside `scanChapter()` and is destroyed before the next activity loop. Its XML
allocations are released by `finish()`, including cancellation, malformed input,
and allocation failure. Results and UI rows are freed in `onExit()`; destruction
then releases the query's string capacity before the reader resumes. The reader
allocates the search activity before releasing reader caches. It retains the
current page and visible-text offset, then releases the section (including its
incremental parser, lookup tables and prefetched page), overlay, image cache,
and SD font caches under the render lock. The EPUB and image extractor remain
available. Cancel reloads the saved page through the existing section cache;
selecting a result uses the existing offset navigation.

Large fixed arrays are allocated with `makeUniqueNoThrow` while search is open,
so they neither consume the small task stack nor become permanent static RAM.
Results occupy at most 5,600 bytes and the chapter matcher at most 2,600 bytes in
the host size assertions. UI rows are a separate fixed array of 32 list items
and 32 short section labels, allocated after scanning finishes. Those figures
exclude the existing keyboard/UI objects and EPUB ZIP streaming buffers. During
each synchronous ZIP read, search borrows the existing framebuffer under the
render lock so the inflater can use it for its state and 32 KiB window. The
loan returns before parser finalization, result-row allocation, or rendering;
the display continues showing the previously submitted searching screen.

Expat has a 32 KiB ledger limit, including the search allocator's aligned block
headers. This bounds retained parser allocations; it is not a total device heap
measurement and excludes platform allocator overhead and transient `realloc`
copying. Expat's allocator API lacks a context argument, so create/parse calls
claim the nullable pointer atomically and release it immediately afterward.
Competing or nested search parser calls fail closed instead of borrowing another
parser's allocation budget. Each allocation records its owner for cleanup.

## Deliberate limits

| Limit | Value |
| --- | --- |
| Query | 64 UTF-8 bytes, nonempty and not only whitespace |
| Results | First 32 matches in spine order |
| Snippet context | Up to 12 codepoints before and after a match |
| Input chunk / cancellation checkpoint | 1 KiB |
| Uncompressed input per chapter | 4 MiB |
| Uncompressed input per search | 64 MiB |
| Spine items per search | 4,096 |
| XML nesting | 64 elements |
| Single markup token | 8 KiB |
| Element name | 128 bytes |
| Accounted Expat allocations | 32 KiB |

Search proceeds one chapter per activity loop and yields at input checkpoints.
Cancellation is checked during the scan; an in-progress storage read still has
to return. A held Back button is consumed before returning to the reader.
The display updates for the searching screen and completed results rather than
refreshing on every match. Partial results use the **Search results (partial)** header;
empty failures or incomplete scans show the corresponding message.

Matching decodes UTF-8 and HTML entities, collapses whitespace, joins text across
inline tags, and separates block boundaries. Offsets count decoded body-text
codepoints, retaining the reader's whitespace offsets rather than counting
bytes in the normalized query. Head, script, style, title, and ruby fallback
text are excluded. Internal DTD subsets are rejected and external entities are
never fetched. Malformed chapters roll back their own results while retaining
valid earlier chapters; bounded scans can return partial results.

Case matching covers ASCII, Latin-1 uppercase, and the basic Greek and Cyrillic
uppercase ranges. This is not full Unicode case folding or accent normalization.
Search does not compute CSS visibility or layout, so CSS-hidden body text can
match. Phrases do not cross spine-item boundaries. More than 32 matches require
a narrower query; there is no persistent result index or match highlighting.

## Verification

`test/epub_search` builds the actual search engine against the firmware's
`lib/expat` sources with `XML_GE=0` and `XML_CONTEXT_BYTES=1024`, and compiles the
actual search activity with UI/storage stubs.
The tests cover overlap and chunk boundaries, entities and UTF-8, reader offsets,
malformed XML, DTD rejection, limits, cancellation, repeated allocation release,
injected parser allocation failures, activity cleanup, result selection, and
partial-result layout. They run through the root host-test CMake project.
`test/reader_search_handoff` executes the production menu dispatch, cache-release
helper, launch and result callback against resource stubs, including launch OOM,
cancel/resume coordinates, invalid results and intentional result navigation.
The activity tests check that the framebuffer is loaned only during ZIP reads
under the render lock and has returned before UI updates, including read failure
and cancellation. These tests do not emulate a physical display or SD cache.

On a device, open Find in Book, cancel one scan, select a result from another,
and resume normal page turns. A debug build can compare free heap after repeated
search exits; no search state should accumulate. Host tests do not establish
real SD latency, available device heap, physical input behavior, or e-paper
appearance. No recording is required.

## Upstream source

The feature adapts the idea from [CrossPoint PR #3441](https://github.com/crosspoint-reader/crosspoint-reader/pull/3441),
which was open when reviewed. Its human author is AmirMohammad Cheraghali
(`QuercusCode`), with Git commit author address
`amirmohammadcheraghali@amirmohammads-macbook-air.home`. The search engine and
lifecycle were rewritten to avoid persistent reading instrumentation and bound
parser resources. Preserve the human author's `Co-Authored-By` attribution if
this adaptation is later committed.
