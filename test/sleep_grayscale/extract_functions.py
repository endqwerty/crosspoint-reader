"""Compile complete production sleep-image flows against recording decoders/display."""

import pathlib
import sys

source = pathlib.Path(sys.argv[1]).read_text()
starts = (
    "HalDisplay::GrayscaleMode sleepGrayscaleMode(",
    "bool displayBwIfSleepGrayscaleUnsupported(",
    "AlphaOverlayResult tryRenderTransparentOverlayBmp(",
    "void SleepActivity::renderBitmapSleepScreen(",
    "bool SleepActivity::renderTransparentOverlayPng(",
)
functions = []
for marker in starts:
    if source.count(marker) != 1:
        raise SystemExit(f"Expected one production function: {marker}")
    start = source.index(marker)
    end = source.index("\n}\n", start) + 3
    functions.append(source[start:end])
pathlib.Path(sys.argv[2]).write_text("\n".join(functions))
