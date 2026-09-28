"""Compile the sleep entry routing prefix and complete Quick Resume paint method."""
import pathlib
import sys
source = pathlib.Path(sys.argv[1]).read_text()
start = source.index('void SleepActivity::onEnter() {')
end = source.index('  if (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::TRANSPARENT_CUSTOM)', start)
prefix = source[start:end]
assert 'renderLastScreenSleepScreen' in prefix and 'display.setInverted(false)' in prefix
start = source.index('void SleepActivity::renderLastScreenSleepScreen() const {')
end = source.index('\n}\n', start) + 3
pathlib.Path(sys.argv[2]).write_text(prefix + '\n  (void)frameWasInverted;\n}\n' + source[start:end])
