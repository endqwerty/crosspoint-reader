# Fork instructions

This is the canonical location for persistent instructions specific to this fork.
Read it alongside the upstream `AGENTS.md`. Keep temporary task state, branch
names, release hashes and outstanding work in `WIP.md`.

## Precedence

1. The user's current instructions and explicit standing authorizations recorded
   below (automatic commits, task-branch pushes and pull requests on the personal
   fork, and upstream syncs of the personal `develop`).
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

## Second opinion from another model

On the user's Mac, Claude Code sessions have two user-level subagents that hand
work to the Codex CLI (a ChatGPT model): `codex-reviewer` (read-only review,
`codex exec -s read-only`) and `codex-implementer` (`-s workspace-write`; the
caller reviews the diff). They live in `~/workspace/agent-config/agents/`, which
is the single source for shared agent configuration (see
`/Volumes/workspace/homelab/workstation.md`), not in this repository, so they
exist only on that Mac.

- Use `codex-reviewer` on a branch's diff before opening a pull request when the
  change touches memory handling, rendering or shared code, and treat its
  findings as claims to verify against the code. It does not replace
  `scripts/fork-workflow.sh check` or the device tests in `AGENTS.md`.
- Everything above about branches, pull requests and not merging still applies
  to work done through `codex-implementer`.

## Upstream first

Treat the original project as the authoritative, more stable implementation. The
fork is a linear series of local patches on top of upstream `develop`.

- Keep upstream history intact as the base. All fork-only commits must follow
  that base; never interleave upstream updates with local changes through merge
  commits. Upstream's own historical merges remain untouched.
- Fetch the official project (remote `official`) and rebase the local patch
  series onto its updated `develop`. Verify remote URLs rather than assuming a
  remote's role from its name. Never merge upstream into the customized branch.
- Task branches reach `develop` only as squash (or rebase) merges of pull
  requests on the personal fork; see "Branches and pull requests". Do not
  introduce local merge commits or use `--rebase-merges` to preserve an
  interwoven fork history.
- Preserve upstream architecture, public interfaces, and behavior when resolving
  conflicts. Adapt or drop local changes that conflict with upstream's design;
  remove patches that upstream has superseded. Do not retain an older local
  implementation at the expense of upstream compatibility.
- Before rewriting existing local history, create a backup ref. Flatten legacy
  local merges into a reviewed patch series above the upstream base, preserving
  intended changes and human authorship. Revalidate the resulting source using
  the relevant tests and firmware target when source changes.
- Rebasing published commits rewrites their IDs. When an authorized update
  requires a force push, use `--force-with-lease`, never an unconditional force
  push.

## Repository setup

Both personal forks use a single long-lived `develop` branch:

- Reader fork: `https://github.com/endqwerty/crosspoint-reader.git`.
  Its upstream base is `develop` in
  `https://github.com/crosspoint-reader/crosspoint-reader.git`.
- SDK fork: `https://github.com/endqwerty/freeink-sdk.git`.
  Keep its local patch above the SDK revision pinned by the official reader's
  `develop`, from `https://github.com/Free-Ink/freeink-sdk.git`. A newer SDK `main`
  does not automatically replace the reader's tested dependency revision.

The fork is the project. In the maintained checkout
(`~/workspace/crosspoint-reader`) `origin` is the personal fork, local `develop`
tracks `origin/develop`, and `origin/HEAD` points to `origin/develop`, so T3
Code starts each thread's worktree from the fork's `develop`. The official
project is the remote `official`, fetch only (its push URL is disabled). It is
deliberately not called `upstream`: T3 Code treats a remote of that name as the
project's own repository and then looks for, opens and merges pull requests
there. "Upstream" in this file means the official project, not a remote name.
Where `AGENTS.md` says to push to `fork`, use `origin`. The SDK submodule's
checkout still names its official remote `upstream`. `.gitmodules` points to
the SDK fork so the pinned local SDK commit is available to a recursive checkout:

