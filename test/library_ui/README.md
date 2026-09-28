# Library activity regression tests

The harness extracts the current production LibraryListActivity methods at CMake
configure time, including session entry, filters, grouping, book actions, child
callbacks, and visible row composition. It fails configuration if a method is
missing or its braces cannot be parsed. LibraryMenuActivity's complete constructor
and method bodies compile in a separate test target.

The real LibraryText fold/query functions and LibrarySession token policy are
linked/included. Narrow in-memory index, storage, state, recents, child-activity,
and rendering stubs record requests and inject failures. The linked SDK's actual
ListNav and FreeInkUI geometry functions handle viewport synchronization; the
production ring-selection and ButtonNavigator page-index methods are extracted
alongside the Library's button dispatch. These are dependency
seams; navigation, filter, callback, and row-building behavior comes from the
production source. The backend suites separately test actual index/state files.

Coverage includes:

- Reconcile once per session, metadata mismatch, invalidation and failed refresh.
- Title/author/series query matching combined with shelf-state filtering.
- Filter allocation/read failures, sort remapping, stale/out-of-range row actions.
- Series identity boundaries even when displayed names are identical, group
  expand/collapse, and visible-row storage bounded independently of total books.
- Child cancellation, index release/reopen, reopen failure, render-lock acquisition
  in callbacks, and swallowed held-button releases.
- Cancelled/failed deletion preserving caches and recents, successful deletion
  clearing them once, and independent favorite/reading-state changes.
- Popup count bounds, touch action values, explicit Back cancellation, and safe
  margins for portrait and landscape geometry.
- First/last row wrapping, held-button tab switching, measured page jumps with
  deferred viewport movement, Back restoring a collapsed group, a clipped
  trailing row, and clearing queued scroll when the filter changes.

This does not run FreeRTOS scheduling, SD drivers, the full screen renderer,
keyboard hardware, or the complete ActivityManager stack. The final firmware
build checks integration with those real interfaces.
