## Git Workflow and Repository Awareness

### Repository Detection Protocol

Use the session's branch, remote, and status checks; inspect their roles rather
than assuming `origin` is a fork or that an `upstream` remote exists.
In this fork `origin` is the personal fork and `official` is the official
project, fetch only; no remote is named `upstream`.

### Git Operation Rules

- Integration and PR comparisons target `develop`, not `master` or remote HEAD.
- Authorization follows [human ownership](../../AGENTS.md#human-ownership).
  The standing authorization (docs/FORK.md) covers pushing the task branch to
  `origin` and opening and merging this thread's PR on the fork; closing a PR
  unmerged is not covered. Never push to the official project without explicit
  approval from the human, who also opens any PR against it. Before a push,
  recheck remotes and verify the URL rather than assuming a name establishes
  its role.
- No assistant co-author or generated-by trailers.
- When adapting/superseding another person's PR, verify the original human
  author and credit them with `Co-Authored-By`; skip bot authors.

### Branch Naming Convention

Use `<type>/<short-description>`, where type is `feat`, `fix`, `refactor`,
`docs`, `test`, `chore`, or `perf`, matching PR title prefixes.
In this fork a T3-assigned branch name is kept as is, and `feature/` and
`codex/` are also accepted.

### Commit Message Format

Use `<type>: <short summary>` (summary at most 50 characters), with an optional
body containing relevant facts and verification. Types match branch prefixes.

### When to Commit

A local commit requires completed requested work (the standing authorization
replaces the human's approval of the commit on the fork), relevant passing checks, and [firmware-handoff](../skills/firmware-handoff/SKILL.md)
for firmware/build changes. A pure refactor preserves behavior. Do not commit
experiments, failed builds, build warnings, or ignored outputs; inspect status
and `.gitignore` before staging. Exclude unrelated user work.

Hardware testing is the human's and is not a gate for a commit or PR on the fork.
The agent lists the unverified device checks with a concrete test plan and never
claims the hardware result itself. For the official project, hardware testing
comes before a PR and the human owns PR prose and publication.
