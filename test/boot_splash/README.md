# Boot splash regression

BootSplashProduction.cpp is generated from the complete production onEnter
method, with a configure dependency that fails if its marker moves. The fixture
reuses GfxRefreshHost and real logo, Ubuntu UI and Noto Sans small font data.
Only activity entry/lock, translated strings, version text and the HAL endpoint
are replaced. The HAL models an opaque MSB-first one-bit image blit and records output polarity at submission; the assertions
count physical white pixels after applying that recorded polarity.

32 cases cover both inherited polarities and selected themes in all four
orientations, the one-submission contract, lock release, mostly-black artwork,
and the next screen following its own selected theme. The transition models
ActivityManager's current polarity assignment; it does not execute its task
loop. Hardware refresh timing and transient waveform flashing are not measured.
