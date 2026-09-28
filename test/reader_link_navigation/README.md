# Reader link navigation tests

`ReaderLinkNavigationTest` compiles the production `openFootnotes`, `navigateToHref`,
`saveLinkStack`, `loadLinkStack`, `restoreSavedPosition`, and `jumpToPercent` methods, the saved-progress
load block, and the bookmark/chapter/toolbar contents result handlers extracted from
`EpubReaderActivity.cpp`.
The extraction fails at configure time if its boundaries move. It also uses the production
fixed-capacity `ReaderNavigationHistory`.

The surrounding fixture substitutes chapter resolution, activity creation, render locking,
and persistence. Tests cover ordinary-link progress preservation, the `links.bin` back-stack
saved on exit and consumed on open, nested and mixed note/link returns, bounded history, failed targets, direct one-note navigation, multiple-note selection,
cancel behavior, and allocation failure. They also cover first-spine resume in all three
progress formats, exact percentage boundaries, stale navigation targets, and explicit
destinations retiring a temporary footnote origin. History retains page counts and visible
offsets for return after re-pagination. Save assertions check the exact resume payload
requested by the reader; they do not simulate SD writes or a physical power cycle.

The full firmware build validates integration with the real activity and storage APIs. Hardware
verification can use an EPUB with a Begin Reading link and an endnote: read onward after the
link and reopen, then exit while a note is open, reopen on the note and press Back. While a note is open, change
text size and return, or choose another chapter and reopen the book. No recording is required.
