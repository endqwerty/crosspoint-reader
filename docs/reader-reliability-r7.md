# Reader reliability (r7)

This iteration fixes refresh handoff, page-turn ordering, recovery after a failed
page read, and Library search. It builds on the RC02 source integration documented
in [r6](upstream-rc-r6.md); all six [r5 features](epub-library-r5.md) remain.
The intended image is `firmware-x4pro-epub-r7-final.bin`, version
`1.6.5rc02-x4pro-r7-6c83edd`. Follow `build/FLASH-LATEST.md` for the current package.

## Refresh and input

- The pull-down Refresh action records intent on the panel activity. Its `onExit()`
  promotes the destination frame to FULL while ActivityManager holds RenderLock,
  after the panel's final possible paint. A late panel repaint cannot consume the
  cleanup intended for the book. Normal dismissal does not request an extra FULL.
- The panel input loop holds the existing RenderLock around FreeInkApp routing and
  callbacks. Routing and rendering share event/interaction state, so they must be
  serialized. Callbacks do not acquire a second lock.
- The reader samples fresh direction input before draining its one pending turn.
  Latest direction still wins; the 200 ms guard and single-slot coalescing policy
  remain. Chapter skips, orientation changes and end-of-book actions discard an
  obsolete pending turn. This prevents stale turns; it does not raise the turn rate.

These changes follow the existing ActivityManager/UI locking and renderer refresh
promotion APIs. No SDK waveform, BUSY timeout or anti-aliasing default is changed.
The related upstream [panel refresh change, #3289](https://github.com/crosspoint-reader/crosspoint-reader/pull/3289)
is already part of the baseline. The delayed-refresh report in
[#3555](https://github.com/crosspoint-reader/crosspoint-reader/issues/3555) motivated
checking the destination-frame handoff; the host tests demonstrate a software race,
not a measurement or proof of every physical symptom in that report.

## Failed page reads

Before destroying a Section after a failed cached-page load, the reader retains
that Section's current target page in its existing resume fields. It clears stale
deferred positioning and the previous successfully rendered page's text offset.
Rebuilding therefore retries the failed target instead of the page originally
used to enter the section. The existing three-retry limit remains; unsuccessful
attempts do not save reading progress or alter link/footnote history.

This covers page-load recovery. Parser-wide allocation failure propagation and
transactional cache writes are separate work, not fixed by retaining the target.

## Whole-library search

The index's 96-byte folded title prefix remains the fast path. A miss near that
limit checks the full stored title, or filename stem when metadata is absent.
UTF-8 truncation can stop the prefix at byte 93, so the fallback includes that
case. Author and series matching remain available.

Combined text and shelf filters now test text before opening per-book state
files. In a 4,096-book synthetic fixture whose query matches one short title,
the production filter reads 4,096 index records but only one path hash and one
book-state file. An empty text query still checks every necessary shelf state.
This counts avoided state-file operations; SD wall-clock speed is not measured.
Read failures discard partial results and show the existing filter error.

The index format and 4,096-book limit are unchanged. The full stored title/name
blob is itself limited to 255 bytes; this does not promise matching text beyond
that persisted limit. Search and filtering still run synchronously in Library.

## Resource cost and verification

- Reader queue/recovery reuse existing fields and add no heap allocations.
- The temporary panel activity adds one boolean, potentially within existing
  structure padding. No resident frame buffer or background task is added.
- Library filtering reuses one local string for title/name/author/series blobs,
  reserving 255 bytes only after the first prefix miss. Existing HAL/index APIs
  require a string; a 256-byte stack buffer would duplicate storage and exceed the
  small-stack guideline. The scratch is released when filtering returns; existing
  Unicode-fold temporaries still apply. There is no new reader allocation.
- Find in Book remains explicitly invoked and releases search resources before
  reading resumes. Its ordinary-reading CPU/SD/heap contract is unchanged.

Host regressions execute extracted production methods rather than reimplementing
the decision logic. Manual refresh uses the real GfxRenderer with a fake HAL.
Coverage includes late panel paints, callback lock ownership, destination refresh
modes, fresh reverse input at guard expiry, special actions, failed-page retries,
search/anchor and footnote progress, UTF-8 title limits, and state-file read counts.
Negative controls reproduce the queue, recovery and refresh failures against r6.
The package records the full host gate, build, static checks and source/image hashes.

For device verification, leave anti-aliasing off as planned. Try pull-down Refresh
after several pages, quick next/back changes, sleep/wake, and Library search for
a late word in a long title with a Reading or Favorites filter. Check normal
touch/buttons and orientations. No recording is needed. Physical ghosting, actual
heap headroom and SD fault behavior still require the device; host counts do not
establish Kindle-equivalent latency or optical quality.

## Next candidates

1. Propagate parser allocation failures without silently dropping content.
2. Check cache writes and replace complete cache files transactionally on SD.
3. Make Library reconciliation cancellable with progress while preserving the old
   usable index; investigate stable book identity across external SD renames.
4. Add an actual EpdBus BUSY fault harness before changing timeout/recovery policy.

No unsafe writes after a BUSY timeout or speculative faster waveforms are proposed.
