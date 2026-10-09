# Local checks, builds, firmware handoff and worktree lifecycle

Read this before running checks or builds, exporting firmware, or releasing a
worktree. The always-read rules are in [../FORK.md](../FORK.md).

## Local checks

GitHub Actions is off for the fork, and these checks replace its gate and
determine build readiness. The user does not want to spend CI minutes; this
machine is the CI. Run them before a pull request is opened or updated, scoped
to the X4 Pro and any specific upstream-compatibility risk in the diff:

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
- Investigate new warnings caused by the diff; record existing dependency
  warnings rather than blocking delivery or changing upstream solely to silence
  them.

It keeps its build directories and logs outside the worktree, under
`~/.local/share/crosspoint-build/ci/<worktree>/`, and reports each step as ok
or FAILED with the log to read. Put the result (commit checked, which mode,
test counts, RAM and flash lines) in the pull request body and preserve the
logs in the firmware package or external evidence directory.

Validation rules:

- Review existing upstream changes before choosing a new implementation.
- Measure parser, storage, allocation and rendering work with meaningful fixtures.
  Report host operation counts separately from physical page-turn latency,
  ghosting and peak device heap. Never claim unmeasured hardware improvements.
- Run the relevant validation after source changes, including the target firmware
  build before recommending a new image. Preserve existing test coverage. Do not
  rebuild solely for documentation, commits, or history changes when source bytes
  remain identical to a validated build.

## Toolchain

- `pio`, `cmake` and `ctest` are in `~/.local/share/crosspoint-build/venv/bin/`,
  not on `PATH`. Host tests:
  `cmake -S test -B <dir> -DCMAKE_BUILD_TYPE=Release`, build, then
  `ctest --test-dir <dir> --output-on-failure --timeout 60 -j 8`. Sanitizer runs
  add `-DCROSSPOINT_TEST_SANITIZERS=ON` with Homebrew
  `/opt/homebrew/opt/llvm@22/bin/clang` and `clang++`, and need `--timeout 180`
  (`GlyphRasterParity` takes about 70 s under the sanitizers). Keep test build
  directories under `~/.local/share/crosspoint-build/`. `./bin/clang-format-fix`
  finds the venv's `clang-format` when that `bin/` is first on `PATH`.
- The maintained checkout lives on local disk at `~/workspace/crosspoint-reader`;
  T3 Code worktrees go under the local `~/.t3/worktrees`. Keep source and git
  data off the SMB share.
- Firmware version for a handoff: put `[crosspoint]` `version =
  1.6.5-dev-x4pro-rNN-<upstream short hash>` in a temporary
  `platformio.local.ini` (gitignored), build, and delete it afterwards.

## Firmware handoff

All development work runs in isolated, disposable worktrees. Do not rely on a
worktree surviving a handoff. Before it can be removed, deliver the completed
firmware and its build evidence to a new dated folder under
`/Volumes/workspace/builds/crosspoint-reader/` (not the repository's `build/`).
PlatformIO normally writes to the worktree's local `.pio/build/x4pro-gh_release/`;
it does not automatically export to SMB.

- For each firmware handoff, build `x4pro-gh_release`, inspect the resulting
  image, and copy `firmware.bin` with a descriptive filename, `SHA256SUMS`,
  source commit/SDK revision, version, build log and flash instructions.
  Re-read the copied image and verify its SHA-256, then update the shared
  `FLASH-LATEST.md` with a relative link and Windows path. Preserve previous
  builds. Windows opens `\\10.10.0.214\workspace\builds\crosspoint-reader`.
  Never store the only output under `workspace/projects/crosspoint-reader`;
  that shared source checkout is disposable. A successful compilation does
  not establish physical-device validation.
- Default firmware handoffs to the web flasher: Xteink X4 Pro → Custom .bin.
  `/Volumes/workspace/builds/crosspoint-reader/FLASH-LATEST.md` identifies the
  currently validated image. Historical filenames alone do not establish which
  image should be flashed.

## Worktree lifecycle

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
- `scripts/fork-workflow.sh release` drops build output and the SDK checkout.
  Run it as the last command of the turn that ends the task (the global rules
  require it every time). Run it only once the work is delivered or preserved:
  commit/push authorized work and preserve/export wanted firmware, check logs
  and ignored local overrides such as `platformio.local.ini` first. A thread
  that is only investigating or has undelivered work first commits what it has
  to the task branch and preserves those files, then releases. The command
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
