"""Compile complete production sleep-image flows against recording decoders/display."""

import pathlib
import sys

source = pathlib.Path(sys.argv[1]).read_text()
starts = (
    "void cleanBeforeAbsoluteGray(",
    "HalDisplay::GrayscaleMode sleepGrayscaleMode(",
    "bool displayBwIfSleepGrayscaleUnsupported(",
    "AlphaOverlayResult tryRenderTransparentOverlayBmp(",
    "void SleepActivity::renderBitmapSleepScreen(",
    "bool SleepActivity::renderTransparentOverlayPng(",
)
marker = "#if FREEINK_DEVICE_METALIO_EINK4\nconstexpr auto kSleepClean"
if source.count(marker) != 1:
    raise SystemExit("Expected one production sleep refresh constant block")
start = source.index(marker)
end = source.index("#endif", start) + len("#endif")
functions = [source[start:end]]
for marker in starts:
    if source.count(marker) != 1:
        raise SystemExit(f"Expected one production function: {marker}")
    start = source.index(marker)
    end = source.index("\n}\n", start) + 3
    functions.append(source[start:end])
pathlib.Path(sys.argv[2]).write_text("\n".join(functions))
