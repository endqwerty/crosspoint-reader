# Bookmark persistence tests

This target compiles the complete production `BookmarkFile`, `BookmarkUtil`,
`PersistableStore`, and `AtomicFile` implementations against ArduinoJson 7.4.2,
matching the firmware dependency. CMake verifies the upstream source archive's
SHA-256 digest. JSON parsing and Arduino String serialization are not mocked.

A small host Arduino String adapter implements the API used by ArduinoJson's
real Arduino String writer, including a failing `concat` to model a short
serialization. HAL storage uses the same fault-injection fixture as the atomic
file tests. Password decoding alone is stubbed; these tests do not exercise
credential handling. A rejecting ArduinoJson allocator exercises overflowed
documents and parser allocation failures.

These are host persistence and serialization checks, not proof of physical SD
power-loss durability, FAT rename atomicity, or device heap availability.
