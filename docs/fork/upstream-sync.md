# Upstream first: rebasing and syncing

Read this before rebasing, resolving conflicts with upstream, syncing the
personal `develop`, or rewriting history. The always-read rules are in
[../FORK.md](../FORK.md).

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
  requests on the personal fork; see [delivery.md](delivery.md). Do not
  introduce local merge commits or use `--rebase-merges` to preserve an
  interwoven fork history.
- Preserve upstream architecture, public interfaces, and behavior when resolving
  conflicts. Adapt or drop local changes that conflict with upstream's design;
  remove patches that upstream has superseded. Previously validated fork work
  is disposable when upstream implements its purpose differently. Invalidate
  obsolete code, tests and design assumptions; document any intentional loss
  of fork functionality instead of preserving incompatible architecture.
- Before rewriting existing local history, create a backup ref. Flatten legacy
  local merges into a reviewed patch series above the upstream base, preserving
  intended changes and human authorship. Revalidate the resulting source using
  the relevant tests and firmware target when source changes.
- Do not keep bundle or other off-repository backups of the fork's history; the
  user does not want them (2026-10-01). The backup ref that `sync-publish` leaves
  under `refs/fork-backup/` is enough.
- Rebasing published commits rewrites their IDs. When an authorized update
  requires a force push, use `--force-with-lease`, never an unconditional force
  push.

## Syncing with upstream

The agent does this as part of resolving work, without a pull request: at the
start of a task, and again before opening or updating a pull request if upstream
moved meanwhile. `scripts/fork-workflow.sh status` shows whether upstream is
ahead.

1. In the task worktree, `git fetch official` and `git rebase official/develop`.
   The thread stays on its task branch. Before task commits it has the same
   baseline as `origin/develop`, so this rebases the downstream series; with
   task commits, they ride on top of it. Identify the sync candidate separately.
   Resolve conflicts by the "Upstream first" rules.
2. Validate the rebased series with `scripts/fork-workflow.sh check`, unless
   upstream changed only files the build and tests do not read. Add static
   analysis or targeted compatibility builds as required by the diff under
   [Local checks](builds.md#local-checks); changed shared source alone does not
   require `--full`.
3. Publish it with `scripts/fork-workflow.sh sync-publish <commit>`, where
   `<commit>` is the rebased series without the task's own commits (`HEAD` when
   the task has none yet). The script checks that the commit contains the
   upstream tip and no merge commits, that local `develop` is clean and (after
   a fast-forward) equals `origin/develop`, keeps the old tip under
   `refs/fork-backup/`, force-pushes
   with a lease on the inspected remote tip, moves local `develop`, and lists
   the open pull requests.
4. Each open pull request is now based on the old `develop`. Coordinate with
   its owning thread; do not mutate another active worktree. The owner runs
   `scripts/fork-workflow.sh restack`, which finds the old base in
   `origin/develop`'s reflog, rebases the task commits onto the new `develop`
   and updates the pull request.

Use `sync-publish` for the lease-push, checks and backup ref. Standing
upstream-sync authorization covers personal reader `develop`, verified by URL
(`https://github.com/endqwerty/crosspoint-reader.git` or its SSH equivalent),
and SDK fork `develop` (`https://github.com/endqwerty/freeink-sdk.git`) under
[Branches and pull requests](delivery.md). It does not cover official-project
pushes or release publication. Personal-fork PR integration uses the
authorization in [FORK.md](../FORK.md).

When syncing, re-check CrossInk's release notes ([scope.md](scope.md#porting-from-crossink)).

After an upstream update, confirm `AGENTS.md` still directs agents to
`docs/FORK.md` and review the fork's pages for rules or patches superseded by
upstream. File placement alone does not enforce linear history.

## Fork edits to upstream instruction files

The fork edits these upstream lines in place so the instructions do not
contradict the standing authorization. After a rebase onto upstream, check that
each still says the same thing, and re-apply it if an upstream change replaced
it. Take any new upstream rule as written, unless it contradicts the
authorization in [FORK.md](../FORK.md); then adapt it the same way and add it
to this list.

| File | Section | Fork change |
| --- | --- | --- |
| `AGENTS.md` | top | Pointer to `docs/FORK.md`. |
| `AGENTS.md` | Human ownership | The standing authorization replaces "fully autonomous agents are forbidden", ask-first commits, push-on-instruction, never opening a PR and human-written PR descriptions for fork work. The original rules are restated for the official project. Review subagents stay read-only. |
| `AGENTS.md` | Mandatory firmware handoff | Checklist items for human confirmation, the hardware test plan and "did not write a PR description" follow the authorization for fork PRs. |
| `.agents/rules/git-workflow.md` | Repository Detection Protocol | Roles of `origin`, `official`, `upstream`. |
| `.agents/rules/git-workflow.md` | Git Operation Rules 2–3 | Replaced by the authorization; `origin` instead of `fork`. |
| `.agents/rules/git-workflow.md` | When to Commit | Ask-first and hardware-tested conditions replaced by the authorization. |
| `.agents/rules/testing-debugging.md` | Testing Checklist, CI/CD | Local checks replace the GitHub Actions gate. |
| `.agents/skills/firmware-handoff/SKILL.md` | step 5 | Same change as the `AGENTS.md` checklist; sync and SDK changes keep their own flow. |
| `.agents/README.md` | top, Mandatory firmware review | Handoff wording; the stale `CLAUDE.md` sentence removed. |
