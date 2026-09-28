# Reuse library-search normalization storage r44

All r43 improvements remain on develop `93e98bb` and SDK `111fdcc7`.
This change reduces repeated allocation during library searches.

## Mechanism

The filter already reused a string for reading full title, author and series
fields, but each fold(candidateText) returned a newly allocated normalized string.
The new foldInto(text, out) reuses caller-owned output capacity. The existing fold
API delegates to the same implementation, preserving all normalization rules.
Input to foldInto must not alias its destination; the search uses distinct strings.
An empty input clears the destination, avoiding a stale previous match.

Search now holds one additional local string object and reuses the output storage
that normalization already needed. Capacity grows only when necessary, using the
standard string reserve behavior; small fields remain inline. Stored fields are
bounded by the existing one-byte lengths. The scratch storage is released at the
end of filterBooks, so it adds no resident cache or active-reading work. Peak
whole-search or device heap is not claimed to be lower.

The stored-prefix shortcut remains first. Searches satisfied by that prefix never
normalize full fields. Series reuse from r42 and canonical-key reuse from r43
remain. Cache formats, sort/search rules, public fold callers and HAL reads are
unchanged.

## Synthetic evidence

A calibrated allocation counter runs 2,000 normalization calls over alternating
255-byte and shorter long fields. The returning-string API makes 2,000 allocation
requests. The caller-owned output starts empty and makes one allocation, retaining
capacity for subsequent calls. A short-field/empty-field sequence makes zero.
Both APIs execute the same production normalization implementation; the returning
API serves as a positive allocation-counter control.

This isolates normalization, not a full Library scan or ESP32 peak heap. It does
not imply that an entire search makes only one allocation, nor does it measure
allocator fragmentation or device elapsed time.

Four new tests cover repeated Unicode NFC/NFD folds against existing expected
strings, empty outputs, bounded UTF-8 views, the long/short allocation workloads,
and a real UI filter spanning long titles, accented authors, series and filename
fallbacks. The UI test asserts every result and unchanged metadata request counts.
Existing title-prefix, series-reuse, sorting and index-output tests remain required.

## Device check

Use the single final X4 Pro image linked in build/FLASH-LATEST.md. Search long
titles, accented author names and series, then change/clear the query. Include a
book without title metadata to exercise filename fallback. Existing caches remain
compatible; no recordings or cache deletion are needed. Start with AA off for
reading. Actual device timing and peak heap remain unmeasured.
No commits, pushes or PRs were created.
