# RC02 integration (r6)

This release adds the upstream changes through `6c83eddbf3feeb375cef20fe702f7c99e5d38703`
and FreeInk SDK `2cca22fe44862215e029a416d5ff6fddcb3e593e` to the existing r5 work.
It is a source integration, not a Git rebase. The original branch, HEAD and staging
state are preserved; the release bundle includes reconstructable cumulative patches.

The [maintainer RC02 announcement](https://github.com/crosspoint-reader/crosspoint-reader/discussions/3544#discussioncomment-18440222)
lists the six commits through that source revision. The `1.6.5rc` tag still points
to RC01; its republished X4 Pro asset contains no source hash. The source choice is
inferred from the maintainer changelog and verified by our own build. At inspection,
the official nightly uses `f64ba736`, before the confirmed UC8279 anti-aliasing fix.
See the packaged `verification/upstream-choice.md` for exact release evidence.

## Integrated behavior

- Single ownership of rendered EPUB blocks, keeping bounded page prefetch and
  layout-cache accounting. Section cache version 46 rebuilds layouts for upstream
  hidden-content/list changes while preserving reading positions.
- Native chapter-parser behavior and search offsets agree on hidden text and
  generated list markers. Find in Book retains its activity-only resource lifetime.
- Updated SDK list layout/queued selection APIs, measured page navigation,
  first/last wrap, held-tab switching and Back from groups.
- RC02 UC8279 variant text-AA waveform restoration, Absolute image-bank selection
  and lifecycle resets, alongside our full-refresh and regional-state guards.
- Upstream bookmark renaming, EPUB/CSS parsing corrections, SD font improvements,
  progressive JPEG scan correction, settings and other compatible upstream fixes.
  Additional connectivity and media workflows are not customized here.
- The upstream 512-byte title/author/language bound retains complete UTF-8 characters.
  Three fixed boolean fields prevent later XML callbacks from resuming a truncated
  field; there are no new allocations for this guard. Oversize and chunk-boundary
  regressions exercise the real parser.

## Deliberate local adaptations

The Library remains Recent / Added / Title / Author-or-Series, retaining schema 4,
Favorites/status, safe rebuild fallback, fresh metadata and migration coverage.
Added keeps persistent first-discovery order. Upstream merges it into Recent using
FAT modification time, which can be an old source timestamp preserved by manual
SD copies; that does not represent when this Library acquired the book. The existing
Recent tab records opened books without needing another full-index lookup API or
sort-time timestamp allocation. Both choices preserve the existing user-visible
meaning and all six approved r5 improvements.

Our display refresh guards remain; synthetic tests establish software ordering and
state recovery, not physical ghosting or latency. Existing anti-aliasing settings
are preserved. No manual recordings are required. Test normal page turns with AA
off, a pulldown refresh, sleep/wake and Library navigation on the device.

## Historical r6 image

The r6 image was `firmware-x4pro-epub-r6-final.bin` in
`build/x4pro-epub-r6/`, version `1.6.5rc02-x4pro-r6-6c83edd`.
The package records actual host-test results, the X4 Pro ESP32-S3 release build,
static checks, source hashes and image validation. `build/FLASH-LATEST.md` is changed
only after all release checks succeed. See the included r5 feature documents for
resource limits and prior human adaptation attribution. For the current image,
follow `build/FLASH-LATEST.md` or the [r7 handoff](reader-reliability-r7.md).
