# Metadata staging r29

Based on develop `ef08c3ad19787fecda7b8b4ab1ad1e1d17e10862` and SDK
`111fdcc7f0176c3ee38391a160ee296bf492dbd8`, retaining r28 behavior and tests.
This refactors the existing BookMetadataCache build path for Phase 1 reliability.
Public APIs, cache version 10, on-card layout and ordinary page painting remain unchanged.

## Failure handling

Temporary spine and contents records previously used unchecked lengths, reads and
seeks during assembly. The shared decoder now validates length and file bounds,
checks each read and rejects invalid contents destinations and trailing staging
bytes. Staging file sizes use the HAL's 64-bit API and must fit the 32-bit format;
output offsets are checked using 64-bit arithmetic before serialization.

Buffered pass flush failures remain latched until a new build. When the optional
buffer-writer object cannot be allocated, the existing raw fallback checks every
write and stops accepting records after failure. Assembly failures close member
handles and discard the incomplete book.bin through the existing cleanup path.
Invalid core fields are rejected before the output is opened, preserving a prior
cache in that case. Other failed rebuilds may remove the prior derived metadata
cache; this change does not add transactional publication or alter progress files.

## Resource mechanism

Existing strings used during assembly retain at most 4,096 bytes per field.
Paths and anchors exceeding that bound are rejected. Long contents captions are
trimmed at a UTF-8 boundary using the same existing reader display limit, and the
contents offset table accounts for the trimmed output so later destinations stay
valid. Parsing the original EPUB may still allocate longer input strings; this
change bounds staging decode, not the entire parser's memory consumption.

The existing three transient 4 KiB streams, chunked sizing workspace and checked
low-memory fallback remain. No class field, resident cache, background work or
new allocation is introduced. Peak device heap and task stack are unmeasured.

## Evidence

Eleven new tests cover all observed assembly read/seek/write failure points for
small and 400-chapter inputs, short buffered reads, malformed lengths and indices,
embedded NUL, trailing bytes, wrapper/buffer allocation failure, sticky flush
failure, recovery, invalid core metadata and oversized reported file sizes.
The latter requires no multi-gigabyte test allocation. Existing thousand-chapter
read-budget, no-I/O progress and heap-capped omnibus tests remain required.

Two safe baseline tests fail on r28 as expected: it accepts trailing staging bytes
and retains the full 12,563-byte fixture containing an oversized caption. The new
assembly produces less than 5,000 bytes for that fixture while preserving all
three contents destinations and anchors. This is a synthetic byte-size result,
not a device timing measurement. Malformed-length tests run only after hardening.

Final gates require all eleven newly added names as well as every r28 and pinned
upstream test name. Sources are hashed and mirrored onto local storage before
compilation so network-filesystem timestamp caching cannot silently omit tests.
Clean-upstream validation is rerun for the updated develop base.

## Device verification

Flash the single final X4 Pro image named by `build/FLASH-LATEST.md`. With AA off,
open a fresh copy of an EPUB and a large omnibus, check first/middle/last contents
destinations, reopen and check saved reading position, bookmarks and sleep/wake.
No cache-format migration or general cache deletion is needed. Preserve original
progress files. Ghosting, real SD latency, peak heap and physical input behavior
still require device testing; see the package for host and linker evidence.

## Upstream alignment

Develop advanced by #3573, `ef08c3ad`, while preparing this release. Its
GfxRenderer::truncatedText binary search replaces our local linear-shrink candidate
reuse implementation verbatim. The upstream method retains an input string and
one reusable candidate; the host allocation ceiling is updated from one to two
observed calls. Existing real-font output comparisons at every pixel width, six
styles, UTF-8 and malformed input cases remain active. This deliberately accepts
the maintainer's allocation tradeoff to reduce repeated full-title measurement.
