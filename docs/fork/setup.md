# Repository setup and instruction files

Read this when setting up a clone, checking remotes or GitHub settings, or
editing the instruction files. The always-read rules are in
[../FORK.md](../FORK.md).

## Repository setup

Both personal forks use a single long-lived `develop` branch:

- Reader fork: `https://github.com/endqwerty/crosspoint-reader.git`.
  Its upstream base is `develop` in
  `https://github.com/crosspoint-reader/crosspoint-reader.git`.
- SDK fork: `https://github.com/endqwerty/freeink-sdk.git`.
  Keep its local patch above the SDK revision pinned by the official reader's
  `develop`, from `https://github.com/Free-Ink/freeink-sdk.git`. A newer SDK `main`
  does not automatically replace the reader's tested dependency revision.

A third remote, `crossink` (`https://github.com/uxjulia/CrossInk.git`, fetch
only, created by `scripts/fork-workflow.sh setup`), is a feature source, never a
base: CrossInk is a single-maintainer fork of CrossPoint that diverges by
roughly 130k lines. Read its code with `git show crossink/main:<path>` and port
wanted behavior as small patches on this `develop`. What was evaluated, ported
and declined is in [fork-crossink.md](../fork-crossink.md); update it with every
CrossInk-derived change and re-check its release notes when syncing.

The fork is the project. In the maintained checkout
(`~/workspace/crosspoint-reader`) `origin` is the personal fork, local `develop`
tracks `origin/develop`, and `origin/HEAD` points to `origin/develop`, so T3
Code starts each thread's worktree from the fork's `develop`. The official
project is the remote `official`, fetch only (its push URL is disabled). It is
deliberately not called `upstream`: T3 Code treats a remote of that name as the
project's own repository and then looks for, opens and merges pull requests
there. "Upstream" in these pages means the official project, not a remote name.
Where upstream docs say to push to `fork`, use `origin`. The SDK submodule's
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
is the CI (see "Local checks" in [builds.md](builds.md#local-checks)).

`t3.json` at the repository root is T3 Code's project file. It makes new threads
use worktrees and leaves the SDK submodule unpopulated in them
(`worktreeSubmodules: "none"`), because git cannot remove a worktree that has a
submodule checked out (see [Worktree lifecycle](builds.md#worktree-lifecycle)).

Do not keep bundle or other off-repository backups of the fork's history; the
user does not want them (2026-10-01). The backup ref that `sync-publish` leaves
under `refs/fork-backup/` is enough.

## Instruction-file maintenance

Fork rules live in `docs/FORK.md` and `docs/fork/`. When the user changes how
work is done, update them in the same task. The fork edits a few lines
of upstream's instruction files in place (listed in
[upstream-sync.md](upstream-sync.md#fork-edits-to-upstream-instruction-files));
keep those edits minimal and add new fork-specific preferences to the fork's
pages rather than to upstream files or release notes. Keep `docs/FORK.md` short:
it is read for every task, so put detail in a topic file and link it from the
table there. Codex, Claude Code and Antigravity all load the repository's
`AGENTS.md` (verified 2026-09-29; versions and the global-file layout are in the
homelab `workstation.md`), so do not add a `CLAUDE.md` or `GEMINI.md`. Recheck
after a major agent update before relying on that.
