# Scope and preferences

Read this when choosing, designing or validating a feature or fix. The
always-read rules are in [../FORK.md](../FORK.md). These preferences narrow where
fork effort goes; they never justify diverging from upstream's design.

- Licensing (2026-10-08): this is a private personal fork with no other users or
  distribution. Do not spend effort on license files, notices or compliance
  tracking for adapted code (CrossInk is MIT). Still add the original human
  author as `Co-Authored-By` when a commit adapts their code, as
  `.agents/rules/git-workflow.md` requires.
- Personal reader font (2026-10-06): embed Libron and enforce it over saved
  built-in, SD bitmap and vector font selections. Keep the selected font size
  at the nearest supported built-in size; preserve other typography settings.
  Font controls show the enforced family as Reader Serif, honoring the source
  font's reserved-name license. See
  [the font policy](../fork-libron-font.md) for provenance and resource details.
- Device scope (2026-10-06): focus on offline EPUB reading for Xteink X4 Pro
  (`x4pro-gh_release`), the only device the user owns or tests. Other devices
  are not feature, optimization or validation targets for this personal fork.
  Check them only when necessary to maintain upstream compatibility, such as
  a changed HAL interface, platform-specific code or shared build configuration;
  record the specific reason and use the smallest relevant set of checks.
  Shared code must retain upstream interfaces and respect C3 memory constraints
  where needed for compatibility. A shared change alone does not require all
  boards to build or work, and unrelated other-device failures do not block
  X4 Pro delivery.
- Remote file loading (web server upload/WebDAV, OPDS, Calibre wireless) is
  unused. Do not remove, hide or compile it out, because that would diverge from
  upstream. Do not invest in it, prioritize upstream PRs that only touch it, or
  use it in validation plans. Local changes must not break it.
- Purpose and evidence: this is a personal project to improve the user's own
  reading on the X4 Pro. The goals so far, and reached in ordinary use, are
  fast and cheap page turns, less ghosting and a fast Library refresh. The user
  does not take manual device measurements (recorded 2026-09-29): do not ask
  for them, do not make them a step in a plan, and do not gate work on them.
  Judge changes by host tests, operation and allocation counts, static RAM and
  a clean X4 Pro build, and prefer changes whose benefit is deterministic over
  ones that trade latency against battery or heap. List device checks as
  unverified, never as validated.
- The user declined extra reading-display features (2026-09-29): time-left
  estimates, whole-book page estimates and additional indentation/estimate UI.
  Do not propose them again without a new request. Chapter-boundary prefetch,
  idle-power changes, extra series-menu UI and UUID-less relinking remain
  conditional on a reported need; an autonomous backlog review does not
  activate them. Upstream watch conditions are tracked in
  [#13](https://github.com/endqwerty/crosspoint-reader/issues/13).
- Library workflow: the user keeps their full Calibre library on the SD card.
  They export from Calibre ("Save to disk") on a computer and copy the files
  with an SD card reader. USB transfer from the device is much slower, so don't
  suggest it. The Calibre export holds about 750 books, one folder per book
  (`Author/Title/file.epub`), so features that assume sibling files in a folder
  do not work for this library. Prioritize work that makes
  this dependable: adding, removing or renaming files externally, re-exported
  files, and keeping reading state across re-exports. The supported copy step is
  `scripts/sync-calibre-library.sh`; update it rather than documenting another
  procedure.

## Porting from CrossInk

The remote `crossink` (`https://github.com/uxjulia/CrossInk.git`, fetch
only, created by `scripts/fork-workflow.sh setup`) is a feature source, never a
base: CrossInk is a single-maintainer fork of CrossPoint that diverges by
roughly 130k lines. Read its code with `git show crossink/main:<path>` and port
wanted behavior as small patches on this `develop`. What was evaluated, ported
and declined is in [fork-crossink.md](../fork-crossink.md); update it with every
CrossInk-derived change and re-check its release notes when syncing.
