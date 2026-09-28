# Fork instructions

This is the canonical location for persistent instructions specific to this fork.
Read it alongside the upstream `AGENTS.md`. Keep temporary task state, branch
names, release hashes and outstanding work in `WIP.md`.

## Precedence

1. The user's current instructions for the task at hand.
2. The official project: upstream `AGENTS.md`, its architecture, interfaces,
   conventions and maintainer decisions.
3. This file.

Upstream always wins over the fork. The preferences below narrow where fork
effort goes; they never justify diverging from upstream's design. If a fork
preference conflicts with upstream, follow upstream and note the conflict in
`WIP.md`.

## Upstream first

Treat the original project as the authoritative, more stable implementation. The
fork is a linear series of local patches on top of upstream `develop`.

- Keep upstream history intact as the base. All fork-only commits must follow
  that base; never interleave upstream updates with local changes through merge
  commits. Upstream's own historical merges remain untouched.
- Fetch the official upstream remote and rebase the local patch series onto its
  updated `develop`. Verify remote URLs rather than assuming a remote's role
  from its name. Never merge upstream into the customized branch.
- Integrate local feature branches with fast-forward or squash. Rebase diverged
  features first; do not introduce local merge commits or use `--rebase-merges`
  to preserve an interwoven fork history.
- Preserve upstream architecture, public interfaces, and behavior when resolving
  conflicts. Adapt or drop local changes that conflict with upstream's design;
  remove patches that upstream has superseded. Do not retain an older local
  implementation at the expense of upstream compatibility.
- Before rewriting existing local history, create a backup ref. Flatten legacy
  local merges into a reviewed patch series above the upstream base, preserving
  intended changes and human authorship. Revalidate the resulting source using
  the relevant tests and firmware target when source changes.
- Rebasing published commits rewrites their IDs. Existing push-approval rules
  still apply; when an authorized update requires a force push, use
  `--force-with-lease`, never an unconditional force push.

## Repository setup

Both personal forks use a single `develop` branch:

- Reader fork: `https://github.com/endqwerty/crosspoint-reader.git`.
  Its upstream base is `develop` in
  `https://github.com/crosspoint-reader/crosspoint-reader.git`.
- SDK fork: `https://github.com/endqwerty/freeink-sdk.git`.
  Keep its local patch above the SDK revision pinned by the official reader's
  `develop`, from `https://github.com/Free-Ink/freeink-sdk.git`. A newer SDK `main`
  does not automatically replace the reader's tested dependency revision.

The maintained checkout uses `origin` for each official upstream and `fork` for
its personal fork, with local `develop` tracking `fork/develop`. Fresh clones may
name remotes differently; always verify their URLs. `.gitmodules` points to the
SDK fork so the pinned local SDK commit is available to a recursive checkout:

```sh
git clone --recurse-submodules --branch develop https://github.com/endqwerty/crosspoint-reader.git
```

Keep backup history in verified bundles before removing obsolete branches. After
finishing a feature, return to the single maintained `develop` branch; temporary
feature branches may be used during active development.

## Local work and publication

- Commit only when the user requests it. A commit request does not authorize a
  push, PR, release publication, or history rewrite.
- Push to a personal fork or open/close a PR only with explicit user approval.
  Verify remotes before pushing; use `fork` unless the user specifies otherwise.
  Never infer authorization to publish from a previous task's permission.
- Preserve human authorship when adapting patches; do not add assistant
  attribution to commits. Follow the author-verification rules in `AGENTS.md`.
- After an upstream update, confirm `AGENTS.md` still directs agents here and
  review this file for rules or patches superseded by upstream. File placement
  alone does not enforce linear history.

## Development and validation preferences

- Focus on offline EPUB reading for Xteink X4 Pro (`x4pro-gh_release`). The X4 Pro
  is the only device the user owns or tests. Other targets still have to build,
  and shared changes must respect C3 memory limits and upstream HAL interfaces.
  Do not spend effort optimizing or validating other devices beyond that.
- Remote file loading (web server upload/WebDAV, OPDS, Calibre wireless) is
  unused. Do not remove, hide or compile it out, because that would diverge from
  upstream. Do not invest in it, prioritize upstream PRs that only touch it, or
  use it in validation plans. Local changes must not break it.
- Library workflow: the user keeps their full Calibre library on the SD card.
  They export from Calibre ("Save to disk") on a computer and copy the files
  with an SD card reader. USB transfer from the device is much slower, so don't
  suggest it. The library has about 400-500 books. Prioritize work that makes
  this dependable: adding, removing or renaming files externally, re-exported
  files, and keeping reading state across re-exports. The supported copy step is
  `scripts/sync-calibre-library.sh`; update it rather than documenting another
  procedure.
- The maintained checkout lives on an SMB share where macOS creates `._*` files.
  Ignore them through `.git/info/exclude`, not upstream's `.gitignore`.
- Review existing upstream changes before choosing a new implementation.
- Measure parser, storage, allocation and rendering work with meaningful fixtures.
  Report host operation counts separately from physical page-turn latency,
  ghosting and peak device heap. Never claim unmeasured hardware improvements.
- Run the relevant validation after source changes, including the target firmware
  build before recommending a new image. Preserve existing test coverage. Do not
  rebuild solely for documentation, commits, or history changes when source bytes
  remain identical to a validated build.
- Default firmware handoffs to the web flasher: Xteink X4 Pro → Custom .bin.
  `build/FLASH-LATEST.md` identifies the currently validated image. Historical
  filenames alone do not establish which image should be flashed.
- Do not start open-ended roadmap work from a handoff. Finish the user's requested
  scope and record concrete remaining work in `WIP.md`.

## Instruction-file maintenance

Keep upstream's `AGENTS.md` changes limited to the reference to this file. Add new
fork-specific preferences here rather than distributing them through upstream
instructions or release notes. A duplicate `CLAUDE.md` is unnecessary for the
verified Claude Code versions with native `AGENTS.md` support; check the installed
version and instruction-loading settings before assuming that for other setups.