```sh
git clone --recurse-submodules --branch develop https://github.com/endqwerty/crosspoint-reader.git
git remote add official https://github.com/crosspoint-reader/crosspoint-reader.git
scripts/fork-workflow.sh setup
```

`setup` is needed once per clone (worktrees share it). It makes the fork `gh`'s
default repository, because `gh`, and T3 Code's own "create pull request"
action, otherwise resolve a fork to its parent. It also disables pushing to
`official` and enables `rerere`, pruning fetches and `origin` as the push
default.

The fork's GitHub settings (set 2026-09-30 and 2026-10-01): merge commits
disabled, squash and rebase merges allowed, squash commits take the pull
request's title and body, head branches are deleted on merge, and GitHub
Actions is disabled. The user does not want to spend CI minutes; this machine
is the CI (see "Local checks").

`t3.json` at the repository root is T3 Code's project file. It makes new threads
use worktrees and leaves the SDK submodule unpopulated in them
(`worktreeSubmodules: "none"`), because git cannot remove a worktree that has a
submodule checked out (see "Worktree lifecycle").

Do not keep bundle or other off-repository backups of the fork's history; the
user does not want them (2026-10-01). The backup ref that `sync-publish` leaves
under `refs/fork-backup/` is enough.

## Branches and pull requests

```text
official/develop          official project (upstream), fetch only
      |  periodic rebase, force-push with lease, no pull request
      v
origin/develop            downstream integration branch
      ^
      |  pull requests, squash-merged
      +-- t3code/<slug>   one per T3 Code thread, in its own worktree
      +-- feature/<x>, fix/<x>, docs/<x>, refactor/<x>, codex/<x>
```

- `develop` is the integration branch, not a place to work. Never commit task
  work on it, never push task commits to `origin/develop`, and never move local
  `develop` to a task branch. It changes in exactly two ways: a merged pull
  request, or an upstream sync.
- Every task happens on a short-lived branch created from `origin/develop`. A
  T3 Code thread already has one: its worktree under `~/.t3/worktrees` is on
  `t3code/<slug>`, created from `origin/develop`. T3 Code renames it once, right
  after the thread's first message (`t3code/b3eaa40d` became
  `t3code/next-wip-pr-handling`, 2026-10-01), so read the name with `git branch
  --show-current` instead of remembering the one from the start. After that,
  keep the branch and its name for the whole thread (T3 Code tracks the thread
  by it). Branches made by hand
  use `feature/`, `fix/`, `docs/`, `refactor/` or `codex/`. The permanent
  checkout stays on `develop`; do not check out `develop` in a worktree.
- Standing authorization: commit completed, validated work on the task branch,
  push that branch to `origin` and open a pull request against the fork's
  `develop`, without asking each time. Do not leave completed work only in a
  disposable worktree.
- Open the pull request with `scripts/fork-workflow.sh pr --title "<type>:
  <summary>" --body-file <file>`. It refuses to run on `develop`, with
  uncommitted changes, on a branch that is not based on the current
  `origin/develop` or contains merge commits, and it always names the fork as
  the base repository. Never open a pull request against the official project
  without explicit approval.
- The squash commit is the pull request's title and body. Write the title as
  the commit subject (`AGENTS.md` format) and the body as the
  commit message, ending with any `Co-Authored-By` lines for adapted work. Do
  not add assistant attribution or a generated-by line.
- When T3 Code's `link_pull_request` tool is available, link every pull request
  to the thread; T3 Code then shows it beside the thread and settles the thread
  when it merges.
- The user merges pull requests (T3 Code or GitHub), with squash. Do not merge
  one unless the user says so in that thread. A pull request whose commits
  should stay separate can be rebase-merged instead.
- After the merge GitHub deletes the remote branch, the thread settles and the
  user archives it. The worktree is T3 Code's to remove; see "Worktree
  lifecycle". Never delete a thread's local branch: T3 Code recreates the
  worktree from it when the thread is resumed.
