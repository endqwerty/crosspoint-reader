# Fork instructions

Rules for this personal fork, on top of `AGENTS.md` and `.agents/`. Read this page
for every task and a topic page only when the task touches it. Upstream lines that
contradicted the authorization below are
[edited in place](fork/upstream-sync.md#fork-edits-to-upstream-instruction-files).

**Precedence:** (1) the user's current instructions and the authorization below;
(2) upstream (`AGENTS.md`, `.agents/`, maintainers); (3) these pages. Upstream wins
on firmware architecture and behavior: if a fork preference conflicts, follow
upstream and note it in the issue or PR. Authorization replaces asking for approval,
not engineering, validation or upstream-compatibility requirements.

**Standing authorization (2026-10-06):** autonomous work end to end on the personal
fork, replacing upstream's ban on autonomous agents and its ask-first Git rules.
Without asking, agents may commit finished work on the task branch, push it to
`origin`, open and write a PR against the fork's `develop`, resolve review findings,
squash-merge this thread's own PR, and publish validated upstream syncs of the fork's
`develop` (reader and SDK, daily maintenance included). Not covered: the official
project (push or PR), release tags, closing a PR unmerged, another thread's
worktree. Ask only when an essential fact or decision cannot be resolved from the
code, upstream and these rules.

## Every task

- **Start:** read the issue queue; run `git worktree list`, `git status`,
  `git remote -v`, `scripts/fork-workflow.sh status`; sync first if upstream is ahead.
- **Branch:** never commit task work on `develop` or push it to `origin/develop`.
  Work on the branch T3 assigned and never delete it. One thread is one worktree,
  branch and PR; subagents work in that worktree.
- **Remotes:** `origin` is the personal fork (read `fork` in upstream docs as
  `origin`); `official` is the official project, fetch only.
- **History:** a linear patch series on `official/develop`; on conflict follow
  upstream's design; no downstream merge commits.
- **Commands** (`scripts/fork-workflow.sh`): `prepare` before any build or test;
  `check` for any source, test or build change (docs-only: reviewed diff and
  `git diff --check`); `pr`; `release` last, after the work is delivered or preserved.
- **Checks:** GitHub Actions is off; local checks decide build readiness. The only
  device is the Xteink X4 Pro (`x4pro-gh_release`); check other boards only for a
  concrete upstream-compatibility risk. The user takes no manual device
  measurements: do not ask for or wait on them; report device checks as unverified,
  never validated.
- **Records:** when the user changes how work is done, update these pages in the
  same task.

## Read when the task touches it

| Task touches | Read |
| --- | --- |
| Commit, push, PR, merge, SDK change, end-of-task report | [delivery](fork/delivery.md) |
| Checks, builds, toolchain, firmware export, `prepare`/`release` | [builds](fork/builds.md) |
| Rebase, upstream conflict, `sync-publish`, history rewrite or backup | [upstream-sync](fork/upstream-sync.md) |
| Daily review | [daily-review](fork/daily-review.md) |
| Feature choice or validation; CrossInk port; Calibre library; font; declined features | [scope](fork/scope.md) |
| Claiming an issue ([#15](https://github.com/endqwerty/crosspoint-reader/issues/15)), where records live, delegation and its budget | [tasks](fork/tasks.md) |
| Clone setup, remotes, GitHub settings, changing these instructions | [setup](fork/setup.md) |
| Design of a fork feature | `docs/fork-*.md` |
