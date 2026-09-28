# Find-in-book host coverage

`EpubSearchTest` exercises the production streaming search engine and Expat.
`EpubSearchActivityTest` also compiles the complete production search activity,
with fixture EPUB streams, input hardware and UI dependencies. Its input pump
is extracted from `MappedInputManager::update()` and uses the production
`HomeButtonInput` implementation.

Activity checks cover lazy query submission, bounded results, read failures,
ordinary Back cancellation, and returning the framebuffer before navigation.
Home cancels a scan even when Back remains held; navigation occurs after the
stream, framebuffer loan and render lock unwind. Other configured actions
survive repeated scan polls and reach the main loop once, preserving the first
deferred action. These are host lifecycle tests, not device input-timing tests.
