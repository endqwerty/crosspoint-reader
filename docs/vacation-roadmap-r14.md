# Reading UX work for the week of September 24, 2026

This is the local reading branch's work plan, not a change to the maintainers'
[roadmap](../ROADMAP.md) or [scope](../SCOPE.md). Upstream `develop` remains the
foundation. Prefer upstream implementations when work overlaps, and retire local
options rather than carrying a competing architecture. Changes stay local until
explicit publication approval.

## This week's release scope

| Order | Work | Acceptance criteria | Resource boundary |
| --- | --- | --- | --- |
| 1 | Integrate `develop` at `b88b653a797c9952c68df5bb373e560d65682f67` and its SDK pin `deb62ab7e02b8216c5ee86a0721bf99f985c6f23` | Adopt upstream footnotes, popup sliders, touch controls, font/WordStore APIs and accepted themes; retain checked cache writes, failed-refresh recovery, dark boot and offline Library features. | Retire the duplicate local bitmap helper and old Always Next setting; migrate saved mode 4 into upstream directional controls. No new waveform shortcuts. |
| 2 | Preserve Library context after Favorite/reading-state actions | Keep the current author/series and selected book; select a surviving neighbor if the book leaves the filter; return to a valid directory row when its last book disappears. Cover failed reads/writes/allocation, reverse order and 4,096 books. | Reuse the existing sorted-row map and move the existing heading; no added persistent field or index read. Skip refiltering for text-only searches and failed writes. |
| 3 | Reject invalid chapter destinations | A bad TOC spine index must not move the reader, discard its section/history, or mark the book finished. Cover picker, reader callback and toolbar boundaries, including an empty spine and valid first/last chapters. | Bounds checks only on explicit chapter selection; no allocation or active-reading work. |
| 4 | Distinguish unreadable bookmarks from an empty list | Missing/valid-empty storage still shows an empty list; corrupt/unreadable existing storage shows a translated error, allows Back, and does not rewrite the file. | One flag in the lazily opened bookmarks activity; no background work. |
| 5 | Validate and produce one flash candidate | Complete native and LLVM22 ASan/UBSan registries, SDK display/UI/font checks, upstream viewport tests, scoped static analysis and X4 Pro build/image checks. Compare prior and clean-upstream coverage explicitly. | Build from a source-verified local mirror; keep one intended final BIN and publish `build/FLASH-LATEST.md` only after verification. |

The integration is based on the upstream changes available on September 24. It
does not promise compatibility with future changes without another review.

## Why these items

- `LibraryListActivity::applyFilter()` clears group scope, which previously sent
  state-filtered author/series browsing back to the directory after an action.
  Restoring context uses row identity only while the index order is unchanged.
- Chapter destinations can contain values outside the spine. This also addresses
  the class of failure reported in [upstream issue 3457](https://github.com/crosspoint-reader/crosspoint-reader/issues/3457).
- The bookmarks screen previously reduced every failed load to an empty list,
  concealing corrupt or unreadable saved data.
- Upstream merged the glyph optimization in [PR 3633](https://github.com/crosspoint-reader/crosspoint-reader/pull/3633)
  and replaced combined touch modes in [PR 3586](https://github.com/crosspoint-reader/crosspoint-reader/pull/3586).
  Carrying their implementations reduces future integration cost.

## Next candidates, after this release

1. Implemented in r15: counts alongside names-only Authors/Series rows using
   existing group offsets, with no extra SD reads. The drill-down interaction stays.
2. Add an on-demand full-title/path detail view for ambiguous long filenames,
   reusing theme controls and a bounded, fallible buffer only while open.
   [Issue 1170](https://github.com/crosspoint-reader/crosspoint-reader/issues/1170)
   provides an upstream use case.
3. Preserve selection across a complete Library rebuild. This needs stable path
   identity and measured SD-read cost; ordinal identity is insufficient after
   reordering. Keep it separate from the state-action fix.

Avoid parallel implementations of metadata sorting while
[PR 3651](https://github.com/crosspoint-reader/crosspoint-reader/pull/3651)
(publisher `file-as` ordering) and
[PR 3707](https://github.com/crosspoint-reader/crosspoint-reader/pull/3707)
(language-specific title articles/search) remain under review. Recheck their status
before starting related work. The shared header work in PR 3689 is integrated in r15. Recheck PR 3695 before
starting other reader control redesigns. See [r15 compatibility](upstream-compatibility-r15.md).

## Verification when returning

Use the single BIN named by `build/FLASH-LATEST.md`, initially with anti-aliasing
off. Check ordinary forward/back reading, menu and footnote return, old-book cache
rebuilds, bookmark errors, Favorite/state actions within Authors and Series, dark
boot, and saved positions after sleep. No recordings are needed. Host tests cover
logic and failure handling; panel ghosting, actual SD latency, peak device heap and
physical input behavior still need device use.
