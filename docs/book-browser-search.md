# Book browser search build

This document records the original search/r3 handoff. The current r4 integration
also includes the full indexed Library, larger keyboard, and Always Next taps;
see [x4pro-library-ux.md](x4pro-library-ux.md) for its changes and verification.

This custom build adds **Search folders and files** as the first Browse Files
row. On the X4 Pro, tap it, enter prefixes of one or more filename words and
submit OK. It searches the open folder **and all its subfolders**, matching
folder names and supported filenames. Matching ignores ASCII case and Latin
accents. Results show their relative paths, so duplicate names remain distinct.
Tap a folder to browse it or a file to read it. Back cancels an in-progress scan
or clears completed results. Cancelling the keyboard preserves the current
query; entering a folder clears it. Empty input restores normal browsing.
Search does not inspect book contents or metadata, and a matching parent does
not automatically make all its children match. Search stays available with no
results. The firmware picker is unchanged.

## Existing work reviewed and reused

- [PR #2878](https://github.com/crosspoint-reader/crosspoint-reader/pull/2878):
  oreglio's original indexed Library implementation, split into smaller PRs.
- [PR #2885](https://github.com/crosspoint-reader/crosspoint-reader/pull/2885):
  the standalone text core, merged into the Library integration branch.
- [PR #3366](https://github.com/crosspoint-reader/crosspoint-reader/pull/3366):
  Uri Tauber's current Library integration. Its head at review was
  `9601f6f0ba9891ad5a6b7de13b19b418ae65322b`. It had passing X4 Pro CI and a
  community report of X4 Pro use. On September 10, 2026, its maintainer reported
  a bug when adding books and asked to hold merging. A fresh September 10
  check found the comment edited to **FIXED**, with the PR still OPEN at
  `d4411be90656522599f0839f0eb6048813f81ea8` and a published X4 Pro firmware
  artifact. This is now a near-complete upstream alternative. This build keeps
  the original pinned text core; it does not import the broader index or Library
  UI. See the [updated maintainer comment](https://github.com/crosspoint-reader/crosspoint-reader/pull/3366#issuecomment-5619913740).
- [CrossVi](https://github.com/tvhdc/crossvi): a broader CrossPoint-derived
  firmware advertising Library search; its documented target is X3/X4.

`lib/LibraryIndex/LibraryText.{h,cpp}`, its original host tests, and
`utf8DecomposedBase()` are reused from PR #3366's pinned head under the project's
MIT license. Original human authors verified in Git history: oreglio
(Aurelien Sorce) and Uri Tauber. The Browse Files integration and the
`FileBrowserSearch` tests are local additions. No upstream PR or fork was
modified or published.

## Memory and UI behavior

The query is limited by the existing keyboard to 48 UTF-8 bytes. A generic
`FolderSearch<HalFile>` walker reads through the HAL, holding only one directory
and one temporary entry handle. It queues relative directory paths without
recursive stack frames. Each input-loop pass performs at most eight steps,
checking elapsed time between steps and stopping the batch after 20 ms. An
individual SD call can still block. Back is checked before the next batch.
Traversal never holds the render lock. The UI shows a searching message, then
publishes the completed, sorted list under the lock and disables stale touch
routing. Results retain raw SD bytes for opening and deletion; NFC is applied
only to display copies.

To bound heap use, there are at most 256 results and 256 pending directories,
with independent 16 KiB limits on their stored path bytes. Full paths are
limited to 1,024 bytes, and the browser reuses its existing 500-byte heap name
buffer. Truncated or unreadable names are skipped. A pending limit skips that
subtree; a result limit stops the scan at the first excess match. Unreadable
directory opens or any limit produce **Partial results. Try a smaller folder.**
The HAL does not distinguish end-of-directory from an entry read failure, so
that particular SD error cannot be identified by this walker.

The two work vectors reserve their bounded capacity before traversal rather
than repeatedly growing. Heap storage avoids large stack arrays and a resident
index of the entire card. Their string objects plus path payload can take about
44 KiB during scanning on a 32-bit target; rendering also retains display names,
extensions and list rows. These bounds are not a guarantee against allocation
failure. Actual minimum free heap and largest free block still need measurement
on X4 Pro with 256 long results and wide/deep folder trees. The firmware picker
and ordinary directory listing retain their existing behavior and limits.

The Search row is indexed separately from results. Opening, deletion,
return-to-folder selection and the firmware picker account for that offset.
Layouts use existing theme and orientation-aware dimensions. Long relative
paths use the existing wrapped/truncated label treatment.

## Build and verification

The source repository lives on the NAS at
`/Volumes/workspace/projects/crosspoint-reader`, branch
`feature/book-browser-search`, based on upstream
`e5dcc64fd4f9cfefc33f1a0917ea1fb2a5ca183b`.

The build uses PlatformIO's `x4pro-gh_release` environment (ESP32-S3, 16 MB flash,
8 MB PSRAM, X4 Pro board profile). A local `platformio.local.ini` gives it the
version `1.6.0-x4pro-search-r3-e5dcc64` so it can be distinguished from upstream.
The local compiler workspace is
`/Users/danielyang/.local/share/crosspoint-build/source`. It is a copy of the NAS
source excluding macOS AppleDouble metadata (`._*`), Git internals and build
caches; those metadata files interfere with Git/package tools on this NAS.
The final build package includes the source patch and hashes of build inputs.

Commands, with the build virtual environment on PATH:

```sh
pio run -e x4pro-gh_release
pio check -e x4pro-gh_release --fail-on-defect low --fail-on-defect medium --fail-on-defect high
cmake -S test -B ../tests
cmake --build ../tests -j 8
ctest --test-dir ../tests --output-on-failure
```

All 262 host tests passed, including 21 recursive traversal tests. The full source static check reports two low-severity
style suggestions in unchanged WebDAV handlers; explicitly checking the imported
text/UTF-8 code adds five low-severity style/performance suggestions. Neither
check reports medium/high defects, and the Browse Files integration has none.
The strict low-severity commands exit nonzero; see the packaged logs. These
suggestions are not compiler errors.

Host tests verify text matching and the same incremental traversal used by firmware; the firmware build verifies integration with
real device headers and libraries. Physical display, touch and SD-card behavior
still need testing on the reader. After flashing, test a matching query, no
matches, cancellation, clearing, a nested folder, and opening the selected
result. Verify portrait and landscape layouts; serial heap measurements should
stay above 50 KB while repeatedly entering and leaving search. Search does not
change EPUB caches.

## Independent review (revision 2)

GPT-5.6 Sol independently reviewed the integration and confirmed one medium
severity issue: a long query in the list's trailing value slot could extend
outside the row and leave a negative width for the label. The query now uses
the full-width subtitle slot. A host probe against the actual FreeInkUI list
renderer reproduced the original overflow and verified bounded text rectangles
for the replacement at 480- and 800-pixel widths with 12- and 20-pixel glyph
metrics. This is geometry validation, not physical display validation.

No other introduced correctness/safety defect was found in the row offsets,
raw paths, deletion, firmware picker, cancellation, empty results, lifecycle or
locking. The main remaining gap is activity-level and physical-device testing
of touch/button interactions and large-directory heap use. The 241 host tests
exercise the matching code, not those hardware flows.

## Independent review (revision 3: recursive search)

GPT-5.6 Sol independently reviewed the walker, browser integration, HAL/SdFat
contracts and tests. It found one critical defect: closing a default `HalFile`
asserts. Every scanner close is now guarded by a valid-handle check. Sol
re-reviewed the fix and tests and found no additional definite defect. The host
fake enforces the explicit-close contract to catch a regression.

The 21 new tests cover traversal through nonmatching parents, folder matches,
relative paths and duplicate names, subtree scope, supported extensions,
Unicode/raw paths, hidden/system exclusions, one-entry steps, cancel/restart,
unreadable/missing directories, exact result-count boundaries, pending/result
byte caps, deep nesting with at most two handles, path/name truncation, unsafe
components and no matches. They do not execute the activity's touch/button
routing. The browser integration's strict static check reports no defects.
The X4 Pro release image is built separately; see FLASH.md for its verified
size, hash, version and hardware-validation limits.