- SDK changes are not reviewed through pull requests. Commit them on the SDK
  fork's `develop`, push that (with a lease after an SDK rebase), and let the
  reader pull request carry the new submodule commit, which must already be on
  the SDK fork so CI and other checkouts can fetch it.

### Local checks

GitHub Actions is off for the fork, so the checks upstream's CI would run are
run here before a pull request is opened or updated:

- `scripts/fork-workflow.sh check`: formatting (the whole tree through
  `bin/clang-format-fix`, then no diff), host tests in Release and under the
  sanitizers, and the `x4pro-gh_release` firmware build. Use it for every change
  to source, tests or build files.
- `check --full` adds `pio check` (cppcheck) and the five firmware targets CI
  builds (`default`, `sticky`, `x4pro`, `x4c`, `papermono`). Use it after an
  upstream sync that changed source, and for changes to shared code that other
  boards compile.
- `check --fast` is formatting and Release host tests only.
- Documentation-only changes need review and a diff check, not `check`.

It keeps its build directories and logs outside the worktree, under
`~/.local/share/crosspoint-build/ci/<worktree>/`, and reports each step as ok
or FAILED with the log to read. Put the result (commit checked, which mode,
test counts, RAM and flash lines) in the pull request body: it is the only
record of the checks. The pull request title is the squash commit's subject, so
it must follow the `AGENTS.md` commit format; nothing checks that automatically.

### Worktree lifecycle

T3 Code owns the worktrees. It removes one by itself when its thread is
deleted, when the thread has been idle for the configured number of days, or
when the worktree's HEAD is already contained in `origin/develop`; it recreates
the worktree from the thread's branch when the thread is used again. There is
no other cleanup job. T3 Code only removes a worktree that is on its thread's
branch, has no uncommitted or untracked files, has no ignored files (build
output) and has no submodule checked out, so:

- Run `scripts/fork-workflow.sh prepare` before building or testing. It checks
  out the pinned SDK, borrowing objects from the permanent checkout.
- Run `scripts/fork-workflow.sh release` as the last step of every handoff,
  after everything is committed and pushed. It refuses to drop SDK work that is
  not pushed, then removes the SDK checkout, all ignored files
  (`.pio`, generated headers, `platformio.local.ini`) and the worktree's check
  directories, and says whether T3 Code can now remove the worktree. Copy any
  firmware image to the share first. The next `prepare` and `check` rebuild
  what was dropped.
- Do not remove a worktree or its branch by hand, and do not leave the worktree
  on another branch.

After a squash merge the branch tip is not contained in `origin/develop`, so
the merged thread's worktree goes when the thread is deleted or has been idle
for the configured days, not at the moment of the merge. T3 Code's settings and
their limits are in the homelab `workstation.md`.

### Syncing with upstream

The agent does this as part of resolving work, without a pull request: at the
start of a task, and again before opening or updating a pull request if upstream
moved meanwhile. `scripts/fork-workflow.sh status` shows whether upstream is
ahead.

1. In the task worktree, `git fetch official` and `git rebase official/develop`.
   Before the task has commits of its own, the branch is `develop`, so this
   rebases the downstream series; with task commits, they ride on top of it.
   Resolve conflicts by the "Upstream first" rules.
2. Validate the rebased series with `scripts/fork-workflow.sh check` (`--full`
   when upstream changed source), unless upstream changed only files the build
   and tests do not read.
3. Publish it with `scripts/fork-workflow.sh sync-publish <commit>`, where
   `<commit>` is the rebased series without the task's own commits (`HEAD` when
   the task has none yet). The script checks that the commit contains the
   upstream tip and no merge commits, that local `develop` is clean and (after
   a fast-forward) equals `origin/develop`, keeps the old tip under
   `refs/fork-backup/`, force-pushes
   with a lease on the inspected remote tip, moves local `develop`, and lists
   the open pull requests.
