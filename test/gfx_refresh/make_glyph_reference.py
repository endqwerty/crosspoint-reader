"""Build a host-only renderer with the frozen r11 glyph implementation."""

import pathlib
import re
import sys

source, reference, output = map(pathlib.Path, sys.argv[1:])
text = source.read_text()
pattern = r"template\s*<TextRotation rotation = TextRotation::None>\s*static void renderCharImpl\([\s\S]*?\n}\n"
matches = list(re.finditer(pattern, text))
if len(matches) != 1 or "renderer.drawGlyphBitmap" not in matches[0].group():
    raise SystemExit("Expected one production glyph dispatch; update reference extraction explicitly")
result = text[: matches[0].start()] + reference.read_text() + text[matches[0].end() :]
output.write_text(result)
