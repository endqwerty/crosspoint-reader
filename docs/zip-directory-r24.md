# Checked ZIP directory scans and extraction (r24)

This work remains based on CrossPoint develop
`4a6283db9c9692e059eaf2d57d5b7a36d85a2f4d` and SDK
`111fdcc7f0176c3ee38391a160ee296bf492dbd8`. The upstream branch and open proposals
were reviewed on 2026-09-26. The change follows the current roadmap's reader,
HAL, memory and code-consolidation priorities; it does not add a new reader path.

## Reproduced problem

An external test against r23 let the ZIP reader read a valid directory signature,
then made subsequent reads and seeks fail persistently. The scan did not advance:
it exceeded the fixture's 256-operation safety budget. All five existing ZIP tests
passed, while this fault test failed. This models a persistent SD error; it is not
a measurement of physical card removal or watchdog behavior.

Stored-entry streaming had a separate signedness hazard: a negative HAL read was
converted to an unsigned byte count before being sent to the consumer. Failed
payload seeks were also ignored. Those failures now return an error before data
from an invalid position or an oversized length reaches the consumer.

## Changes and compatibility

The four central-directory readers now share one checked header decoder:
individual size lookup, eager stat-cache population, batch sizing, and enumeration.
A complete fixed header is read at once, followed by its filename. Declared entry
lengths are checked against the directory boundary before any skip. All reads and
seeks must succeed. Loops are bounded by the declared entry count, and individual
lookups retain their sequential cursor and wraparound behavior. Failed eager-cache
population clears partial entries before a retry.

The end record validates the single-disk counts, directory range, and comment
length. Integer decoding does not depend on alignment or signed shifts. The
existing 1 KiB trailer-search window and 255-byte supported entry-name limit
remain; longer names are skipped intact. Optional directory signatures, extra
fields, comments, and exact-length filename views have regression coverage.
These layouts follow [PKWARE APPNOTE sections 4.3.12–4.3.16](https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT).

Batch sizing returns -1 for I/O or structural failure. Its sole production caller,
BookMetadataCache, rejects that result and removes incomplete output. A missing
filename in an otherwise readable archive still produces a normal unmatched
result. Zero-sized streaming buffers are rejected, local-header and payload seeks
are checked, stored-stream negative reads are rejected, and output allocation
arithmetic uses the address-sized type with an overflow check.

No EPUB or Library cache format changes. The r23 bounded indexing work and all
previous reading/library features remain. No new background work, settings,
connectivity, or user-interface allocation is introduced.

## Resource effects and evidence

The decoder adds no heap buffer. It uses a 46-byte header array; callers retain a
filename buffer, now 255 bytes and explicitly length-delimited. ZIP state gains a
32-bit directory boundary and a 16-bit cursor ordinal to bound scans and preserve
wraparound with an optional directory signature. Existing stat-cache allocation
behavior is retained; this change does not claim to make every ZIP allocation
fallible.

In the same two-entry host fixture, batch scanning changes from 19 reads and
9 seeks to 5 reads and 2 seeks. This reduces separate HAL calls and mutex entries;
it does not directly measure physical SD transactions or elapsed time. Other HAL
queries, such as position checks, are not included in those counts.

Tests cover every persistent and transient directory read/seek failure, partial
reads, retry, range/count/length corruption, cursor wraparound, supported-name
boundaries, extra fields and comments, extraction of stored/deflated payloads,
extraction I/O failures, zero-sized buffers, and second-chunk cache cleanup.
The final package carries complete native and LLVM22 ASan/UBSan registries,
firmware/image validation, static analysis, source hashes and the source archive.
All r23 test names remain required.

## Device check

Use only the image named by `build/FLASH-LATEST.md`, initially with anti-aliasing
off. Open an uncached EPUB, navigate chapters, reopen it, then check Library,
Authors/Series, bookmarks and sleep/wake. Keep original progress caches when
making test copies. No recordings or deliberate card removal are required.
Host validation does not establish optical ghosting, electrical SD recovery,
actual peak heap, or on-device latency.
