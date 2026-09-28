# r13: upstream compatibility and reading stability

Target: ESP32-S3 Xteink X4 Pro, `x4pro-gh_release`.
Version: `1.6.5-dev-x4pro-r13-6f94d1a`.
Intended application image: **`firmware-x4pro-epub-r13-final.bin`**.

## Integration policy

The base is CrossPoint `develop` commit `6f94d1ad5d84a81ec8a28c06b0afe01a7300fe75`, with its pinned FreeInk SDK `13418e0986b05039bf056e050a6df5305d47c209`. This imports 27 commits beyond the previously source-integrated `6c83edd` base. This is a pinned development build, not an upstream release candidate. No remote branch is changed.

The working branch and SDK base now advance to those commits; local features remain uncommitted on top. Earlier packages copied upstream source without advancing HEAD, so this update uses a preserved r12 source/index snapshot and a three-way source merge before updating the bases. The package includes the actual base hashes, binary source patches, complete source manifest, and recovery evidence.

Upstream owns public interfaces, platform support, and default behavior. Keep extensions small and removable; retain a departure only when a concrete reading or failure-recovery requirement justifies it and tests cover it. In particular, do not maintain private copies of whole upstream activities or custom panel waveforms without device evidence. Revisit every retained departure on the next integration. Future compatibility is a continuing validation requirement, not a guarantee this build can prove.

## Adopted upstream behavior

- File Browser uses upstream lazy `rowProvider` rows and bounded visible-row preparation, NFC filename composition, and its Rename menu. Recursive search and opening-key release handling extend that flow. Rename also preserves bookmark recovery sidecars and local favorite/reading state and invalidates the warm library session.
- Word and character spacing controls, Home button shortcuts, power/frontlight fixes, library refresh dirty markers, image scratch allocation consolidation, SD-font cache fixes, and the other changes in the pinned develop history are carried forward.
- Library arrival order uses upstream file modification timestamps, with stable first-discovery order for equal timestamps. The separate reading-history Recent tab remains an extension. Upstream compact author keys remain available; full author identity is a separate helper used for collision-safe grouping. The upstream recent-row identity lookup API is restored. Library schema remains 4 and its fold/sort revision becomes 5, forcing a safe metadata rebuild while preserving first-discovery history and reading positions. This first library refresh reparses EPUB metadata; subsequent unchanged scans reuse it.
- UC8179 uses upstream distinct light/dark gray waveforms. The custom overlay exception is retired. UC8279 X4 uses upstream whole-screen FAST fallback; its custom regional-window path is retired. Seventeen SDK files return byte-for-byte to upstream, including fourteen formatting-only differences.
- The optional SSD1677 fast-DU shortcut is already part of upstream and remains disabled in this release. There are no new waveform bytes or claimed optical improvements.

## Preserved reading extensions and compatibility fixes

Authors and Series continue to show names first, then matching books; Favorites, bounded recursive search, crash-safe bookmarks/progress, dark boot splash, and cached-page/library I/O optimizations remain. Find in Book still initializes only when opened. Its scan pump now defers upstream input actions and unwinds parser/framebuffer ownership before handling Home; it adds no work to active reading.

Upstream asynchronous overlay refresh needs an explicit settle point before an activity pushed onto the stack starts painting. A default no-op `Activity::onSuspend()` hook runs under the existing manager render lock; the EPUB reader settles pending refresh without reacquiring that lock. Failed completion invalidates the optimistic baseline and requests a full redraw. Overlay close also settles before restoring the saved page. The driver transaction and BUSY-failure protections remain independent of panel waveform selection.

Upstream and r12 both used section-cache version 47 for incompatible layouts. The combined layout is version **48** (partial sentinel **234**), with character and word spacing in the header and tracking in text blocks. Both old v47 variants are rejected and rebuilt automatically; saved progress/bookmarks remain separate. First opens after flashing can therefore take longer. No manual cache deletion is required.

Checked parser callbacks and cache writes remain fallible instead of silently recording incomplete pages. This is a deliberate failure-handling extension to upstream interfaces. Existing call sites retain that contract while accepting upstream spacing settings. Sleep-screen image rendering also checks grayscale capability and falls back to the decoded monochrome frame before any unsupported plane upload.

## Resource and measurement limits

The integration keeps the single-framebuffer design and HAL storage locking. It does not add an always-resident search index or reading background task. Rename recovery owns bounded temporary state only for the user-triggered rename; large state cannot safely live on the small task stack. Overlay suspension and the search Home latch require only control state, not a new framebuffer. The author identity helper runs while building library metadata, not on page turns. Upstream timestamp sorting needs a fallible temporary four-byte slot per indexed book and falls back to first-discovery ordering if allocation fails. The restored recent-row API uses a checked 4 KiB temporary read chunk only when called; it cannot safely fit on the small task stack and adds no persistent cache.

Cache tracking adds one byte per text block and two header bytes. In the retained host fixtures, page decoding uses 193 reads/2,616 bytes/59 allocations for prose and 198 reads/2,870 bytes/64 allocations for annotated content; retaining the decoded page adds no reload. These are deterministic host counts, not MCU timings or an optical benchmark. No claim of Kindle parity, reduced ghosting, hardware heap margin, or physical BUSY timing follows from these tests.

## Verification and future integration

Final test counts, build/image checks, and checksums are in the matching package `test-notes.md` and gate records. The native and LLVM 22 ASan/UBSan gates require the complete current upstream 356-test registry and r12 coverage, with reviewed upstream replacements only: thirteen NFC tests now run both string and in-place variants, and ditherer allocation failure now tests the single consolidated scratch allocation for each ditherer. Both suites run without filters. The exact unmodified upstream tree is also built and tested separately with the same native toolchain.

Future integrations should update the upstream reference registry, audit local API/cache/driver differences, rerun full host/sanitizer and SDK tests, then build the X4 Pro application image once after the final executable edit. Keep source manifests and patches tied to the real HEAD rather than reporting copied source as a Git rebase. Do not trade failure recovery for a smaller diff without equivalent protection in trunk.

For device verification, start with anti-aliasing off. Read forward/backward, open and close menus rapidly, use Home while Find in Book is scanning, reopen an old cached book, and browse Authors, Series, Recent, and Added in the large library. Rename a book and verify its bookmarks/favorite and refresh membership. Confirm the boot splash stays dark. No recordings are needed; report missing text, wrong positions, stalls, resets, or changed appearance. Physical ghosting and panel timing still require the user's device observations.
