## Git Workflow and Repository Awareness

### Repository Detection Protocol

Always verify repository context before Git operations. At session start run
`git branch --show-current`, `git remote -v`, and `git status --short`.
Remotes may describe a fork (`origin` personal, `upstream` main), a direct
clone (`origin` main), or multiple collaborators; inspect them rather than
assuming their roles. A fork may use
`https://github.com/<your-username>/crosspoint-reader.git` for `origin` and
`https://github.com/crosspoint-reader/crosspoint-reader.git` for `upstream`.
In this fork `origin` is the personal fork and `official` is the official
project, fetch only; no remote is named `upstream`.

### Git Operation Rules

1. Integration branches and PR comparisons target `develop`, not `master` or the remote's symbolic HEAD.
2. The standing authorization (docs/FORK.md) covers pushing the task branch to `origin` and opening and merging this thread's PR on the fork. Never push to or open a PR against the official project without explicit approval from the human. Closing a PR unmerged is not covered.
3. Before a push, inspect remotes again and use `origin` for the feature branch unless the human specifies otherwise.
4. Never add Claude, Codex, or assistant self-attribution as a commit co-author or generated-by trailer.
5. When a change supersedes or adapts another person's PR, verify the original human author from Git/GitHub and add that person as `Co-Authored-By`; skip bot authors.

### Branch Naming Convention

Use `<type>/<short-description>` for new branches, with the same type prefix as
PR titles: `feat`, `fix`, `refactor`, `docs`, `test`, `chore`, or `perf`.

```text
feat/<short-description>          # New features
fix/<issue-number>-<description>  # Bug fixes
refactor/<component-name>         # Code refactoring
docs/<topic>                      # Documentation updates
```

**Examples**:

- `feat/sd-download-progress`
- `fix/123-orientation-crash`
- `refactor/hal-storage`

### Commit Message Format

**Pattern**:

```text
<type>: <short summary (50 chars max)>

<optional detailed description>
```

**Types**: `feat`, `fix`, `refactor`, `docs`, `test`, `chore`, `perf`

**Example**:

```text
feat: add real-time SD download progress bar

Implements progress tracking for book downloads using
UITheme progress bar component with heap-safe updates.

Tested in all 4 orientations with 5MB+ files.
```

### When to Commit

**A local commit may be made when**:

- Feature or bug fix is complete (the standing authorization replaces the human's approval of the commit)
- Refactoring preserves all functionality
- All tests pass (`pio run` succeeds)

**DO NOT commit when**:

- Build fails or the diff introduces new warnings
- Experimenting or debugging in progress
- Files excluded by `.gitignore` would be included — always run `git status` and cross-check against `.gitignore` before staging (e.g., `*.generated.h`, `.pio/`, `compile_commands.json`, `platformio.local.ini`)

**Rule**: **If uncertain whether the work is complete, verify it before committing.**

Hardware testing is the human's and is not a gate for a commit or PR here. The
agent lists the unverified device checks with a concrete test plan and never
claims the hardware result itself.

---
