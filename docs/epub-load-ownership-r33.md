# EPUB load ownership r33

Based on develop `ef08c3ad19787fecda7b8b4ab1ad1e1d17e10862` and SDK
`111fdcc7f0176c3ee38391a160ee296bf492dbd8`, retaining r32 behavior and tests.
The latest fetch still resolves to this develop revision. The four affected
object allocations are also present upstream. This stays within Epub::load;
no public header, cache format or architecture changes are introduced.

## Failure and ownership boundary

Epub::load used four bare-new allocation sites for BookMetadataCache and
CssParser. With exceptions disabled, failed object allocation can abort.
All four now use the existing makeUniqueNoThrow helper and check the result
before dereferencing. Load failures release both owners through a stack-only
ScopedCleanup callback. A subsequent attempt can start without retained metadata
handles or CSS rules from the failed load.

Repeated loads and the post-build metadata reload destroy the previous object
before allocating its replacement. The prior reset(new T(...)) expression
constructed the replacement first, temporarily retaining both objects and their
owned data. The existing release of metadata before CSS parsing remains intact.
Successful loads retain the same two owners for reading.

This replaces existing heap allocations rather than adding any. Their lifetimes
span load and subsequent reading, so stack locals would not be an alternative.
The cleanup guard adds only local bookkeeping and uses no std::function or heap
allocation. No class field, active-page work or resident cache is added. Host
owner counts verify non-overlap; they do not quantify device peak-heap savings.
Standard-library strings and parser internals retain their existing allocation
behavior; this is not a claim that every possible allocation in EPUB loading is
now fallible.

## Behavior preserved

Cached loading, CSS low-memory fallback, missing/partial/invalid CSS rebuilds,
section invalidation after changed styles, inline CSS support when external CSS
is disabled, EPUB 3 navigation with NCX fallback, reading without a TOC, and
best-effort temporary-file cleanup retain their existing policy. The r32 cache
publication and backup recovery remain unchanged. Failed reloads do not remove
published metadata, allowing a later retry to use it.

## Evidence and tests

Fifteen tests compile the complete production Epub::load method extracted by
CMake with checked method boundaries. Only its metadata/CSS/storage collaborators
are simulated; their serialization, CSS and file-operation behavior retain their
separate real-code suites. This tests loader orchestration without pretending to
exercise hardware or every parser allocation.

Tests inject failure at each of the four object-allocation sites, at every fatal
load/build stage, and after existing cache ownership. They verify cleanup and
successful retry, owner count peaks, CSS fallback/invalidation policy, cached and
fresh CSS-disabled loads, nav/NCX/no-TOC behavior, and temporary cleanup failure.
Three safe baseline tests fail on r32: checked metadata allocation, failed-stage
cleanup, and replacement ownership. All previous/upstream test names remain
mandatory. Clean-upstream evidence remains the r29 run at the identical revision.

## Device verification

Use the final X4 Pro BIN identified in build/FLASH-LATEST.md, initially with AA off.
Open a fresh copy of a book, reopen it, and navigate chapters. Check embedded
styles on/off and a large EPUB; retain original progress caches. Host tests do
not establish device peak heap, SD latency, physical ghosting or power-loss
behavior. No cache migration or manual recordings are required.
