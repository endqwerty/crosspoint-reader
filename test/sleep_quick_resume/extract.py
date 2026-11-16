"""Compile the sleep entry routing prefix and complete Quick Resume paint method."""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'cmake'))
from extract_functions import extract  # noqa: E402

source = pathlib.Path(sys.argv[1]).read_text()
start = source.index('void SleepActivity::onEnter() {')
end = source.index('  const uint8_t sleepScreen = SETTINGS.sleepScreen;', start)
prefix = source[start:end]
assert 'renderLastScreenSleepScreen' in prefix and 'display.setInverted(false)' in prefix
paint = extract(source, ['void SleepActivity::renderLastScreenSleepScreen() const {'])
pathlib.Path(sys.argv[2]).write_text(prefix + '\n  (void)frameWasInverted;\n}\n' + paint)
