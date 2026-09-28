# TrueType adapter host checks

These tests link the production `TtfEpdFont`, `EpdFont`, and FreeInkFont/FreeType backend with the upstream DejaVu Sans fixture. The font target enables the same auto-hinter flag as firmware. It covers all four lazy styles, packed 1/2-bit bitmap contracts, cache clearing and release, size reload, streamed sources, and recovery when the adapter's heap preflight rejects growth. A separate target includes the real header without `BOARD_HAS_PSRAM` and asserts that vector-font support stays disabled.

Build `TtfEpdFontTest` and `VectorFontGateTest` in the normal host CMake tree, then run `ctest -R 'TtfEpdFontTest|VectorFontGate' --output-on-failure`. The ordinary sanitizer option instruments both the adapter and linked backend.

The SDK allocator uses its real host malloc implementation. Heap-capacity queries are controlled test values; the cache-manager stub does not invoke device eviction callbacks. This does not simulate PSRAM placement, concurrent allocation failures after preflight, ESP32 fragmentation, SD latency, or panel output. The streamed callback reads bounded fixture bytes from memory. Hardware validation is still needed for those behaviors.
