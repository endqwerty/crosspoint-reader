# Pull requests to the official project

Read this before preparing any change for the official project
(`crosspoint-reader/crosspoint-reader`). [delivery.md](delivery.md#offering-a-fix-to-the-official-project)
has the Git steps; this page says what the maintainers accept. It summarizes
their written rules and how they reviewed about 2,600 pull requests from
December 2025 to November 2026 (researched 2026-11-16). Upstream numbers are
written in code format (`#1234`); open them at
`https://github.com/crosspoint-reader/crosspoint-reader/pull/<number>` (or
`/issues/<number>`, `/discussions/<number>`).

## Short version

An AI-assisted pull request is accepted when a human can show all of this:

1. The change is small, does one thing and fits `SCOPE.md` and `ROADMAP.md`.
2. The human reproduced the problem and verified the fix on a real device.
3. The human understands the code and can explain what it does and why.
4. The human wrote the description and the review replies in their own words.
5. The template is filled in, AI use is disclosed honestly and the branch is
   current with `develop`.

AI use itself is not a reason for rejection. Unverified, oversized or
unexplained work is. Maintainers often close such PRs quickly, sometimes
calling them "AI slop" (`#2575`, `#4070`).

## Who reviews

Uri-Tauber and itsthisjustin merge most pull requests and are the contacts in
`GOVERNANCE.md`. lpla, brianhuster, uxjulia, serialx, jeremydk and osteotek also
merge. The founder, daveallie, and znelson did most early merges but have not
reviewed since mid-2026. Uri-Tauber does most reviews and reported spending 3
to 7 hours a day on the project (Discussion `#2721`, 2026-07-25). There are
about 200 open pull requests, so reviewer time is limited.

## Written rules

These files are on `official/develop`. For upstream work, read upstream's own
copies (`git show official/develop:AGENTS.md`), not this fork's edited ones
([the fork edits](upstream-sync.md#fork-edits-to-upstream-instruction-files)).

- **PR template** (`.github/PULL_REQUEST_TEMPLATE.md`). Summary, a Scope Check
  list, Additional Context with memory and flash impact, and the question "Did
  you use AI tools to help write this code? YES | PARTIALLY | NO". It says
  CrossPoint has no restrictions on AI tools but asks for transparency.
- **`SCOPE.md`, `ROADMAP.md` and the template's Scope Check.** New built-in
  themes and new network connectors are closed for now. Interactive apps,
  writing tools, RSS, browsers, media playback and PDF are out of scope. A
  feature must not already be handled by the stock firmware or another popular
  fork, unless the PR explains why. Changes to `freeink-sdk/`, `lib/hal/`, the
  bootloader, OTA or recovery need the relevant maintainer first.
- **`AGENTS.md` and `.agents/`** (rewritten in `#3890`, 2026-10-08). Fully
  autonomous agents are forbidden. The human writes the PR description; an
  agent may give factual notes and test results, never ready-to-paste PR prose.
  Commits need explicit human approval and pushes an explicit instruction.
  Agents never open or close a PR. No assistant co-author or generated-by
  trailers. The `firmware-handoff` skill runs checks and four independent
  reviews before a change is ready. Hardware testing is the human's job before
  the PR. The ESP32-C3 of the X3/X4 is the resource baseline. When a
  contributor asked to relax these rules, Uri-Tauber refused: "they reduce the
  amount of AI slop we get" (`#4333`, comment of 2026-11-11).
- **`.agents/rules/git-workflow.md`.** Branches `<type>/<short-description>`;
  commits `<type>: <short summary>` with at most 50 characters; human authors
  of ported code credited with `Co-Authored-By`.
- **`.agents/rules/hardware-resources.md`, "Flash size: unused features must
  cost nothing"** (added 2026-11-16, inside the EEGO A4 auto-sleep PR `#4494`). A new optional feature must
  not grow builds, screens or call sites that do not use it.
- **`docs/contributing/development-workflow.md`.** Branch from `develop`, one
  fix or feature per PR, conventional-commit PR title (CI checks it),
  `clang-format` 21+, `pio check` and `pio run` before opening, and
  reproduction and verification steps for bug fixes.
- **Unwritten limit.** GitHub refuses a sixth open PR from one user. Uri-Tauber
  called it "a relatively-new restriction after new contributors opened dozens
  of AI-generated PRs without even verifying on actual device" (`#4331`,
  2026-11-06).

## How the rules changed

| When | Change |
| --- | --- |
| 2025-12 | daveallie adds the PR template. Reviews ask to split mixed PRs and avoid new dependencies. |
| 2026-01-12 | The AI Usage question joins the template. |
| 2026-02-05 | `SCOPE.md` and `GOVERNANCE.md` appear (`#640`). |
| 2026-03 to 05 | Many duplicate feature PRs. Maintainers ask for a Discussion and community votes first. 44 stale PRs closed on 2026-05-19. |
| 2026-07-20 | "Phase 0" closes old roadmap work; the Scope Check list and `ROADMAP.md` arrive (`#2149`). Hardware testing becomes the expected gate. |
| 2026-10 | `AGENTS.md` rewrite with the human-ownership rules (`#3890`) and repeated "AI slop" closures. `SCOPE.md` allows narrow, product-neutral building blocks for downstream forks (`#3821`). |
| 2026-11 | The five-open-PR limit is reported (`#4331`), the agent rules are kept (`#4333`) and the flash-size rule is added (`#4494`). Uri-Tauber: "I dont have any bias against AI tolls [sic], I use them myself heavily. What I do want is accountability" (`#4331`). |

The trend is toward stricter evidence and human accountability, not toward
banning AI.

## What reviews ask for

Counts are maintainer comments matching each topic, from a keyword scan of
about 7,000 comments.

- **Memory and heap (about 450).** Allocations, fragmentation, OOM and stack
  size, judged on the ESP32-C3. Extra edge-case code is pushed back: "the
  firmware will be bloated with almost-never-used code, and maintenance will be
  a nightmare" (`#3455`).
- **Rebase onto `develop` (about 230).** A PR many commits behind is not
  reviewed until it is updated.
- **Split the PR (about 180).** "the zipfile perf improvement should have its
  own PR" (`#3942`).
- **Device images (about 170).** Before and after, from a real panel: phone
  photos or framebuffer captures. Simulator images and mockups are distrusted
  (`#2306`, `#3342`).
- **Real hardware testing (about 170).** "draft this until you done proper
  testing on actual device" (`#2557`).
- **Translations (about 140).** New user-facing strings go through the
  translation files, not hard-coded text.
- **Generated files.** Change the generator script, regenerate, and commit both
  (upstream `.agents/rules/generated-files-cache.md`).
- **Short, plain descriptions.** "I dont have the patience to read Claude's
  verbose description" (`#3685`). A long description is accepted when it is
  evidence in clear sections, as in `#4076`.
- **Minimal diffs.** "the best code is code that never gets written — it can
  never break" (`#3732`).

## Why PRs are closed

From a sample of 96 closed-unmerged PRs, December 2025 to November 2026:
about 40% closed by their author, 15 to 20% replaced by another PR or duplicate, about 15%
stale. The rest:

- Out of scope or not an ebook concern (`#1811`; issue `#2528`).
- Another fork already proves an approach; upstream prefers that one (`#1698`).
- Maintenance or flash cost greater than the benefit (`#2615`, `#4042`).
- Claims in the description that the code does not match (`#2109`).
- No real-hardware test (`#2500`, `#2517`).
- No community interest in a feature (`#1650`), or a design choice upstream
  made on purpose (`#3285`).
- AI output nobody reviewed (`#2575`, `#4070`, `#2392`).

`#4070` is the case to remember. A contributor asked Claude to find upstream
fixes and opened 30 untested PRs. Uri-Tauber closed all but one and set these
conditions for each PR: reproduce the bug on `develop` on real hardware, verify
the fix on real hardware, "Write the PR description yourself — no AI", and list
the tools and steps used.

## Maintainers' own PRs compared with contributors'

Measured over 1,559 merged PRs, December 2025 to November 2026.

| | Maintainers | Contributors |
| --- | --- | --- |
| Median size | 75 lines, 4 files | 39 lines, 2 files |
| Median time to merge | 9 hours | 26 hours |
| Closed unmerged | 13% | 42% |
| Median description | about 500 characters | about 1,600 characters |
| Share answering YES to the AI question | 17% | 46% |

- Maintainers self-merge about half their PRs, many without another review,
  and often skip the template (itsthisjustin never uses it). Contributors do
  not get that freedom: they need evidence (device images, heap and flash
  numbers, reproduction steps).
- Maintainers write short descriptions and long ones only for risky changes.
  Start with a short summary; put evidence in sections below it.
- Disclosed AI use still merges: over 300 contributor PRs answering YES were
  merged. Since January 2026 the merge rate among resolved contributor PRs was
  78% for NO and 54% for YES. Scope closures explain part of the gap.
- `#4076` (lpla, who also merges) is a good model and answers YES. It changes
  one line, explains the cause, gives reproduction steps with a test file,
  shows before and after framebuffer captures, and lists what was and was not
  tested. It merged within 80 minutes.

Merged contributor PRs from April to November 2026: median 37 changed lines and
1 file; 75% are under 120 lines and 5 files. Half merge within a day; PRs idle
for two months are usually closed.

## Checklist for an upstream PR from this fork

Before the human opens it:

- [ ] Search open and closed PRs, issues and Discussions for the same change.
- [ ] Fits `SCOPE.md` and `ROADMAP.md`. A feature has a Discussion or issue
      with maintainer interest first. A bug fix needs none.
- [ ] One problem per PR, the smallest diff that fixes it, no drive-by
      refactors, no fork-only hunks, no new dependency.
- [ ] Based on current `official/develop`, rebuilt as in
      [delivery.md](delivery.md#offering-a-fix-to-the-official-project).
      Branch `<type>/<summary>`; commits `<type>: <summary>` of at most 50
      characters; a conventional-commit PR title.
- [ ] Upstream's `firmware-handoff` done against upstream's own rules:
      `clang-format`, `pio check`, `pio run -e default` (ESP32-C3), host
      tests, four read-only reviews.
- [ ] Flash change reported for `default` and one other target when shared
      code changes; a feature costs nothing where it is unused.
- [ ] Heap or speed claims give ESP32-C3 numbers. The owner has only the X4 Pro
      (ESP32-S3): say so, and ask for a C3 tester when the claim depends on it.
- [ ] The human reproduced the bug on `develop` and verified the fix on the
      device, with phone photos or framebuffer captures for any screen change.
- [ ] The human can explain the cause and the fix without the agent.
- [ ] The human wrote the description. The agent supplies only facts: commits
      tested, check output, numbers, files changed.
- [ ] AI Usage answered honestly (usually YES or PARTIALLY).
- [ ] No assistant `Co-Authored-By` line in commits. Leave out the "Generated
      with" footer that fork PRs use: the human writes the upstream body.
      Ported code credits its human author.
- [ ] CodeRabbit's findings handled before asking a maintainer to review;
      replies to reviewers in the human's own words.
- [ ] Fewer than five of the owner's PRs are open upstream.
- [ ] In fork issues, PRs and commits, upstream numbers stay in code format,
      not links ([FORK.md](../FORK.md#every-task)).

## Device testing and fork rules

Fork rules say the owner takes no manual device measurements and agents do not
ask for them. Upstream PRs are the exception: a request from the owner for an
upstream PR allows the agent to ask once for the device test and the
description. Until the owner has done both, stop with the branch prepared and
do not push it for a PR; the change stays on the fork.
