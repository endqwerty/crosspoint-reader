# SdFat storage regressions

The normal CMake/CTest suite runs `SdFatCache0`, `SdFatCache1` and
`SdFatPatchHook`. The two binaries exercise shared and separate FAT caches using
real SdFat 2.3.1 from a hash-pinned archive, patched by the production hook in an
isolated build directory. Source bytes outside the three reviewed patches must
match the archive. `CROSSPOINT_TEST_SANITIZERS=ON` instruments both dependency and
harness; no sanitizer checks are suppressed.

Each binary checks 3,089 scenarios: partial-fill failure lengths 0–512 across
three access paths and two transport contracts, dirty write/mirror retry,
FAT16/FAT32 fragmentation, remounts, complete payload and FAT-copy verification,
exFAT, and thousand-book directory/name/payload enumeration. The block device is
simulated. Report printed sector counts as synthetic storage operations, not
physical elapsed time or page-turn speed. Host memory allocation is test-only.

The patch-hook tests exercise real patch application in temporary directories,
version/source validation, preflight failure, source/path containment, dependency
selection, SDK-only behavior and idempotence. Firmware uses its normal HAL/SDK
storage path; these tests do not prove electrical SD recovery or peak device heap.
