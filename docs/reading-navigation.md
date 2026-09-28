# Reading navigation

Ordinary content-link taps retain a Back destination without replacing the reader's saved
progress on exit. Opening the Footnotes action starts a temporary excursion: closing the book
while still inside that excursion restores its outermost origin. Links followed from a note
remain part of that excursion, and Back unwinds each remembered destination.

The existing parser collects internal links into the Footnotes list without retaining semantic
`epub:type=noteref` metadata. The reader therefore distinguishes the user's action (content-link
tap or Footnotes action), not a guessed meaning inferred from link text or filenames.

Return history uses three fixed entries and no dynamic allocation. When full, the oldest ordinary
return destination is dropped; once an outermost footnote origin occupies the first entry, that
origin is retained and the next-oldest destination is dropped instead. Invalid href resolution
does not modify history. The typed history adds 12 bytes to the activity on ESP32 compared with
the former three pairs of integers plus depth counter. It changes no persistent file format.

The Footnotes menu entry appears first when a page has links. A single item opens directly;
multiple items use the existing picker. The power shortcut and menu share this behavior. The
picker's existing heap allocation is now fallible and checked; the single-item case avoids it.
Cancelling the picker returns to the originating menu or page.

## Attribution

Progress semantics adapt [PR #3495](https://github.com/crosspoint-reader/crosspoint-reader/pull/3495),
commit `7935842b55c91e8fc718961c93454bd04f990164`, by **Dan <dpatynski@gmail.com>** (`dfourn`).
The single-note shortcut and menu ordering adapt
[PR #2457](https://github.com/crosspoint-reader/crosspoint-reader/pull/2457), commits
`995f3d4821a1985c31d98b28a191b8c2935e7428` and
`7e0638f3bf8385ea6021fe7c970cf836c4b574da`, by **Davide Masserut <dm@mssdvd.com>** (`mssdvd`).
Author identities were checked against the upstream commits. Include these human authors as
co-authors if these adaptations are committed. The dynamic single-note label from that PR was
not imported; the existing translated Footnotes label serves both entry points.

## Verification

The `reader_link_navigation` host target compiles the actual reader methods with a lightweight
activity/storage fixture. Its checks cover saved resume coordinates, normal reading after links,
mixed/nested excursions, invalid targets with full history, bounded retention, zero/one/multiple
footnotes, cancellation and picker allocation failure. It does not validate physical panel
appearance or simulate SD persistence across a power cycle.
