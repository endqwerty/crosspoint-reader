# Parser context and configuration coverage (EPUB r21)

Base: CrossPoint `develop` `1d61100f90d2e7e32965c14301320d9989a7ab71`, SDK
`111fdcc7f0176c3ee38391a160ee296bf492dbd8`. The latest upstream branch was checked
before work. All r20 reader, Library and storage behavior remains on this base.

## Upstream proposal and scope

This adopts the configuration change in
[PR #2438](https://github.com/crosspoint-reader/crosspoint-reader/pull/2438), head
`2635ed6cdaf0e0b8fb18e55af258bb587c9598c9`: set `XML_CONTEXT_BYTES=0`.
The proposal is open and awaiting review, not an accepted upstream decision.
The only firmware source/configuration change is the shared flag and its comment
in `platformio.ini`. The Expat implementation and application parser interfaces
are unchanged. No cache version or reading-state migration is needed.

Original human author, verified from the commit: Erica Jensen
(`erica@mailershaven.com`). Any future commit incorporating the proposal must
include `Co-Authored-By: Erica Jensen <erica@mailershaven.com>`.

`XML_GetInputContext` has no application caller. Retaining already-parsed bytes
for that API is unnecessary. Expat still retains incomplete tokens and input
needed to resume parsing. `XML_GE=0` remains unchanged. This removes retained
context copies and can avoid buffer growth; it does not impose a smaller limit
on valid XML tokens or add any allocation, persistent object or background work.

## Tests use the firmware parser

The shared host `FirmwareExpat` target builds reader-owned `lib/expat` and reads
`XML_GE` and `XML_CONTEXT_BYTES` from `platformio.ini`. It replaces three separate
hard-coded parser builds and the system-Expat dependency in OPF metadata tests.
Chapter layout, OPF metadata, search/activity and XPath tests now agree with the
firmware configuration. The existing linked-configuration assertion compares
against the configured value instead of its obsolete literal 1024; it retains
the independent assertion that general entities are disabled.

New cases exercise:

- Both XML input APIs with chunks of 1, 7, 1024 and 4096 bytes, UTF-8 splits,
  numeric/predefined entities and 5,000-byte attributes.
- Suspended parsing followed by resume, malformed/truncated XML and every
  observed parser allocation failure, with allocator cleanup checked.
- Production container, OPF, NCX and EPUB 3 navigation parsers fed in 1, 17,
  1024 and 2048-byte chunks. Long labels, nested navigation, decoded URI paths
  and anchors use production FsHelpers instead of an identity stub.
- The existing 512-byte OPF title bound. Long metadata is still bounded rather
  than expanding firmware retention to satisfy an artificial test expectation.

A host-only retained-context target compiles the same Expat source with 1024
bytes of context. It runs the same low-level boundary, failure and memory tests.
It contributes nothing to the firmware.

## Measured mechanism and limits

For 4,096 short paragraphs streamed in 1024-byte chunks, the same-source host
comparison produced identical text hashes, byte counts and element counts for
`XML_Parse` and `XML_GetBuffer`/`XML_ParseBuffer`:

| Parser allocator requests | Retain 1024 | Retain 0 |
| --- | ---: | ---: |
| Bytes still owned immediately after parsing | 8,772 | 6,724 |
| Peak concurrently owned requested bytes | 10,820 | 6,724 |
| Allocation/reallocation calls | 14 | 13 |

The old configuration grows an input buffer and briefly owns both old and new
storage. The new configuration avoids that growth on this workload. Savings
vary with token lengths, chunk boundaries and parser use; this is not a fixed
per-parser memory guarantee. The measurement excludes allocator bookkeeping,
application allocations, unobservable realloc internals and device heap layout.
It does not establish device load-time or panel improvements.

The final package records source-bound measurements, full native/sanitizer test
registries, firmware build/image checks and scoped static analysis. Historical
r20 tests remain required; no test is removed or disabled.

The separate SdFat FAT-cache proposal (PR #3685) was reviewed and deferred. It
also patches cache failure handling and adds per-volume storage. It needs its
own real-SdFat fault/operation-count harness before a local adoption; reported
X3 timings are not evidence of X4 Pro gains.

## Device verification

Use the single BIN in `build/FLASH-LATEST.md`, initially with anti-aliasing off.
Open EPUB2 and EPUB3 books, check their TOC and Library metadata, change text
settings once to exercise a fresh chapter layout, and use Find in Book on demand.
Check saved positions and sleep/wake. Existing caches remain valid; no general
cache clearing or manual recordings are required. Physical timing, ghosting and
peak device heap remain unmeasured.
