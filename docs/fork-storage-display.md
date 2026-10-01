# Storage and display reliability

This covers:

- storage failures reported through `HalStorage`;
- the patched SdFat FAT cache;
- display driver hardening with Direct-grayscale sleep covers;
- the always-dark boot splash.

None of these change a cache, settings, bookmark, Library index or reading-position format. Those formats are described in `docs/file-formats.md`.

## Storage failure handling

- **Checked allocation:** all four `HalFile::Impl` wrapper allocations use `makeUniqueNoThrow`, so running out of memory returns an empty handle instead of aborting. A missing read open, or the normal end of a directory, returns an empty handle and allocates nothing. Closing an empty or moved-from handle is safe and does not touch the SD card.
- **Writable opens reserve the wrapper first.** This applies to `O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND` opens and to `openFileForWrite`. A wrapper OOM therefore never creates or truncates the destination. The cost is that a failed write open may allocate one wrapper and free it again. Upstream PR #3419 opens first; this fork deliberately does not. Writes are still not transactional against other filesystem failures.
- **`HalFile::hasError()`** is true in two cases:
  - SdFat reported an error;
  - the directory-entry wrapper allocation in `openNextFile()` failed.

  The allocation failure is latched: it moves with the handle and survives `rewindDirectory()`. Only reopening the directory starts a clean scan. Check `hasError()` before closing the handle.
- **Consumers:**
  - The Library builder (`lib/LibraryIndex/LibraryBuilder.cpp`) rejects an interrupted scan. It keeps the committed index and removes staged files.
  - `FolderSearch` (`lib/FolderSearch/FolderSearch.h`) keeps the partial results it has and labels them incomplete.
  - `FileBrowserActivity` throws away an incomplete count pass or fill pass and marks the search row `STR_SEARCH_INCOMPLETE`. Reloading retries. The firmware picker is the same activity in `Mode::PickFirmware`, so it behaves the same way.
- **Memory:** the wrapper was already on the heap, because `HalFile` hides SDK types and moves across scopes. The only addition is one `bool` per wrapper, which alignment may pad. `~Impl()` closes the file while holding the recursive `StorageLock`.
- **Limits:**
  - Only wrapper OOM becomes recoverable. SDK and standard-library allocations can still fail in other ways.
  - `hasError()` sees only errors that SdFat reports. Some `openNext` failure paths never set an error on the parent, and corrupt FAT entries are not caught in general.
  - Other directory iterators have not been converted: web server, WebDAV, ClearCache, dictionary registry, sleep image picker and NextBookFinder.
  - If upstream picks a different API, migrate the callers and keep the failure tests.

## SdFat separate FAT cache

`-DUSE_SEPARATE_FAT_CACHE=1` in `platformio.ini` turns on SdFat 2.3.1's existing inline second sector cache for FAT lookups. Cluster chains then stay cached while directory and file sectors pass through the main cache.

- **Memory:** the extra cache is static storage inside `FatPartition`, one 512-byte sector. It adds no heap allocation and no background work.
- **Where it helps:** the gain is largest for directory enumeration and scattered small reads. It is smaller when chains span several FAT sectors. exFAT keeps its own policy and does not change.

`scripts/patch_sdfat.py` is a PlatformIO `post:` hook. It applies the patches in `scripts/sdfat_patches/`:

1. `0001` (`FsCache.cpp`): a failed `readSector()` invalidates the cache. Without this, a partial transfer can leave another sector's bytes under the old sector's identity. A later write-back could then corrupt both FAT copies. Dirty data has already been synced before the read, so nothing is lost. Dirty write-back failures keep the buffer so the write can be retried.
2. `0002` (`SdFatConfig.h`): adds an `#ifndef` guard. Without it, the architecture default (1 on ARM, 0 elsewhere) overrides the `-D` flag.
3. `0003` (`FatFile.cpp`): `readDirCache` passes a null output pointer, and advancing that pointer is undefined behaviour. The guard leaves it alone. This is local work and is not part of upstream's patch set.

Hook invariants:

- The version is pinned in two places: `greiman/SdFat @ 2.3.1` in `platformio.ini`, and `version=2.3.1` in `library.properties`.
- Every target file must hash to either the reviewed upstream bytes or the patched bytes. Anything else fails the build.
- The whole patch set is checked with `git apply --check` before any file changes, and each result hash is checked after applying. Files that are already patched are left as they are, so incremental builds do not recompile SdFat.
- The hook patches only the one selected SdFat, and only inside this project's `.pio/libdeps/<env>/`. It skips the PioArduino SDK-only pass. Git runs isolated from the enclosing repository.
- Upgrading SdFat means reviewing the patches and hashes again. Remove each patch once upstream SdFat fixes the same problem. See `scripts/sdfat_patches/README.md`.

