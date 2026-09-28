# Library details tests

Compile the full production activity/header and FreeInkUI, using a recording
display and deterministic input/lifecycle stubs. The production UITheme
orientation-safe area method is extracted unchanged at configure time. These
checks cover text reachability, layout bounds, UTF-8 line boundaries, pagination,
Back/Open and nothrow buffer failure. The Library UI suite separately exercises
the production menu, child creation and return callback.

This does not simulate the physical panel, font shaping, held-button timing,
ActivityManager scheduling or SD storage. Those remain covered by their existing
suites or require device use. No broad device-memory or visual-quality claim is
made from this recording target.
