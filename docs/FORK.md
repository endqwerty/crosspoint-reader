# Fork instructions

This is the canonical location for persistent instructions specific to this fork.
Read it alongside the upstream `AGENTS.md`. Keep temporary task state, branch
names, release hashes and outstanding work in `WIP.md`.

## Precedence

1. The user's current instructions and explicit standing authorizations recorded
   below (automatic commits, local integration and personal `develop` pushes).
2. The official project: upstream `AGENTS.md`, its architecture, interfaces,
   conventions and maintainer decisions.
3. This file.

For firmware architecture and behavior, upstream always wins over the fork.
The recorded user authorizations satisfy generic requirements to obtain approval;
they do not waive engineering, validation or upstream-compatibility requirements.
The preferences below narrow where fork
effort goes; they never justify diverging from upstream's design. If a fork
preference conflicts with upstream, follow upstream and note the conflict in
`WIP.md`.

## Shared information

The user works in this repository with several agents (Codex, Claude Code and
Antigravity, through T3 Code). They share only files, not memory. Agent memory
(Claude project memory, Codex memories, Antigravity `brain/`) must never be the
only record of a rule, decision or finding. Record it here instead:

| Information | Location |
| --- | --- |
| Durable rules, preferences, standing authorizations | This file |
| Task state, handoff evidence, release hashes, next steps | `WIP.md` |
| Design, invariants, limits, attributions of fork features | `docs/fork-*.md` |
| Upstream conventions | `AGENTS.md` (upstream; only its link here is local) |
| Current firmware image and build evidence | `/Volumes/workspace/builds/crosspoint-reader/FLASH-LATEST.md` |
| Mac, T3 Code worktrees and cleanup, agent tooling, home server | `/Volumes/workspace/homelab/workstation.md` and `README.md` there |

When a user instruction changes how work is done, update this file in the same
task. When a fact belongs to the machine rather than the repository, update the
homelab page. If `/Volumes/workspace` is not mounted, say so instead of guessing.

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

The maintained checkout uses GitHub's fork layout in both repos: `origin` is the
personal fork and `upstream` the official project, with local `develop` tracking
`origin/develop`. `origin/HEAD` points to `origin/develop`, so tools that start
from the default branch, such as T3 Code worktrees, begin at the fork. Where
`AGENTS.md` says to push to `fork`, use `origin`. Fresh clones may name remotes
differently; always verify their URLs. `.gitmodules` points to the
SDK fork so the pinned local SDK commit is available to a recursive checkout:

```sh
git clone --recurse-submodules --branch develop https://github.com/endqwerty/crosspoint-reader.git
```

Keep backup history in verified bundles before removing obsolete branches. After
finishing a feature, integrate it into the maintained `develop` branch. Keep the
active worktree on its feature branch until the user deletes it; do not attempt
to check out `develop` there while the permanent checkout owns that branch.

## Starting a new worktree

- Start from the latest personal `develop`, never a retired worktree branch.
  Read this file and `WIP.md`, inspect `git worktree list`, status and remote
  URLs, then fetch personal and official `develop`. Resolve remote advances
  and update the linear upstream base before implementing the requested feature.
- Initialize the reader-pinned SDK with `git submodule update --init` if needed.
  Do not substitute the SDK's newest branch tip. Recursive icon submodules are
  needed only for icon generation, not ordinary firmware builds.
- Perform feature work in the isolated local worktree. Use the permanent
  checkout for final integration into `develop`, preserving unrelated changes.
  A shared local build mirror is usable only after verifying its source matches
  the intended worktree; never reuse another feature's outputs without checking.
- `WIP.md` separates completed handoff evidence from possible future work.
  Its roadmap is context, not an instruction to begin unrequested features.

## Local work and publication

- The user's standing instruction authorizes automatic local administration:
  commit completed, validated work, rebase as needed, and integrate it into
  local `develop` without asking again. This is explicit ongoing authorization
  for commits and local integration, including where generic agent guidance
  otherwise asks for a per-task commit request. Do not leave completed work
  only on a disposable worktree branch.
- Before integration, fetch official upstream `develop` and bring local
  `develop` up to date by rebasing the fork-only patch series above it. Keep
  upstream commits and their trees unchanged; all customizations must remain
  later commits. Never amend/squash upstream commits, interleave upstream with
  local patches, or create merge commits to update the fork. Follow the backup,
  conflict-resolution and validation rules above when rebasing.
- Integrate the completed worktree branch using fast-forward or squash after
  rebasing it onto the updated local `develop`. Preserve concurrent/unrelated
  work. Run relevant checks, export requested firmware/evidence, and update
  `WIP.md` before the final commit. Verify local `develop` contains the finished
  work, includes the fetched upstream tip, and has no fork-only merge commits.
- Finish with clean integrated source state and durable outputs so the user can
  simply delete the worktree. Report any genuine blocker instead of claiming
  unfinished work is complete. As the last step, after pushing, check and report:
  - `git status --short` is empty in the worktree and in `freeink-sdk`, and the
    submodule is at its pinned commit (`git submodule status` shows no `+`).
  - No temporary files or `platformio.local.ini` remain, and temporary git
    worktrees or scratch clones created for the task are removed.
  - Local `develop` and `origin/develop` contain the work.
