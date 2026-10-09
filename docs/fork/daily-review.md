# Daily review

Read this when asked for a daily review or a maintenance pass. The always-read
rules are in [../FORK.md](../FORK.md).

A daily review is an on-demand maintenance pass when requested; it does not
create a scheduled task. Use this order so new work is based on the current
upstream implementation and a validated integration branch. Read-only PR
research can run in parallel with the sync; choose implementations against the
final baseline and serialize shared builds and Git operations.

1. **Preflight and ownership.** Run `uname -s`; read `AGENTS.md`, `docs/FORK.md`,
   the live issue queue (#15) and upstream watch (#13). Inspect the branch,
   remote URLs, worktrees, tracked/untracked changes and open personal-fork
   pull requests. Preserve other threads' work and local overrides. Confirm
   the shared artifact destination is mounted before promising a handoff.
2. **Refresh and inspect upstream first.** Fetch `official` and `origin`, run
   `scripts/fork-workflow.sh status`, and inspect commits since the previous
   upstream base, including upstream instructions, the pinned SDK, cache
   formats, build configuration and CI target matrix. Map upstream cache bumps
   to the fork's numbering before resolving conflicts. Review maintainer
   decisions and merged PRs before choosing a local implementation. Record
   exact reader/SDK/base hashes; do not treat a previous review as live state.
3. **Rebase the baseline before new task work.** Create backup refs before
   rewriting reader or SDK history. Prepare and verify the reader-pinned SDK.
   If the official reader changes its SDK pin, replay SDK-only patches onto that
   exact revision, not the latest SDK branch. Run relevant SDK host suites, then rebase the reader's downstream
   series onto `official/develop`, reconcile overlaps using upstream's interfaces
   and behavior, and drop superseded patches. A previously validated fork feature is still disposable:
   replace, simplify or remove it when upstream takes a conflicting direction
   or implements its purpose differently. Apply "Upstream first"
   ([upstream-sync.md](upstream-sync.md)) and bump
   any affected persisted cache version. Keep adaptation fixes
   together in the sync candidate; only its final tree is the validated build.
   Keep task commits separate from the sync candidate;
   never publish them directly to `develop`. Check upstream ancestry, SDK
   ancestry and zero downstream merge commits.
4. **Validate and publish the upstream sync.** Use `check` when source changed,
   plus relevant SDK host suites and additional checks justified by a specific
   compatibility risk. Review conflict resolutions, including changes that
   merged without conflicts. Collect the independent review before the build
   gate when possible to avoid redundant builds. Keep the helper's optional
   compatibility matrix aligned with upstream when its target list changes;
   a new board alone does not require building or validating that device.
   Publish a changed SDK revision before the reader that pins it; use the
   documented leases and
   `sync-publish <baseline-commit>`. If upstream moves during validation,
   rebase again and rerun checks affected by the new source. Restack existing
   PRs through their owning threads; do not mutate another active worktree.
   When upstream is already contained, record that no rebase was needed.
5. **Review PRs, refactoring and updates against that baseline.** Inspect the
   current open/recently merged upstream PRs, their head SHAs, source diffs,
   reviews and dependencies. Compare candidates with existing fork code and
   tests: a matching title is not proof that a fix is missing. Prioritize
   offline EPUB correctness, bounded heap, storage reliability and the
   Calibre library workflow on X4 Pro. Record an adopt/wait/skip decision and
   its evidence. Keep drafts, new UI, speculative prefetch/power work and
   feedback-dependent issues deferred under the existing rules. Prefer
   deterministic refactors with fixtures and operation/allocation evidence;
   a review need not invent a code change. Review toolchain and dependency
   updates against upstream pins; never blindly upgrade to newest versions.
6. **Implement and review scoped maintenance.** Claim an eligible issue before
   edits; verify paths/lines and preserve human authorship for adapted PRs.
   Delegate under [tasks.md](tasks.md#delegation). Update `docs/FORK.md` or
   `docs/fork/` for durable workflow changes, affected `docs/fork-*.md` for
   design changes, and issues for dated findings. Run the checks required by
   the final diff under [Local checks](builds.md#local-checks); use additional
   targets only for a concrete compatibility concern.
7. **Recheck and hand off.** Fetch and verify upstream/fork tips again before
   opening or updating the single personal-fork PR. Sync any new upstream
   baseline before publishing task work, then restack and revalidate as
   needed. Commit/push authorized task work, open and link the fork PR, and
   merge its reviewed, validated head under the standing authorization.
   Record selected/deferred PRs, exact checked commit and SDK, test counts,
   target RAM/flash and remaining limits in the issue/PR. For changed firmware,
   export a dated checksum-verified package and update `FLASH-LATEST.md` after
   all gates pass ([builds.md](builds.md#firmware-handoff)). Preserve logs
   outside the worktree; retain the thread's
   branch and use native cleanup rules. Never claim device timing, ghosting,
   peak heap or power-loss validation from host/build results.
