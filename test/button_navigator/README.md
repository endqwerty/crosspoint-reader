# Button navigation regression tests

Compiles the full production ButtonNavigator with deterministic mapped input and
a host allocation hook. The hook has an explicit-allocation positive control.
For Clang, `-fno-assume-sane-operator-new -fno-builtin` keeps the hook observable:
otherwise libc++'s vector allocation can disappear from optimized baseline builds.
The code is still built with the suite's Release optimization and sanitizers.

Idle polling and temporary caller lists must make zero allocations with the
small callbacks used here. This is not a claim that arbitrary std::function
captures cannot allocate, nor a measurement of device timing or fragmentation.
The release verification compares the same idle test against the previous
production implementation and records its expected failure and allocation counts.

Behavior tests retain logical direction, repeat thresholds, release suppression,
clock wrap, empty/multiple lists and returned-list lifetime.
