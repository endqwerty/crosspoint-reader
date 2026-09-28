# Reader navigation regression tests

Compile the actual production navigation methods and caller blocks with small
host fixtures. CMake extracts their source and fails if a boundary marker moves;
it reconfigures when any tested reader source changes. The generated
`ReaderNavigationProduction.cpp` in the build directory is inspectable.

The tests cover ignored boundary inputs, successful forward/back/skip requests,
queued EPUB turns, chapter and end-of-book transitions, and explicit forced
refresh. The real XTC/EPUB navigation methods determine whether an update is
needed. This verifies that rejected turns do not enqueue a redraw or consume the
reader's refresh cadence; it does not execute the complete EPUB loop, renderer,
FreeRTOS scheduler, or physical panel. EPUB caller blocks are exercised after
their earlier input/UI guards, which remain outside this change.

Run after configuring the root host-test project:

```sh
cmake --build build/host-tests --target ReaderNavigationTest
ctest --test-dir build/host-tests -R ReaderNavigation --output-on-failure
```
