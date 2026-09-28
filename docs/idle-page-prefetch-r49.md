# Idle page prefetch r49

All r48 improvements remain on develop `93e98bb` and SDK `111fdcc7`.

## Removed work

The idle reader loaded the next page, scanned and prepared its fonts, then exited
PrewarmScope. That scope clears caches on destruction; FontDecompressor frees
its page and hot-group buffers, SD fonts reset mini glyph data, and vector fonts
drop cached page glyphs. The foreground render creates its own preparation scope
and prepares its page again. The idle glyph preparation did not preserve those
glyphs for the next page turn.

The reader now loads and retains the decoded next page without the discarded
font scan/preparation. Foreground font preparation and rendering are unchanged.
SD-font persistent advance data and vector-font buffer capacities have different
lifetimes; this is not a claim that every font backend lost every side effect of
preparation. It eliminates transient glyph work, not a measured device percentage.

No new allocation, buffer or cache format is introduced. Existing page retention
still checks its page budget and free-heap thresholds after loading. The existing
render lock, debounce, UI/heap gates and one attempt per displayed page remain.
Active parsers stay excluded because Section rejects page retention during builds.
Library and page-loading improvements from prior releases remain included.

## Evidence and tests

Production evidence: EpubReaderActivity::loop and renderContents;
FontCacheManager::PrewarmScope destructor; FontDecompressor::clearCache;
SdCardFont::clearCache; TtfEpdFont::clearCache; Section::retainPrefetchedPage.

The host fixture compiles the actual reader idle block with platform doubles.
Across 100 idle opportunities on a completed section, font scan/preparation
requests drop from one to zero. Page reads and successful retention both stay
at one; there are no display refreshes or build chunks. Advancing to another
page allows one new prefetch. Four tests cover this behavior, lock/UI/debounce/
heap gates and exact boundaries, active builds and the end of a section.
Existing real cache tests cover ownership, memory limits and foreground reuse.
These counts establish removed calls and unchanged scheduling, not real-font
allocation sizes, elapsed time, panel ghosting or peak device heap.

## Upstream review

Review of Jan Steinke's draft PR #3675 prompted investigation of the existing idle
path. Its proposed additional image work and window-paused font preparation are
not imported. The final change removes redundant work from the existing reader.
https://github.com/crosspoint-reader/crosspoint-reader/pull/3675
Draft PR #3705 remains deferred because its own evidence identifies a cold-layout
regression with varying-width fonts. Develop was fetched and remains `93e98bb`.

## Device check

Use the single final X4 Pro image linked by build/FLASH-LATEST.md. With AA off,
pause briefly between page turns and confirm text and navigation remain normal.
No recordings or cache deletion are needed. Host checks do not establish a device
speed percentage or improved panel ghosting. No commits, pushes or PRs created.