Limits:

- The gain is measured only as fewer simulated block reads. For example, enumerating 1,000 books with a small read from each goes from 5,163 to 3,903 reads on FAT16 and from 3,969 to 2,967 on FAT32. This is not a measurement of load time or of panel behavior.
- On X4 Pro, a single-sector SDMMC read copies out of its DMA buffer only on success (`SdmmcBlockDevice.cpp`). The partial-fill corruption case therefore does not apply to X4 Pro. It still applies to SPI paths on other boards and to the general filesystem contract.
- SD failure recovery and peak heap have not been measured on a device.

## Display commit failures and BUSY latching

Active-high display waits report failure when BUSY stays asserted. The bus
latches that failure and suppresses later SPI command and data writes. Driver
and display state must not then certify the failed frame, a pending cleanup,
gray state or the shadow/baseline as synchronized. Recovery requires a
controller reset and initialization followed by a successful full frame. The
UC8179/UC8279 level-based wait behavior and waveform tables are unchanged.

The HAL and renderer return the driver's commit result, so the reader can keep
its cleanup request and refresh cadence until a complete page succeeds (see
`fork-reader.md`, "Display commit failures"). Host coverage: `test/epd_bus`,
`test/refresh_sequences`, `test/manual_refresh`.

## Display hardening and Direct sleep covers

The drivers live in the `freeink-sdk` submodule (`libs/display/FreeInkDisplay`). The pinned revision contains:

- the RC02 baseline (`2cca22fe`);
- Direct grayscale and `UltraChipDirectGrayLuts.h`;
- one local commit with the fork's display changes.

A handoff must include the nested SDK commits, not only the submodule pointer.

- **SSD1677 first paint after boot or wake:** the one-shot cleanup always runs, including when the sunlight fading fix powers the panel off after every refresh. A FAST first paint is promoted to HALF, or to FULL on boards without a HALF sequence. An explicit HALF or FULL simply consumes the one-shot. Ordinary FAST turns keep their waveform.
- **Sleep covers** use the best grayscale mode the driver supports, in this order: Direct (UC8179 and UC8279), then Absolute, then Overlay, then B/W HALF.
  - Direct sends both complete planes and then activates once.
  - Transparent white overlay pixels keep the existing background in both planes.
- **Direct with FULL:** an explicit or queued FULL is painted as a full-screen B/W cleanup before the Direct pass. That adds one activation.
  - HALF and FAST Direct passes still wait for both planes before activating.
  - The cleanup keeps the canvas pointer and its content, in both single- and dual-buffer builds.
- **Direct cancellation** rejects incomplete planes and requests a coherent B/W resync. Leaving a finished Direct image uses upstream's explicit redraw and cleanup of the destination. It is never treated as a fast text turn.
- **Text anti-aliasing is unchanged:**
  - Overlay text waveforms, the reader's refresh cadence and manual FULL promotion all stay the same.
  - The UC8179 dark-gray split and the UC8279 quality bank are never forced onto sparse text AA.
  - UC8279 keeps upstream's per-variant AA tables and image-pass resets.
  - Absolute and Direct image planes use the four-tone image bank. Overlay masks below the image threshold keep the text bank.
- **Window requests:**
  - UC8279 (`Uc8279X4Driver`) window requests take the default full-screen FAST fallback.
  - The facade also falls back to a full refresh whenever the display is inverted or an inversion is pending.
  - No UI depends on regional UC8279 refresh.
- The stock fast-DU shortcut stays off.
- **Memory:** no framebuffer or heap allocation is added, only small driver state fields and constant waveform data. The UC8279 quality bank is built at compile time (`constexpr makeQualityBank()`) instead of being a mutable runtime bank.
- **Limits:** the tests check controller payloads and activation counts. They do not measure ink motion, ghosting or latency.

## Always-dark boot splash

`BootActivity::onEnter()` (`src/activities/boot_sleep/BootActivity.cpp`) always shows light artwork on a mostly black background, whatever the theme or inherited polarity. This follows the dark sleep-screen convention. Under one `RenderLock` it:

1. sets normal output polarity;
2. draws the existing artwork;
3. inverts the framebuffer in place (`invertScreen()`);
4. submits once.

Other behavior:

- The night/light setting is not touched. `ActivityManager` applies it before the next activity renders.
- Silent restarts and splashless wakes keep their existing routing.
- The splash adds no heap allocation, framebuffer, settings write or background work.
- Normal e-ink transitions may still flash briefly.

## Tests

- **`test/hal_storage`**: production `HalStorage` built against fixed-size SDK and mutex stubs, using the real `Memory` helper with exceptions disabled. Covers:
  - wrapper OOM and ownership;
  - cleanup serialized under the lock;
  - telling the normal end of a directory apart from an error;
  - side effects and retries of write opens.
