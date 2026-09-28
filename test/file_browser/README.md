# File Browser integration coverage

The fixture compiles the production lazy-row, search, input, rename, and prewarm
methods extracted from `FileBrowserActivity.cpp`, including its rename rollback
helper. It uses the production folder walker, Unicode composition, path helpers,
bookmark paths, reading-state persistence, and SDK list navigation.

The HAL models files and injected write/rename failures. The renderer records
prewarm requests; child activities and the options popup model input handoffs.
This covers the search sentinel in both browser modes, bounded row prewarming,
raw Unicode paths, nested search-result renames, bookmark primary/backup/staging
preservation, reading-state migration and rollback, and failed allocations.

These are host checks. They do not establish physical SD-card atomicity during
power loss, panel timing, or device heap usage. Recursive deletion is outside this fixture; its UI boundary is stubbed here.
