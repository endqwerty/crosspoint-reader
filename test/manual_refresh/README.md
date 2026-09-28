# Manual refresh handoff

This harness extracts the complete production `FrontlightPanelActivity::loop`,
tile callback, `runTile`, light controls, `onExit`, `persistLightSettings`, `close`,
and `render` methods. It links the real
`GfxRenderer` against the existing recording HAL. Fixture code supplies activity
lifecycle calls, a checked render lock, settings, and a recognizable panel image.

The regression schedules a panel paint after the Refresh tile requests dismissal,
through the production input loop, then exits under the activity manager's lock
and paints the destination. FULL must
reach that destination exactly once; the late panel paint must keep its requested
FAST or HALF mode. Further cases cover ordinary dismissal, repeated requests,
settings-write behavior, async B/W, grayscale, and unsupported-grayscale fallback.
The loop holds the render lock across routing and callbacks because FreeInkApp
routing and rendering share event state. Callbacks also accept the renderer's
already-held lock, without acquiring a nested lock. Tests assert both entry paths,
plus button, swipe, idle, and drag-release paths through the complete input loop.

Pop, replacement, and sleep transitions all call the outgoing activity's `onExit`
under the manager's render lock. A replacement therefore receives an explicitly
requested cleanup just as a popped reader does. Ordinary exits add no cleanup.
The production change adds one Boolean and no heap allocation.

The test does not run the FreeRTOS scheduler, complete UI toolkit, or display
driver. It forces the relevant interleaving and checks actual renderer submission;
existing SDK sequence tests cover controller commands. No optical or page-speed
measurement is claimed.
