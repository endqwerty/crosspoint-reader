# Sleep image grayscale fallback

`SleepGrayscaleTest` extracts and compiles the complete production sleep-screen
functions from `SleepActivity.cpp`, including the BMP cover, transparent BMP and
transparent PNG paths. The extraction depends on the source file and fails if
function markers move. Decoding, storage and display hardware use recording
fixtures; the capability branch and early returns are production code.

Nine tests cover unsupported controllers, inverted display capabilities, and
supported Overlay/Absolute/Direct modes across all three image paths. An
unsupported path must display its already decoded B/W image once with HALF,
retain its pixels and background, and perform no grayscale base, plane uploads
or grayscale activation. Supported paths still run their two-plane composition.
No new firmware allocation is needed for the fallback.

Build and run from a configured host test directory:

```sh
cmake --build build/host --target SleepGrayscaleTest
build/host/sleep_grayscale/SleepGrayscaleTest
```

These tests do not decode actual artwork or measure optical panel behavior.
The SDK Pro host tests separately check that UC8279 LUT variant `0x67` rejects
all grayscale modes without writing the panel bus.