4. Each open pull request is now based on the old `develop`. In its worktree run
   `scripts/fork-workflow.sh restack`, which finds the old base in
   `origin/develop`'s reflog, rebases the task commits onto the new `develop`
   and updates the pull request.

With no conflicts and nothing to revalidate this is the plain sequence in the
permanent checkout (`git fetch official`, `git rebase official/develop`, `git
push --force-with-lease origin develop`); `sync-publish` is the same thing with
the checks and the backup ref. The standing authorization covers this force
push of the personal `develop`, verified by URL
(`https://github.com/endqwerty/crosspoint-reader.git` or its SSH equivalent). It
does not cover pushes to the official project, release publication, or closing
or merging pull requests.

After an upstream update, confirm `AGENTS.md` still directs agents here and
review this file for rules or patches superseded by upstream. File placement
alone does not enforce linear history.

## Starting a task

- A new thread's worktree starts from the fork's `develop`. Read this file and
  `WIP.md`, inspect `git worktree list`, status and remote URLs, then run
  `scripts/fork-workflow.sh status` and sync with upstream first if it is ahead.
- The worktree starts without the SDK. `scripts/fork-workflow.sh prepare` checks
  out the reader-pinned revision; do not substitute the SDK's newest branch tip.
  Recursive icon submodules are needed only for icon generation, not ordinary
  firmware builds.
- Do the work in the thread's worktree. A shared local build mirror is usable
  only after verifying its source matches the intended worktree; never reuse
  another feature's outputs without checking.
- `WIP.md` separates completed handoff evidence from possible future work.
  Its roadmap is context, not an instruction to begin unrequested features.

## Finishing a task

- Run the local checks ("Local checks") before opening or updating the pull
  request for source changes. Record physical-device checks separately as
  pending when unavailable; do not claim device validation or hold the pull
  request back solely on that absence. Documentation-only changes require review
  and diff checks, not a firmware rebuild or device test.
- Update `WIP.md` and the affected `docs/fork-*.md` in the same pull request.
- A firmware handoff is built from the pull request's head. Record the pull
  request number and head commit in `build-info.json` and `WIP.md`: after a
  squash merge that commit is no longer on `develop`, but GitHub keeps it at
  `refs/pull/<number>/head`, and the squash commit has the same tree as long as
  `develop` did not move in between.
- Report any genuine blocker instead of claiming unfinished work is complete. As
  the last step, check and report:
  - the pull request URL, the local check results, and that the pull request is
    linked to the thread;
  - the branch is pushed, and `origin/develop` contains the upstream tip;
  - temporary git worktrees or scratch clones created for the task are removed;
  - `scripts/fork-workflow.sh release` ran last and reported that T3 Code can
    remove the worktree.
- Preserve human authorship when adapting patches; do not add assistant
  attribution to commits. Follow the author-verification rules in `AGENTS.md`.

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
  builds. Windows opens `\\10.10.0.214\workspace\builds\crosspoint-reader`.
  Never store the only output under `workspace/projects/crosspoint-reader`;
  that shared source checkout is disposable. A successful compilation does
  not establish physical-device validation.
- Local toolchain: `pio`, `cmake` and `ctest` are in
  `~/.local/share/crosspoint-build/venv/bin/`, not on `PATH`. Host tests:
  `cmake -S test -B <dir> -DCMAKE_BUILD_TYPE=Release`, build, then
  `ctest --test-dir <dir> --output-on-failure --timeout 60 -j 8`. Sanitizer runs
  add `-DCROSSPOINT_TEST_SANITIZERS=ON` with Homebrew
  `/opt/homebrew/opt/llvm@22/bin/clang` and `clang++`, and need `--timeout 180`
  (`GlyphRasterParity` takes about 70 s under the sanitizers). Keep test build
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