- **`test/library_builder`**: a read failure at each point of root and nested scans. It checks that the previous index stays byte-identical and that staged files are cleaned up, then retries.
- **`test/folder_search`, `test/file_browser`**: failed count and fill passes, and incomplete results.
- **`test/sdfat_cache`**: CTest entries `SdFatCache0`, `SdFatCache1` and `SdFatPatchHook`.
  - Builds the real hash-pinned SdFat with the production hook, in both cache modes.
  - Covers:
    - every partial-fill length from 0 to 512;
    - dirty primary and mirror retries;
    - FAT16 and FAT32 fragmentation;
    - remounts;
    - FAT-copy equality;
    - exFAT;
    - 1,000-book directories.
  - The only simulated hardware is a sparse block device and a clock that advances deterministically. A constant clock runs out of short-name aliases.
  - The host build undefines the unaligned-access macro so SdFat takes the same byte-decoding path as on ESP32.
  - Sanitizer runs use no suppressions.
- **`freeink-sdk/.../FreeInkDisplay/test/host/run_pro.py`**: the real facade and drivers in single- and dual-buffer builds. Covers:
  - wake cleanup with power-off;
  - Direct activation, cancellation, recovery and FULL cleanup order.

  The SDK's UC8253 and UC8279 X3 runners, and the upstream UC8279 waveform-selection test, guard the shared Direct and bank logic.
- **`test/refresh_sequences`**: production drivers running Direct cover, abort and B/W workloads, plus UC8279 window fallback on every variant.
- **`test/gfx_refresh`**: Direct manual FULL promotion, and transparent white preserved in both Direct planes in all four orientations. It starts from a decoded four-tone row and does not test BMP decoding.
- **`test/sleep_grayscale`**: sleep image paths in each supported grayscale mode.
- **`test/boot_splash`**: the complete production `onEnter`. Covers:
  - all orientations, polarities and themes;
  - one submission;
  - lock lifetime;
  - the next screen's theme.

  ActivityManager's polarity assignment is modeled; its task loop is not run.

## Attribution

Keep these `Co-Authored-By` lines when this work is committed or rebased.

- **Storage failure handling** adapts [CrossPoint PR #3419](https://github.com/crosspoint-reader/crosspoint-reader/pull/3419) at `f2faf88b77b0a3e4b5154b5090b1f6a059347d5c`. It was an open proposal when adopted, not an accepted upstream decision. Daviex (`david.iuffri94@hotmail.it`), verified from the PR commit records:
  `Co-Authored-By: Daviex <david.iuffri94@hotmail.it>`
- **SdFat cache:** the build flag, the cache override, the failed-fill invalidation and the patch hook are upstream's since [CrossPoint PR #3685](https://github.com/crosspoint-reader/crosspoint-reader/pull/3685) by Sung-jin Brian Hong (`serialx`) merged as `e9245489`. The fork carries only the directory-pointer guard (`0003`), `GIT_OPTIONAL_LOCKS=0` for the hook's `git apply`, the patch README and the host tests in `test/sdfat_cache`.
- **Display:**
  - [SDK #97](https://github.com/Free-Ink/freeink-sdk/pull/97): Eszter `<hello@eszter.xyz>`, commit `6644bf2fbf7525c36d5f86b7d0e880bd3a009b23`.
  - [SDK #98](https://github.com/Free-Ink/freeink-sdk/pull/98): Justin Mitchell `<justin@jmitch.com>`, commit `5916724f23f9392a1d75bc2f632780f54b0735fa`.
  - [CrossPoint #3541](https://github.com/crosspoint-reader/crosspoint-reader/pull/3541): Justin Mitchell `<justin@jmitch.com>`, commit `f64a6b2506d3e1bec2a58256e39e3e131e73f1b8`. This covers the Direct-cover, transparent-white and PNG quantization changes to the sleep cover.
  - [UC8279 variant AA restoration](https://github.com/Free-Ink/freeink-sdk/commit/2cca22fe44862215e029a416d5ff6fddcb3e593e): Justin Mitchell `<justin@jmitch.com>`, commit `2cca22fe44862215e029a416d5ff6fddcb3e593e`. Its image-bank selection, image-pass resets and waveform tests are kept.
  - [SDK #91](https://github.com/Free-Ink/freeink-sdk/pull/91): Mathias Karstaedt `<mathias.karstaedt@gmail.com>`, commit `adc731043b7cb8209c283f630edb861debf53305`. It was open when reviewed and was adapted with post-grayscale and outside-window validation.

  All of these authors were verified from the GitHub commit records. The SDK's local display commit already carries `Co-Authored-By` lines for Eszter, Justin Mitchell and Mathias Karstaedt.
