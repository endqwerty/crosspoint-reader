# Section and parser persistence integration

This target executes the real ChapterHtmlSlimParser, ParsedText layout, Page
serialization and Section page reader. CMake extracts complete Section lifecycle
methods from production, with configure dependencies on both implementation and
header. The fixture supplies construction metadata and a temporary HTML file;
it does not execute EPUB inflation, CSS setup or startBuild().

Tests cover ordinary word round trips, parser/layout allocation failure,
page write failure, malformed HTML, final-page allocation failure, rejection of
failed partial saves, header truncation, each header read operation failing then
retrying, header write failure, invalid booleans and partial trailer overflow.
The allocation fault is restricted to a nothrow allocation in production; gtest
assertions run outside its scope. Each fixture removes only its own temporary
directory.

Spacing regressions exercise paragraphs, table cells and long-paragraph soft
flushes through serialization and reload. Both spacing settings invalidate the
cache when changed. Version 49 uses a 43-byte header and partial sentinel 233;
tests reject both earlier version 47 header layouts and sentinel 235 before
reading layout fields, and inject failure at all 14 header reads.

The separate section_persistence target injects deterministic short writes,
close/seek/rename failures and rollback failures using a destination-exclusive
rename model. This integration target uses stdio-backed files to connect parser
failure with cache publication. Together they verify logical error handling,
not SD sector atomicity, power-loss durability, target allocator exhaustion or
real flash/SD timing. Run both targets through the standard test CMake suite;
CROSSPOINT_TEST_SANITIZERS enables address and undefined-behavior checks.
