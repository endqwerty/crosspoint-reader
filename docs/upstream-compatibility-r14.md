# r14: upstream integration and reading navigation

Target: ESP32-S3 Xteink X4 Pro, `x4pro-gh_release`.
Version: `1.6.5-dev-x4pro-r14-b88b653`.
Intended application image: **`firmware-x4pro-epub-r14-final.bin`**.

## Foundation and retained scope

This integrates CrossPoint `develop`
[`b88b653a797c9952c68df5bb373e560d65682f67`](https://github.com/crosspoint-reader/crosspoint-reader/commit/b88b653a797c9952c68df5bb373e560d65682f67)
and its FreeInk SDK pin `deb62ab7e02b8216c5ee86a0721bf99f985c6f23`,
15 commits beyond r13. The upstream base advances in the actual checkout;
local extensions remain uncommitted. No push or PR is part of this work.
This is a pinned development build, not an upstream release candidate.

Upstream owns interfaces and interaction conventions. This integration adopts
its new footnote selection, popup sliders, touch gestures and list scrolling,
accepted Cover Grid theme, Portuguese hyphenation, font-table deduplication,
TrueType support on PSRAM boards, and WordStore changes. The local bitmap helper
is retired in favor of upstream's merged implementation; byte-comparison tests
continue to check raster output. The old combined Always Next option is removed.
Saved mode 4 migrates to upstream's next-page Tap Only and previous-page Swipe
Only settings, preserving the intended tap behavior and center-menu priority.

Names-only Authors/Series drill-down, Favorites/state filters, recursive file
search, lazy Find in Book, dark boot splash, checked saved positions and bookmarks,
page/library storage optimizations, fallible parser/cache writes and display
transaction recovery remain. Search still runs only while its activity is open.
No new panel waveform or timing shortcut is enabled. Future compatibility still
requires review against the next upstream revision.

## Focused UX fixes

- Favorite/reading-state actions retain the current author/series and selected
  book. If a state filter removes that book, select a neighboring survivor in the
  same group. If its last book disappears, return to a valid group-directory row.
  Failed writes and text-only searches no longer cause unnecessary refiltering.
- Reject chapter destinations outside the book's spine before changing reader
  position, section ownership or navigation history. Invalid metadata must not
  accidentally reach the end-of-book state and mark a book finished.
- Show a translated bookmark-load error when existing storage is unreadable or
  corrupt. Missing or valid-empty bookmarks still show an empty list. Opening the
  error screen does not rewrite the stored bookmarks.

The Library fix reuses existing sorted-row maps and moves its existing heading;
it adds no persistent field, allocation or index read beyond the existing filter.
Its extra work is bounded to the 4,096-book index limit and occurs only after an
explicit state action. Chapter validation adds comparisons only on navigation.
Bookmark error state is one flag in an activity allocated only when opened.
Selection across a full Library rebuild is separate work because row ordinals
are not stable after reordering.

The WordStore integration keeps upstream's reclaim-and-retry behavior. If both
attempts fail, the parser latches the error immediately, before a table or rule
can replace the empty text block and hide the dropped word. Neither complete nor
partial caches may commit that failed parse. Section cache layout remains
complete version **48**, partial sentinel **234**, header **43 bytes**; WordStore
is layout-time storage, not a wire-format change. TTF cache identity already
includes a salted family/point-size font ID. No extra cache-format bump is needed.

Footnote and dictionary page snapshots use the existing render lock, releasing
it before navigation or child-activity launch. Popup slider input also holds the
existing lock while dispatching events and changing values, consistent with the
frontlight panel. This protects state shared with the render task without adding
a second framebuffer or background task.
The single-footnote shortcut owns a temporary href copy so it can release the
lock without borrowing a string from the render task's mutable link vector. It
exists only during explicit selection and preserves the existing string-based
navigation API; truncating an arbitrary EPUB href into a fixed buffer could
select the wrong destination.

## Validation boundaries

The matching package records the final full native and LLVM22 ASan/UBSan runs,
test-name comparison with r13 and clean upstream, SDK display/UI/font checks,
upstream list-viewport checks, scoped static analysis and X4 Pro build/image
checks. The package includes complete source manifests, binary patches, pins and
the pre-integration source/index backup. `build/FLASH-LATEST.md` changes only after
the final package verifies successfully.

The new TrueType tests use the actual adapter and FreeType backend for font
metrics/raster/cache behavior and check the non-PSRAM compile guard. They do not
prove runtime PSRAM headroom: upstream still has allocations that can abort if
memory becomes unavailable after its preflight check. This merits a separately
measured upstream-compatible reliability change. Static RAM is not peak heap.

No physical device has been flashed or measured by these checks. They do not
establish improved optical ghosting, panel settling, real SD latency or Kindle
parity. Historical page-composition measurements are not new r14 device results.

See [this week's roadmap](vacation-roadmap-r14.md) for acceptance criteria and
follow-ups. Start device use with anti-aliasing off; verify forward/back reading,
chapter and footnote selection, popup/menu return, bookmark display, Authors/Series
state actions, saved positions and dark boot. No recordings are required.
