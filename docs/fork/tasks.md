# Starting a task, shared records and delegation

Read this when picking up or claiming an issue, recording state, or delegating.
The always-read rules are in [../FORK.md](../FORK.md).

## Where information lives

| Information | Location |
| --- | --- |
| Durable rules, preferences, standing authorizations | `docs/FORK.md` and `docs/fork/` |
| Task ownership, progress, blockers, next steps | Personal fork GitHub issues; start at [#15](https://github.com/endqwerty/crosspoint-reader/issues/15) |
| Validation and source/build identity | Linked pull request and dated firmware package (`build-info.json`) |
| Design, invariants, limits, attributions of fork features | `docs/fork-*.md` |
| Upstream conventions | `AGENTS.md` and `.agents/` |
| Current firmware image and build evidence | `/Volumes/workspace/builds/crosspoint-reader/FLASH-LATEST.md` |
| Mac, T3 Code worktrees and cleanup, agent tooling, home server | `/Volumes/workspace/homelab/workstation.md` and `README.md` there |

When a user instruction changes how work is done, update the fork's pages in the
same task. When a fact belongs to the machine rather than the repository, update
the homelab page.

## Starting a task

- A new thread's worktree starts from the fork's `develop`. Read
  `docs/FORK.md` and the [issue queue](https://github.com/endqwerty/crosspoint-reader/issues/15),
  inspect `git worktree list`, status and remote URLs, then run
  `scripts/fork-workflow.sh status` and sync with upstream first if it is ahead
  ([upstream-sync.md](upstream-sync.md)).
- The worktree starts without the SDK. `scripts/fork-workflow.sh prepare` checks
  out the reader-pinned revision; do not substitute the SDK's newest branch tip.
  Recursive icon submodules are needed only for icon generation, not ordinary
  firmware builds.
- Do the work in the thread's worktree. A shared local build mirror is usable
  only after verifying its source matches the intended worktree; never reuse
  another feature's outputs without checking.
- Select an unclaimed `backlog` issue whose activation conditions are met;
  exclude `in-progress`, `needs-decision` and `waiting-upstream`. Claim it with
  `in-progress` and a note naming the thread's branch before edits. Recheck for
  another claim and resolve ownership if two threads race. The issue's scope
  and the standing authorizations govern implementation; a backlog entry does
  not waive an explicit deferred condition.
- Keep progress, blockers and next actions on the issue, not in a versioned
  task-status file. If stopping before delivery, leave a concrete issue handoff
  and remove `in-progress`. If GitHub is unavailable, report the limitation and
  preserve a resumable handoff in the thread; do not invent live issue status.
- Completed pre-migration history remains in
  [the frozen handoff](https://github.com/endqwerty/crosspoint-reader/blob/75e2cb47f2784d246a03abebf34e03e37a600d96/WIP.md).
  Inspect live Git state and the shared `FLASH-LATEST.md` instead of treating
  historical hashes or roadmap notes as current instructions.

## Delegation

The global rules and T3 Code's orchestration instructions govern delegation.
This repository adds:

- Children share the thread's checkout and work inside it. Assign disjoint
  implementation files; serialize overlapping edits and builds that generate
  shared files. Preserve existing changes rather than requiring a blanket
  clean-tree reset. No child creates an unmanaged worktree, deletes branches or
  publishes independently; the parent handles commits, publishing and
  integration.
- Obtain an independent review from another model for memory, rendering or
  shared-code changes. Reviews complement meaningful host/build checks;
  unavailable device measurements do not block delivery.
- Delegation budget (2026-10-08): prefer GPT 6.1 Sol children while Codex
  allowance remains above 5%, then prefer Claude children. Read the allowance
  with `homelab usage` before and after a large delegation.
