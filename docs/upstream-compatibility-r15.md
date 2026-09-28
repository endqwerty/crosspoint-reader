# r15: upstream refresh and Library group counts

The branch base is CrossPoint `develop` at
`1d61100f90d2e7e32965c14301320d9989a7ab71` (September 25, 2026 fetch), with
FreeInk SDK `111fdcc7f0176c3ee38391a160ee296bf492dbd8`. The seven new upstream
commits are retained as the actual Git baseline. Local changes remain uncommitted;
no remote publication is performed.

## Follow upstream decisions

- Adopt the common header layout, status band and Back arrow. Library Back routes
  through upstream's shared handler, with the existing author/series drill-down
  stages inside it. The blocks icon in the existing right-hand action slot opens
  Options; Refresh is available there. This preserves access to grouping and
  filters after the Back arrow takes the old options slot.
- Adopt Quick Resume's retained display polarity and FAST moon update, and the
  removal of the startup loading-icon paint. Retain the independent dark boot
  splash. No driver waveform or timing changes are introduced here.
- Adopt SD-font cache release on reader exit, Korean word-space justification and
  RTL book tap zones. Retain the checked parser/caching and refresh transactions.
- Keep the upstream glyph helper, ReaderUtils, mapped input, main startup and
  header target implementation. Avoid alternative header or typography designs.

Korean wrapping requires cached layouts to rebuild. The local release already
used version 48, so r15 uses complete version 49 / partial 233. The 43-byte header
and 24-byte BlockStyle do not change. Both old version 47 layouts and version 48
(including their partial sentinels) are rejected before decoding fields. Reading
positions, bookmarks and Library metadata retain their existing formats.

## Small Library addition

Names-only group rows show a count in FreeInkUI's existing right-hand value slot.
The count is the difference between adjacent group offsets, or the filtered total
for the last group. It therefore reflects Favorites/status/search filters and
reverse order without another index read. An eight-byte stack buffer formats the
number into the existing visible-row author string; no new persistent field,
container or proportional-to-library allocation is added. Ordinary book rows
keep their current display. All Library storage is released on entry to reading.

## Compatibility rule for subsequent work

1. Fetch and inspect `develop` and the SDK pin before touching overlapping areas.
2. Preserve a source manifest, patch and baseline before integration. Keep local
   features as adaptations to current upstream interfaces, not alternate cores.
3. Prefer the accepted upstream implementation. Retire duplicate local helpers,
   settings or behavior when they conflict; explicitly migrate saved settings and
   invalidate incompatible caches instead of silently reinterpreting them.
4. Preserve meaningful tests from the previous release and new upstream tests.
   A changed behavior needs a documented replacement assertion, not a skipped test.
5. Keep local features lazy and bounded. Test allocation/read/write failures and
   input/refresh state transitions. Keep hardware claims separate from host results.
6. Publish a single named BIN through `build/FLASH-LATEST.md` only after all gates
   pass against the same source. No commit, push or PR without user authorization.

## Verification

Native and LLVM22 ASan/UBSan tests cover count boundaries/filtering/4096 books,
shared Back stages, previous caches, night/day Quick Resume and activity header
handoff, alongside the retained reader, storage and driver tests. SDK display,
UI and font checks and the X4 Pro build are required. Final logs and the source
manifest are kept with the packaged BIN; this document is not itself evidence
that a gate passed.

On device, start with anti-aliasing off. Check forward/back reading, night-mode
Quick Resume and wake, dark boot, Library Back/Options, and filtered Author/Series
counts. No recordings are needed. Host checks do not establish panel ghosting,
physical SD latency, BUSY behavior or peak device heap.

## Remaining roadmap work

- On-demand full-title/path details for ambiguous names, using shared controls.
- Stable selection restoration after a full Library rebuild, with measured index
  I/O and a safe fallback when the selected path was removed or metadata changed.

These remain separate work: r15 does not claim to implement them. Upstream header
PR 3689 is now integrated rather than deferred; recheck the remaining typography
and metadata-sort proposals before extending those areas.
