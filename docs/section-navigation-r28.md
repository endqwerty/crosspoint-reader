# Chapter navigation r28

Based on develop `6743b68353dcc692e87826f5320c5480392f9e5b` and SDK
`111fdcc7f0176c3ee38391a160ee296bf492dbd8`, preserving all r27 features.
This refactors existing Section methods under ROADMAP Phase 1 reliability and
footprint work. Public APIs, on-card byte layouts and cache versions are unchanged.

## Failure handling

The prior cached-page-count, anchor, paragraph and list-index lookup methods used
unchecked reads and seeks. Failed reads could leave local values uninitialized;
a corrupt anchor length reached std::string::resize before validation.

A shared internal reader now checks the complete lookup header, supported finalized
or partial version, section ordering and table extents with 64-bit arithmetic.
Paragraph/list lookups check their stored count against the header. Failed seeks,
negative reads and positive short reads return no result. Partial caches retain
navigation over committed pages but cannot supply a complete chapter total.
The reader checks the full declared table extent even for an early selected row.

Anchor matching checks each selected/scanned entry against the anchor-region end,
rejects out-of-range pages, skips names of different lengths, and compares equal
length names in a fixed 64-byte buffer. No allocation uses a length read from SD.
The first matching anchor retains precedence; empty and long keys remain supported.
Records after an early match are not scanned. This validates lookup structure and
accessed entries, not every page payload in the chapter.

## Mechanism and resources

Paragraph and list-item searches retain first-at-or-after semantics, duplicate
handling and final-page fallback. They now read 16 indices at a time through a
32-byte stack buffer. Shared header state is a small fixed local structure; no
resident cache, new heap allocation, background work or saved state is added.
File size is queried once and the anchor cursor is tracked locally to avoid
repeated HAL position queries. Ordinary page painting is unchanged.

For a 1,024-page synthetic chapter, paragraph/list scans use 68 HAL reads versus
1,026/1,027 before. Extra header validation reads are included. Byte totals are
not claimed unchanged. Anchor and numeric lookup paths register zero allocations
in the instrumented host test; the previous anchor lookup allocated a temporary
string for a 135-byte key. These are host API counts, not elapsed device time.

## Tests and compatibility

Ten new tests cover real-writer round trips for complete/partial files, every
read/short-read/seek failure point, version/header rejection, truncated/overlapping/
overflowing offsets, mismatched counts, empty and 65,535-page tables, duplicates,
chunk boundaries, corrupt anchor lengths including UINT32_MAX, invalid anchor
pages, empty keys, duplicate keys, fixed-buffer comparison and allocation budgets.
All existing test names remain required. The safe baseline comparison ran two
valid-input performance/allocation tests against the saved r27 implementation;
malicious-length tests were run only after hardening to avoid a baseline 4 GiB
allocation. The harness compiles the complete production methods and helpers.
Clean-upstream validation is retained from r25 for the identical upstream base.

## Device verification

Flash the single final image named by `build/FLASH-LATEST.md` for Xteink X4 Pro.
With AA off, open a chapter, follow footnotes/internal links and return, change a
layout setting, reopen a book, check saved position and partial chapter resume,
then sleep/wake. No cache migration or general deletion is needed. Preserve
original progress files. Physical ghosting, actual SD latency, task stack peaks
and peak heap remain unmeasured; see package logs for host and linker results.