- Leave the active worktree in place, still on its own `t3code/*` branch; do
  not delete or detach that branch. The user deletes the thread, and a launchd
  job then removes the worktree and branch (T3 Code's own cleanup cannot remove
  worktrees with a submodule; see the homelab `workstation.md`). Removing other
  retired worktrees by hand is unnecessary.
- Complete applicable host tests and firmware builds before integrating source
  changes. Record physical-device checks separately as pending when unavailable;
  do not claim device validation or block authorized local administration solely
  on that absence. Documentation-only changes require review and diff checks,
  not a firmware rebuild or device test.
- The user's standing authorization includes pushing completed `develop` to
  their personal fork. Verify remote URLs before every push; for this reader
  repository, push `develop` to `origin` only when it resolves to
  `https://github.com/endqwerty/crosspoint-reader.git` (or its SSH equivalent).
  Fetch the personal remote first and preserve concurrent remote work. Prefer
  a normal push; after a required upstream rebase, use an explicit
  `--force-with-lease` tied to the inspected remote tip, never unconditional
  force. Verify the remote `develop` tip matches local `develop` before handoff.
- This standing authorization does not cover pushes to official upstream,
  other branches, PR creation/closure, or release publication; those still
  require explicit user approval.
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
- Purpose and evidence: this is a personal project to improve the user's own
  reading on the X4 Pro. The goals so far, and reached in ordinary use, are
  fast and cheap page turns, less ghosting and a fast Library refresh. The user
  does not take manual device measurements (recorded 2026-09-29): do not ask
  for them, do not make them a step in a plan, and do not gate work on them.
  Judge changes by host tests, operation and allocation counts, static RAM and
  a clean X4 Pro build, and prefer changes whose benefit is deterministic over
  ones that trade latency against battery or heap. List device checks as
  unverified, never as validated.
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
- The maintained checkout lives on local disk at `~/workspace/crosspoint-reader`;
  T3 Code worktrees go under the local `~/.t3/worktrees`. Keep source and git
  data off the SMB share. Write exported firmware images, release packages and
  evidence to the share at `/Volumes/workspace/builds/crosspoint-reader/`, not
  to the repository's `build/`.
- All development work runs in isolated, disposable worktrees. Do not rely on
  a worktree surviving a handoff. Before deleting it, deliver the completed
  firmware and its build evidence to a new dated folder under the shared
  output directory above. PlatformIO normally writes to the worktree's local
  `.pio/build/x4pro-gh_release/`; it does not automatically export to SMB.
- For each firmware handoff, build `x4pro-gh_release`, inspect the resulting
  image, and copy `firmware.bin` with a descriptive filename, `SHA256SUMS`,
  source commit/SDK revision, version, build log and flash instructions.
  Re-read the copied image and verify its SHA-256, then update the shared
  `FLASH-LATEST.md` with a relative link and Windows path. Preserve previous
  builds. Windows opens `\\<server>\workspace\builds\crosspoint-reader`.
  Never store the only output under `workspace/projects/crosspoint-reader`;
  that shared source checkout is disposable. A successful compilation does
  not establish physical-device validation.
- Local toolchain: `pio`, `cmake` and `ctest` are in
  `~/.local/share/crosspoint-build/venv/bin/`, not on `PATH`. Host tests:
  `cmake -S test -B <dir> -DCMAKE_BUILD_TYPE=Release`, build, then
  `ctest --test-dir <dir> --output-on-failure --timeout 60 -j 8`. Sanitizer runs
  add `-DCROSSPOINT_TEST_SANITIZERS=ON` with Homebrew
  `/opt/homebrew/opt/llvm@22/bin/clang` and `clang++`. Keep test build
  directories under `~/.local/share/crosspoint-build/`. `./bin/clang-format-fix`
  finds the venv's `clang-format` when that `bin/` is first on `PATH`.
- Firmware version for a handoff: put `[crosspoint]` `version =
  1.6.5-dev-x4pro-rNN-<upstream short hash>` in a temporary
  `platformio.local.ini` (gitignored), build, and delete it afterwards.
- Review existing upstream changes before choosing a new implementation.
- Measure parser, storage, allocation and rendering work with meaningful fixtures.
  Report host operation counts separately from physical page-turn latency,
  ghosting and peak device heap. Never claim unmeasured hardware improvements.
- Run the relevant validation after source changes, including the target firmware
  build before recommending a new image. Preserve existing test coverage. Do not
  rebuild solely for documentation, commits, or history changes when source bytes
  remain identical to a validated build.
- Default firmware handoffs to the web flasher: Xteink X4 Pro → Custom .bin.
  `/Volumes/workspace/builds/crosspoint-reader/FLASH-LATEST.md` identifies the
  currently validated image. Historical
  filenames alone do not establish which image should be flashed.
- Do not start open-ended roadmap work from a handoff. Finish the user's requested
  scope and record concrete remaining work in `WIP.md`.

## Instruction-file maintenance

Keep upstream's `AGENTS.md` changes limited to the reference to this file. Add new
fork-specific preferences here rather than distributing them through upstream
instructions or release notes. Codex, Claude Code and Antigravity all load the
repository's `AGENTS.md` (verified 2026-09-29; versions and the global-file
layout are in the homelab `workstation.md`), so do not add a `CLAUDE.md` or
`GEMINI.md`. Recheck after a major agent update before relying on that.
