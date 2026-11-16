"""Compile complete production sleep-image flows against recording decoders/display."""

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "cmake"))
from extract_functions import extract  # noqa: E402

source = pathlib.Path(sys.argv[1]).read_text()
starts = (
    "void cleanBeforeAbsoluteGray(",
    "HalDisplay::GrayscaleMode sleepGrayscaleMode(",
    "bool displayBwIfSleepGrayscaleUnsupported(",
    "class BwFrameKeeper {",
    "bool beginOverlayGrayPlane(",
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
    if not marker.startswith("class "):
        functions.append(extract(source, [marker]))
        continue
    if source.count(marker) != 1:
        raise SystemExit(f"Expected one production class: {marker}")
    start = source.index(marker)
    end = source.index("\n};\n", start) + len("\n};\n")
    functions.append(source[start:end])
pathlib.Path(sys.argv[2]).write_text("\n".join(functions))
