# Library folder scans r31

Based on develop `ef08c3ad19787fecda7b8b4ab1ad1e1d17e10862` and SDK
`111fdcc7f0176c3ee38391a160ee296bf492dbd8`, retaining r30 behavior and tests.
This refactors existing LibraryIndexFile::readPath under roadmap Phase 1 footprint
and reliability work. Public APIs, library format/fold versions, sort order,
metadata and all existing features remain unchanged.

## Mechanism and limits

The folder section stores variable-length paths. Resolving folder N previously
made an individual one-byte HAL read for each preceding folder length. A local
64-byte window now supplies nearby lengths together. Its fill ends at the folder
section boundary or current 512-byte alignment boundary, whichever comes first;
it never reads a new sector merely to speculate on later folder lengths.
The requested folder path and filename retain their existing checked reads.

The window adds 64 bytes of temporary stack data plus small cursor bookkeeping.
No new heap allocation, class field, resident folder table or active-reader work
is introduced. Existing path strings remain unchanged. Every invocation creates
a fresh window, so a prior success cannot mask a later SD failure. Read/seek
failures still pass through readAt, preserving cursor and sort-cache invalidation.
Every failure leaves the returned path empty.

For 4,096 short folder paths, host HAL counts change as follows:

| Metric | r30 | r31 |
| --- | ---: | ---: |
| Reads | 4,098 | 570 |
| Seeks | 4,097 | 569 |
| Bytes copied by HAL reads | 4,113 | 32,561 |

The extra bytes come from bounded read-ahead within the sector containing the
requested length byte. This reduces HAL/mutex call count; it is not a claim of
fewer physical SD transactions or measured device latency. Long folder paths can
fall outside each 64-byte window and receive little or no call-count benefit.
No timing or peak device heap/stack measurement is claimed.

## Validation

Five new tests cover a 4,096-folder call budget, all 73 paths in a mixed 1-255-byte
UTF-8/boundary fixture, every observed negative/short read and seek failure point,
malformed lengths, invalid folder/name references, retry, stale output clearing,
and repeated calls/reopen. Captured read ranges enforce folder-section and sector
bounds for each speculative fill. The safe baseline call-budget test fails on
r30 as expected; all old test names remain mandatory. Clean-upstream test evidence
is retained from r29 for the identical develop revision.

## Device verification

Use the single final X4 Pro image named by `build/FLASH-LATEST.md`, initially with
AA off. In a library distributed across many folders, open books near the end,
show their details, refresh while selected, and confirm the correct book remains
selected and opens. Check Authors/Series drill-in and saved reading positions.
No cache migration or deletion is needed. Preserve original progress files.
Physical input behavior, ghosting, SD latency and stack peaks remain unmeasured.
