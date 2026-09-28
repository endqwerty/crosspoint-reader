# Offline EPUB improvements (r5)

The six approved work areas are Library scalability/upstream polish, configurable
series grouping, Favorites and reading states, display hardening, link/footnote
navigation, and on-demand in-book search. No network or additional media feature
is added. Preserve the current local Library and page-turn work.

## Library

Library reconciles on the first entry after boot, after known SD mutations, or
through Library options -> Refresh library. Subsequent entries reuse the validated
SD index; metadata-mode or format changes force reconciliation. Files copied to a
card while the device remains powered on require Refresh library. Existing files
remain available when a rebuild fails. The 4,096-book cap is retained pending a
larger external-sort design; exceeding scan/path/depth limits is shown explicitly.

The existing Recent / Added / Title tabs remain; the final grouping tab is
configurable as Author or Series, following maintainer feedback on #3059.
Metadata parsing and SD tables adapt the community series work; host checks cover
ordering, reuse, failure, and prior-format migration. Per-book series data is not
added to reader Page or Section objects. Series parsing/sorting scratch exists
only during indexing and is checked and released between phases.

Library options are accessible from the header icon or holding Confirm on the
tab strip. Book actions are accessible by holding a row/Confirm. They include
Favorites, explicit Unread/Reading/Finished states, and confirmed deletion.
Reading states live separately from the rebuildable metadata index and are
path-keyed. Opening a book marks it Reading; leaving its end screen marks it
Finished. Existing unmarked books can be classified manually or by opening them.
Moving files externally does not yet preserve path-keyed state/progress.

See [index and state implementation](library-r5-backend.md) for exact format,
allocation and migration details. Series extraction/indexing adapts
[PR #3059](https://github.com/crosspoint-reader/crosspoint-reader/pull/3059),
reviewed head `8a51da298ca19a6b69290b05012970a080974c6b`, by Kenton Hamaluik
<kenton@hamaluik.ca>. Retain that human attribution if committed.

## Display and navigation

See [display hardening](display-hardening.md) and
[reading navigation](reading-navigation.md). New regional driver support remains
unused by application UI until a suitable region-aware caller is validated.
No optical ghosting or panel-latency claim is derived from host tests.

## Search resource contract

Find in Book is an explicit menu action. Search parser, query, buffers and results
belong only to the dedicated search activity and are released before returning
to reading. Ordinary reading performs no search processing or SD access and holds
no search heap. The parser's bounded allocator may retain one null context pointer
as fixed bookkeeping. Firmware code and translations naturally occupy flash.

See [Find in Book](find-in-book.md) for parser bounds and search limitations.
The implementation uses existing visible-codepoint navigation, cancellation and
bounded results. It does not create a background task or add search fields to
pages/sections. Failed or cancelled searches must leave the reading location intact.

## Release process

The release handoff records targeted tests, independent reviews, the full host
gate, X4 Pro ESP32-S3 build, static checks and binary validation. The historical r5
application image was `firmware-x4pro-epub-r5-final.bin` in `build/x4pro-epub-r5/`.
The current image is identified by package FLASH.md or the [r7 handoff](reader-reliability-r7.md).
`build/FLASH-LATEST.md` is updated only after those packaging checks succeed.
The release source bundle must contain both root changes and the dirty SDK diff
plus its new Direct grayscale LUT header.
