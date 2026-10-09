# Branches, pull requests and merging

Read this before committing, pushing, opening or merging a pull request, or
changing the SDK. The always-read rules are in [../FORK.md](../FORK.md).

```text
official/develop          official project (upstream), fetch only
      |  periodic rebase, force-push with lease, no pull request
      v
origin/develop            downstream integration branch
      ^
      |  pull requests, squash-merged
      +-- <T3-assigned branch>   one per thread, in its own worktree
      +-- feature/<x>, fix/<x>, docs/<x>, refactor/<x>, codex/<x>
```

- `develop` is the integration branch, not a place to work. Never commit task
  work on it, never push task commits to `origin/develop`, and never move local
  `develop` to a task branch. It changes in exactly two ways: a merged pull
  request, or an upstream sync.
- Every task happens on a short-lived branch created from `origin/develop`. A
  T3 Code thread already has one under `~/.t3/worktrees`, created from
  `origin/develop`. Read the branch T3 assigned with `git branch --show-current`;
  it may rename it at thread startup. Keep that branch for the thread's lifetime
  because T3 tracks the workspace by it. Branches made by hand
  use the names in `.agents/rules/git-workflow.md` (`feat/`, `fix/`, `docs/`,
  `refactor/`, ...); `feature/` and `codex/` are also accepted. The permanent
  checkout stays on `develop`; do not check out `develop` in a worktree.
- Standing authorization (2026-10-06): without asking each time, commit
  completed work on the task branch, push it to `origin`, open a pull request
  against the fork's `develop`, resolve review findings and squash-merge this
  thread's own PR. It covers the requested scope on the personal fork,
  including daily maintenance, not upstream publication or release tags.
  Do not leave completed work only in a disposable worktree.
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
- One thread is one worktree, one branch and one pull request. Sub-agents
  (Claude or Codex, started from the thread) work inside that worktree; the
  parent serializes commits on its branch. Do not give children their own worktrees
  (`isolation: worktree`, `git worktree add`, scratch clones), because T3 Code
  does not know about those and never removes them. Work that needs a separate
  pull request needs a separate T3 Code thread. Create one with T3's explicit
  workspace strategy only when the user asks for separate top-level work;
  ordinary subagent parallelism stays in the caller's worktree.
- If upstream is ahead, publish the sync before a pull request is opened
  (T3 Code's own "create pull request" does not check). A branch pushed while
  it sits on an unpublished rebase shows the whole series and conflicts with
  `develop`; `sync-publish` of the validated rebase fixes that
  ([upstream-sync.md](upstream-sync.md)).
- Squash-merge once the checks and reviews required for the diff have passed,
  with no material objection or essential decision open. Fetch and verify head
  and base first; use `gh pr merge <n> --repo endqwerty/crosspoint-reader
  --squash --match-head-commit <reviewed-SHA>` without `--delete-branch`
  (GitHub deletes the remote branch; T3 needs the local branch). If the base
  moved, restack and rerun affected checks. Rebase merge is available when
  human commits should stay separate. Leave the PR open when the user asks
  to review before merge or a real blocker remains.
- After the merge GitHub deletes the remote branch, the thread settles and the
  user archives it. The worktree is T3 Code's to remove; see
  [builds.md](builds.md#worktree-lifecycle). Never delete a thread's local
  branch: T3 Code recreates the worktree from it when the thread is resumed.
- SDK changes are not reviewed through pull requests. Commit them on the SDK
  fork's `develop`, push that (with a lease after an SDK rebase), and let the
  reader pull request carry the new submodule commit, which must already be on
  the SDK fork so other checkouts can fetch it.

## Pull request body

Follow the global pull-request rules and keep the repository's template
(`.github/PULL_REQUEST_TEMPLATE.md`). Fork additions:

- Put the local check result in the body: commit checked, which mode, test
  counts, RAM and flash lines. Preserve the logs in the firmware package or
  external evidence directory.
- Link the task issue. List device checks as unverified.

## Finishing a task

- Run the local checks ([builds.md](builds.md#local-checks)) before opening or
  updating the pull request for source changes. Record physical-device checks
  separately as unverified when unavailable; do not claim device validation or
  hold the pull request back solely on that absence.
- Update the task issue and affected `docs/fork-*.md`; link the issue from the
  pull request. After merge and artifact handoff, verify issue
  closure explicitly (the integration branch is `develop`). Close with actual
  validation results and remaining limits, then remove `in-progress`.
- Build changed task firmware from the reviewed pull request's head. An
  upstream-sync handoff can use the validated sync candidate directly. Record
  the actual build commit, SDK and version; do not rebuild solely to change a
  Git hash when firmware source bytes are identical. Record the pull request
  number and head commit when applicable in `build-info.json` and the issue/PR.
  After squash merge, verify the integration tree matches the reviewed head;
  GitHub retains that head at `refs/pull/<number>/head`.
- Report any genuine blocker instead of claiming unfinished work is complete. Do
  not start open-ended roadmap work from a handoff: finish the user's requested
  scope and record concrete remaining work in GitHub issues.
- As the last step, check and report:
  - the pull request URL, linked to the thread, and local check results;
  - the PR is merged, `origin/develop` has the reviewed head's tree and contains
    the upstream tip, or the reason it was left open;
  - no extra worktrees or scratch clones remain;
  - the thread's branch is retained for native PR/Settle handling; report any
    outcome of `release` (or its concrete failure) under
    [Worktree lifecycle](builds.md#worktree-lifecycle). Do not equate a
    "released" filesystem with native removal eligibility.
- Preserve human authorship when adapting patches; do not add assistant
  attribution to commits. Follow the author-verification rules in
  `.agents/rules/git-workflow.md`.
