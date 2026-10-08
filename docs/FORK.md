# Fork instructions

This is the canonical location for persistent instructions specific to this fork.
Read it alongside the upstream `AGENTS.md`. Track tasks and handoffs in the
[personal fork's issue queue](https://github.com/endqwerty/crosspoint-reader/issues/15),
with validation evidence in the linked pull request and firmware package.

## Precedence

1. The user's current instructions and explicit standing authorizations recorded
   below (automatic commits, task-branch pushes and pull requests on the personal
   fork including validated merges, and upstream syncs of the personal `develop`).
2. The official project: upstream `AGENTS.md`, its architecture, interfaces,
   conventions and maintainer decisions.
3. This file.

For firmware architecture and behavior, upstream always wins over the fork.
The recorded user authorizations satisfy generic requirements to obtain approval;
they do not waive engineering, validation or upstream-compatibility requirements.
On this personal fork, the standing authorization replaces `AGENTS.md` Git
Operation Rules 2–3 and its ask-first/hardware-tested commit conditions. Local
checks replace its GitHub Actions gate and determine build readiness. Investigate
new warnings caused by the diff; record existing dependency warnings rather than
blocking delivery or changing upstream solely to silence them. Report unavailable
human/device checks
as unverified; do not request measurements or wait on them. `AGENTS.md`'s
engineering, attribution and staging rules still apply. The preferences narrow
where fork effort goes; they never justify diverging from upstream's design.
If a fork preference conflicts with upstream, follow upstream and note it in
the task's issue or pull request.

## Shared information

The user works in this repository with several agents (Codex, Claude Code and
Antigravity, through T3 Code). They share only files, not memory. Agent memory
(Claude project memory, Codex memories, Antigravity `brain/`) must never be the
only record of a rule, decision or finding. Record it here instead:

| Information | Location |
| --- | --- |
| Durable rules, preferences, standing authorizations | This file |
| Task ownership, progress, blockers, next steps | Personal fork GitHub issues; start at [#15](https://github.com/endqwerty/crosspoint-reader/issues/15) |
| Validation and source/build identity | Linked pull request and dated firmware package (`build-info.json`) |
| Design, invariants, limits, attributions of fork features | `docs/fork-*.md` |
| Upstream conventions | `AGENTS.md` (upstream; only its link here is local) |
| Current firmware image and build evidence | `/Volumes/workspace/builds/crosspoint-reader/FLASH-LATEST.md` |
| Mac, T3 Code worktrees and cleanup, agent tooling, home server | `/Volumes/workspace/homelab/workstation.md` and `README.md` there |

When a user instruction changes how work is done, update this file in the same
task. When a fact belongs to the machine rather than the repository, update the
homelab page. If `/Volumes/workspace` is not mounted, say so instead of guessing.

## Autonomous agents and delegation

Daniel's direction (2026-10-06) is autonomous work end to end. Complete the
requested scope: inspect upstream, select eligible work, implement, validate,
review, publish and integrate the personal-fork PR, export changed firmware and
update the task record. Resolve routine implementation choices from the code,
upstream and these rules. Ask only when an essential fact or decision cannot be
resolved from available evidence or a real runtime approval is required; an
agent's review, commit or merge step is not a new user approval gate.

Use T3's injected orchestration instructions and `orchestrator_capabilities`
for the live provider/model catalog. Prefer native subagents when they support
the selected model; use `delegate_task` for cross-provider work, unsupported
models or explicitly T3-owned children. Honor a requested model when available.
Codex and Claude follow the same delegation policy:

- Inherit the parent's approval/runtime mode (`runtimeMode: "inherit"` or omit
  the override). Do not force a restrictive child mode, alter permission
  settings or bypass an actual approval request. Task scope defines a
  read-only reviewer; it does not require a different runtime mode.
- Pass the task, absolute paths, starting HEAD, owned files, allowed actions
  and completion criteria. Children share the thread's checkout. Assign
  disjoint implementation files; serialize overlapping edits, builds that
  generate shared files, and Git mutations through the parent. Preserve
  existing changes rather than requiring a blanket clean-tree reset.
- Code reviews are read-only. Research may use explicitly assigned read-only
  remote access. Implementers edit their assigned files and run relevant
  checks, then return the diff, results and unresolved findings. The parent
  reviews and handles commits, publishing and integration; no child creates
  an unmanaged worktree, deletes branches or publishes independently.
- Obtain an independent review from another model for memory, rendering or
  shared-code changes. Verify findings against source and resolve material
  objections. Reviews complement meaningful host/build checks; unavailable
  device measurements do not block delivery.
- Retain task IDs and collect every outcome. Use `task_status` when the result
  is needed and `task_cancel` to stop obsolete work; a wait timeout does not
  cancel a child. For T3-delegated reviews, start each round with a new
  `delegate_task`, including prior findings and responses; do not send another
  round to `childThreadId`. Continue native subagents through native tools.

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
  remove patches that upstream has superseded. Previously validated fork work
  is disposable when upstream implements its purpose differently. Invalidate
  obsolete code, tests and design assumptions; document any intentional loss
  of fork functionality instead of preserving incompatible architecture.
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

A third remote, `crossink` (`https://github.com/uxjulia/CrossInk.git`, fetch
only, created by `scripts/fork-workflow.sh setup`), is a feature source, never a
base: CrossInk is a single-maintainer fork of CrossPoint that diverges by
roughly 130k lines. Read its code with `git show crossink/main:<path>` and port
wanted behavior as small patches on this `develop`. What was evaluated, ported
and declined is in [fork-crossink.md](fork-crossink.md); update it with every
CrossInk-derived change and re-check its release notes when syncing.

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
  use `feature/`, `fix/`, `docs/`, `refactor/` or `codex/`. The permanent
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
  `develop`; `sync-publish` of the validated rebase fixes that.
- Squash-merge once the checks and reviews required for the diff have passed,
  with no material objection or essential decision open. Fetch and verify head
  and base first; use `gh pr merge <n> --repo endqwerty/crosspoint-reader
  --squash --match-head-commit <reviewed-SHA>` without `--delete-branch`
  (GitHub deletes the remote branch; T3 needs the local branch). If the base
  moved, restack and rerun affected checks. Rebase merge is available when
  human commits should stay separate. Leave the PR open when the user asks
  to review before merge or a real blocker remains.
- After the merge GitHub deletes the remote branch, the thread settles and the
  user archives it. The worktree is T3 Code's to remove; see "Worktree
  lifecycle". Never delete a thread's local branch: T3 Code recreates the
  worktree from it when the thread is resumed.
- SDK changes are not reviewed through pull requests. Commit them on the SDK
  fork's `develop`, push that (with a lease after an SDK rebase), and let the
  reader pull request carry the new submodule commit, which must already be on
  the SDK fork so other checkouts can fetch it.

### Local checks

GitHub Actions is off for the fork. Run local checks before a pull request is
opened or updated, scoped to the X4 Pro and any specific upstream-compatibility
risk in the diff:

- `scripts/fork-workflow.sh check`: formatting (the whole tree through
  `bin/clang-format-fix`, then no diff), host tests in Release and under the
  sanitizers, and the `x4pro-gh_release` firmware build. Use it for every change
  to source, tests or build files.
- `check --full` adds `pio check` (cppcheck) and the seven firmware targets CI
  builds (`default`, `sticky`, `x4pro`, `x4c`, `papermono`, `metalio_eink4`,
  `eego_a4`): eight builds including `x4pro-gh_release`. This is an optional
  compatibility matrix, not the default gate for shared changes or upstream
  syncs. Use it only when a concrete compatibility risk warrants all targets;
  otherwise run only the relevant additional `pio run -e <target>` builds.
  Run `pio check` separately when static analysis is needed without the matrix.
- `check --fast` is formatting and Release host tests only.
- Documentation-only changes need an agent-reviewed diff and
  `git diff --check`, not `check` or device testing.

It keeps its build directories and logs outside the worktree, under
`~/.local/share/crosspoint-build/ci/<worktree>/`, and reports each step as ok
or FAILED with the log to read. Put the result (commit checked, which mode,
test counts, RAM and flash lines) in the pull request body and preserve the
logs in the firmware package or external evidence directory.

### Worktree lifecycle

T3 Code owns the worktrees. Native pull-request linking and Settle are the
default handoff; settling a thread does not itself remove its checkout. The
native cleanup worker considers deletion, configured idle time (currently five
days), and whether HEAD is contained in `origin/develop`. Removal still requires
the thread's branch, no active commands or sessions, no uncommitted/untracked
files, no ignored files except `node_modules`, and no populated submodules.
T3 recreates a removed checkout from the thread's branch when resumed. There is
no other cleanup job.

- Run `scripts/fork-workflow.sh prepare` before building or testing. It checks
  out the pinned SDK, borrowing objects from the permanent checkout.
- `scripts/fork-workflow.sh release` is optional, explicit build/SDK cleanup
  when finished with this workspace and cleanup is needed for native removal.
  Never run it merely because a thread settles or reaches a handoff. First
  commit/push authorized work and preserve/export wanted firmware, check logs
  and ignored local overrides such as `platformio.local.ini`. The command
  deinitializes the SDK, runs `git clean -ffdX` (including `.pio`, generated
  headers and ignored overrides), and deletes the external per-worktree check
  directories under `~/.local/share/crosspoint-build/ci/`. It checks SDK state
  before dropping it, but does not preserve those artifacts or local overrides.
  The next `prepare` and `check` can rebuild generated output, not recover
  discarded logs or local configuration. A successful "released" result means
  filesystem readiness only, not proof of native cleanup eligibility.
- Do not remove a worktree or its branch by hand, and do not leave the worktree
  on another branch.

After a squash or rebase merge the branch tip may not be contained in
`origin/develop`; merge/unchanged ancestry checks can therefore still prevent
removal of a released worktree. It may need the configured five-day idle
cleanup or thread deletion, with all native guards still satisfied. T3 Code's
settings and their limits are in the homelab `workstation.md`.

### Syncing with upstream

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
   "Local checks"; changed shared source alone does not require `--full`.
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
"Branches and pull requests". It does not cover official-project pushes or
release publication. Personal-fork PR integration uses the authorization above.

After an upstream update, confirm `AGENTS.md` still directs agents here and
review this file for rules or patches superseded by upstream. File placement
alone does not enforce linear history.

## Daily review

A daily review is an on-demand maintenance pass when requested; it does not
create a scheduled task. Use this order so new work is based on the current
upstream implementation and a validated integration branch. Read-only PR
research can run in parallel with the sync; choose implementations against the
final baseline and serialize shared builds and Git operations.

1. **Preflight and ownership.** Run `uname -s`; read `AGENTS.md`, this file,
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
   or implements its purpose differently. Apply "Upstream first" and bump
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
   Delegate under "Autonomous agents and delegation". Update this file for
   durable
   workflow changes, affected `docs/fork-*.md` for design changes, and issues
   for dated findings. Run the checks required by the final diff under "Local
   checks"; use additional targets only for a concrete compatibility concern.
7. **Recheck and hand off.** Fetch and verify upstream/fork tips again before
   opening or updating the single personal-fork PR. Sync any new upstream
   baseline before publishing task work, then restack and revalidate as
   needed. Commit/push authorized task work, open and link the fork PR, and
   merge its reviewed, validated head under the standing authorization.
   Record selected/deferred PRs, exact checked commit and SDK, test counts,
   target RAM/flash and remaining limits in the issue/PR. For changed firmware,
   export a dated checksum-verified package and update `FLASH-LATEST.md` after
   all gates pass. Preserve logs outside the worktree; retain the thread's
   branch and use native cleanup rules. Never claim device timing, ghosting,
   peak heap or power-loss validation from host/build results.

## Starting a task

- A new thread's worktree starts from the fork's `develop`. Read this file and
  the [issue queue](https://github.com/endqwerty/crosspoint-reader/issues/15),
  inspect `git worktree list`, status and remote URLs, then run
  `scripts/fork-workflow.sh status` and sync with upstream first if it is ahead.
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
  and this file's authorizations govern implementation; a backlog entry does
  not waive an explicit deferred condition.
- Keep progress, blockers and next actions on the issue, not in a versioned
  task-status file. If stopping before delivery, leave a concrete issue handoff
  and remove `in-progress`. If GitHub is unavailable, report the limitation and
  preserve a resumable handoff in the thread; do not invent live issue status.
- Completed pre-migration history remains in
  [the frozen handoff](https://github.com/endqwerty/crosspoint-reader/blob/75e2cb47f2784d246a03abebf34e03e37a600d96/WIP.md).
  Inspect live Git state and the shared `FLASH-LATEST.md` instead of treating
  historical hashes or roadmap notes as current instructions.

## Finishing a task

- Run the local checks ("Local checks") before opening or updating the pull
  request for source changes. Record physical-device checks separately
  as unverified when unavailable; do not claim device validation or hold the pull
  request back solely on that absence.
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
- Report any genuine blocker instead of claiming unfinished work is complete. As
  the last step, check and report:
  - the pull request URL, linked to the thread, and local check results;
  - the PR is merged, `origin/develop` has the reviewed head's tree and contains
    the upstream tip, or the reason it was left open;
  - no extra worktrees or scratch clones remain;
  - the thread's branch is retained for native PR/Settle handling; report any
    optional build/SDK cleanup performed or deferred under "Worktree lifecycle".
    Do not equate a "released" filesystem with native removal eligibility.
- Preserve human authorship when adapting patches; do not add assistant
  attribution to commits. Follow the author-verification rules in `AGENTS.md`.

## Development and validation preferences

- Licensing (2026-10-08): this is a private personal fork with no other users or
  distribution. Do not spend effort on license files, notices or compliance
  tracking for adapted code (CrossInk is MIT). Still add the original human
  author as `Co-Authored-By` when a commit adapts their code, as `AGENTS.md`
  requires.
- Delegation budget (2026-10-08): prefer GPT 6.1 Sol children while Codex
  allowance remains above 5%, then prefer Claude children. Read the allowance
  with `codex-usage` (see the global `AGENTS.md`) before and after a large
  delegation; it reports Codex only.
- Personal reader font (2026-10-06): embed Libron and enforce it over saved
  built-in, SD bitmap and vector font selections. Keep the selected font size
  at the nearest supported built-in size; preserve other typography settings.
  Font controls show the enforced family as Reader Serif, honoring the source
  font's reserved-name license. See
  [the font policy](fork-libron-font.md) for provenance and resource details.

- Device scope (2026-10-06): focus on offline EPUB reading for Xteink X4 Pro
  (`x4pro-gh_release`), the only device the user owns or tests. Other devices
  are not feature, optimization or validation targets for this personal fork.
  Check them only when necessary to maintain upstream compatibility, such as
  a changed HAL interface, platform-specific code or shared build configuration;
  record the specific reason and use the smallest relevant set of checks.
  Shared code must retain upstream interfaces and respect C3 memory constraints
  where needed for compatibility. A shared change alone does not require all
  boards to build or work, and unrelated other-device failures do not block
  X4 Pro delivery.
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
- The user declined extra reading-display features (2026-09-29): time-left
  estimates, whole-book page estimates and additional indentation/estimate UI.
  Do not propose them again without a new request. Chapter-boundary prefetch,
  idle-power changes, extra series-menu UI and UUID-less relinking remain
  conditional on a reported need; an autonomous backlog review does not
  activate them. Upstream watch conditions are tracked in
  [#13](https://github.com/endqwerty/crosspoint-reader/issues/13).
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
  a worktree surviving a handoff. Before it can be removed, deliver the completed
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
  scope and record concrete remaining work in GitHub issues.

## Instruction-file maintenance

Keep upstream's `AGENTS.md` changes limited to the reference to this file. Add new
fork-specific preferences here rather than distributing them through upstream
instructions or release notes. Codex, Claude Code and Antigravity all load the
repository's `AGENTS.md` (verified 2026-09-29; versions and the global-file
layout are in the homelab `workstation.md`), so do not add a `CLAUDE.md` or
`GEMINI.md`. Recheck after a major agent update before relying on that.
