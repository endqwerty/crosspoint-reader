# Reader overlay restoration

The host target compiles the actual EPUB `closeOverlayToPage()` and
`discardOverlayPage()` bodies plus the actual refresh-cycle helper. The real
GfxRenderer stores, restores and releases the B/W snapshot; only activity UI,
lock and HAL display dependencies are replaced with fixtures. CMake tracks the
production source and fails if extraction markers move.

Tests cover byte-exact restoration in four orientations, keeping the previous
panel baseline while erasing chrome, scheduled/manual/promoted cleanup,
grayscale fallback, missing/invalidated snapshots and existing non-Xteink
behavior. The recorded grayscale flag is conservative: image pages and any
page still requiring AA must use the full render path.

The render prologue and toolbar chapter-jump lambda are also compiled directly.
Their tests guard snapshot invalidation before early returns or queued chapter
renders, and verify that a clamped chapter jump does not request a repaint.

The production activity transition branch and EPUB suspension hook verify that
deferred chrome settles under the existing RenderLock before a child can paint
the shared framebuffer. A failed wait or baseline cleanup carries a full-refresh
promotion into the child's first display. A push without pending chrome performs
no baseline cleanup. The lock fixture detects recursive acquisition and confirms
the manager releases its lock before entering the child.

The fixture does not execute the entire activity manager or settings UI.
Snapshot creation/invalidation and classification wiring require source review;
the firmware build checks their integration. Driver-sequence tests separately
verify differential RAM. Neither target models physical ghosting.
