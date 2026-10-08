#!/usr/bin/env bash
# Fork workflow helper: task branches reach develop through pull requests on
# the personal fork; develop itself moves only by rebasing onto the official
# project. docs/FORK.md ("Branches and pull requests") says when to use each
# command.
set -euo pipefail

OFFICIAL_REPO="crosspoint-reader/crosspoint-reader"
# Not named "upstream": T3 Code treats a remote of that name as the project's
# own repository and would look for pull requests there.
OFFICIAL_REMOTE="official"
BASE_BRANCH="develop"
SDK_PATH="freeink-sdk"
# Local toolchain and build area (docs/FORK.md, "Development and validation preferences").
BUILD_ROOT="${CROSSPOINT_BUILD_ROOT:-$HOME/.local/share/crosspoint-build}"
TOOL_BIN="${CROSSPOINT_TOOL_BIN:-$BUILD_ROOT/venv/bin}"
LLVM_BIN="${CROSSPOINT_LLVM_BIN:-/opt/homebrew/opt/llvm@22/bin}"
# Community fork read as a feature source (docs/fork-crossink.md); never a base.
CROSSINK_REMOTE="crossink"
CROSSINK_URL="https://github.com/uxjulia/CrossInk.git"
CI_ENVS="default sticky x4pro x4c papermono metalio_eink4 eego_a4"

die() {
  echo "fork-workflow: $*" >&2
  exit 1
}

usage() {
  cat <<'EOF'
Usage: scripts/fork-workflow.sh <command>

  setup                 Verify remotes and set the per-clone git and gh defaults.
  status                Show official/fork/branch state and open pull requests.
  prepare               Check out the pinned SDK submodule in this worktree.
  check [--fast|--full] Run the checks CI would run, locally. Default: format,
                        host tests (Release and sanitizers) and the X4 Pro build.
                        --fast: format and Release host tests only.
                        --full: adds cppcheck and every firmware target CI builds.
  pr [gh pr create options]
                        Push the current task branch to the fork and open a pull
                        request against the fork's develop (never the official
                        project).
  restack               Rebase the current task branch onto the fork's develop
                        after develop was rebased, then update its pull request.
  release               Drop build output and the SDK checkout so T3 Code can
                        remove this worktree by itself. Explicit cleanup only.
  sync-publish <commit> Make <commit> (a validated rebase of develop onto
                        official/develop) the new develop and force-push it with
                        a lease. Not for task work; that goes through `pr`.
EOF
}

# owner/name from a GitHub remote URL (https or ssh).
repo_slug() {
  local url
  url=$(git remote get-url "$1" 2>/dev/null) || die "remote '$1' is missing"
  url=${url%.git}
  case "$url" in
    https://github.com/*) echo "${url#https://github.com/}" ;;
    git@github.com:*) echo "${url#git@github.com:}" ;;
    ssh://git@github.com/*) echo "${url#ssh://git@github.com/}" ;;
    *) die "remote '$1' is not a GitHub URL: $url" ;;
  esac
}

# Sets FORK_REPO. Remote names do not establish their role; the URLs do.
check_remotes() {
  [ "$(repo_slug "$OFFICIAL_REMOTE")" = "$OFFICIAL_REPO" ] ||
    die "remote '$OFFICIAL_REMOTE' is not $OFFICIAL_REPO"
  FORK_REPO=$(repo_slug origin)
  [ "$FORK_REPO" != "$OFFICIAL_REPO" ] || die "remote 'origin' is the official repository, not a personal fork"
  ! git remote get-url upstream >/dev/null 2>&1 ||
    die "a remote named 'upstream' exists; rename it: git remote rename upstream $OFFICIAL_REMOTE"
}

current_branch() {
  git symbolic-ref --quiet --short HEAD || die "HEAD is detached; check out a task branch"
}

require_clean() {
  [ -z "$(git status --porcelain --ignore-submodules=none)" ] || die "the working tree has uncommitted changes"
}

require_task_branch() {
  local branch=$1
  [ "$branch" != "$BASE_BRANCH" ] || die "'$BASE_BRANCH' is the integration branch; work on a task branch"
  [ "$branch" != "master" ] || die "'master' is not a task branch"
}

fetch_all() {
  git fetch --quiet "$OFFICIAL_REMOTE" "$BASE_BRANCH"
  git fetch --quiet --prune origin
}

# Path of the worktree that has develop checked out, or empty.
develop_worktree() {
  git worktree list --porcelain | awk -v ref="refs/heads/$BASE_BRANCH" '
    /^worktree / { path = substr($0, 10) }
    /^branch / && $2 == ref { print path }'
}

main_worktree() {
  git worktree list --porcelain | awk '/^worktree / { print substr($0, 10); exit }'
}

open_prs() {
  gh pr list --repo "$FORK_REPO" --base "$BASE_BRANCH" --state open \
    --json number,headRefName,title --template \
    '{{range .}}#{{.number}}	{{.headRefName}}	{{.title}}{{"\n"}}{{end}}'
}

# Build directories kept outside the worktree, one set per worktree.
ci_dir() {
  echo "$BUILD_ROOT/ci/$(basename "$(git rev-parse --show-toplevel)")"
}

cmd_setup() {
  check_remotes
  # gh resolves a fork's default repository to its parent unless told otherwise.
  gh repo set-default "$FORK_REPO"
  git remote set-url --push "$OFFICIAL_REMOTE" DISABLED-fetch-only
  git remote get-url "$CROSSINK_REMOTE" >/dev/null 2>&1 || git remote add "$CROSSINK_REMOTE" "$CROSSINK_URL"
  git remote set-url --push "$CROSSINK_REMOTE" DISABLED-fetch-only
  git config rerere.enabled true
  git config remote.pushDefault origin
  git config fetch.prune true
  echo "origin   = $FORK_REPO (pull requests target $BASE_BRANCH here)"
  echo "$OFFICIAL_REMOTE = $OFFICIAL_REPO (fetch only)"
  echo "$CROSSINK_REMOTE = $CROSSINK_URL (fetch only, feature source)"
  gh api "repos/$FORK_REPO" --jq \
    '"merge commits allowed: \(.allow_merge_commit)\nsquash allowed: \(.allow_squash_merge)\nrebase allowed: \(.allow_rebase_merge)\ndelete branch on merge: \(.delete_branch_on_merge)"'
}

cmd_status() {
  check_remotes
  fetch_all
  local branch behind
  branch=$(git symbolic-ref --quiet --short HEAD || echo "(detached)")
  behind=$(git rev-list --count "origin/$BASE_BRANCH..$OFFICIAL_REMOTE/$BASE_BRANCH")
  echo "$OFFICIAL_REMOTE/$BASE_BRANCH $(git rev-parse --short "$OFFICIAL_REMOTE/$BASE_BRANCH")"
  echo "origin/$BASE_BRANCH   $(git rev-parse --short "origin/$BASE_BRANCH") ($(git rev-list --count "$OFFICIAL_REMOTE/$BASE_BRANCH..origin/$BASE_BRANCH") downstream commits)"
  if [ "$behind" -gt 0 ]; then
    echo "the official project is $behind commit(s) ahead: sync $BASE_BRANCH before opening or merging pull requests"
  else
    echo "$BASE_BRANCH contains the official tip"
  fi
  if [ "$branch" != "(detached)" ] && [ "$branch" != "$BASE_BRANCH" ]; then
    echo "$branch: $(git rev-list --count "origin/$BASE_BRANCH..HEAD") commit(s) ahead of origin/$BASE_BRANCH, $(git rev-list --count "HEAD..origin/$BASE_BRANCH") behind"
  fi
  echo "open pull requests into $FORK_REPO:$BASE_BRANCH:"
  open_prs
}

cmd_prepare() {
  local top reference
  top=$(git rev-parse --show-toplevel)
  reference="$(main_worktree)/$SDK_PATH"
  # Borrow objects from the permanent checkout's SDK so nothing is downloaded.
  if [ "$top" != "$(main_worktree)" ] && [ -e "$reference/.git" ]; then
    git -C "$top" submodule update --init --reference "$reference" "$SDK_PATH"
  else
    git -C "$top" submodule update --init "$SDK_PATH"
  fi
}

run_step() {
  local name=$1 log=$2
  shift 2
  printf '%-28s ' "$name"
  if "$@" >"$log" 2>&1; then
    echo "ok"
  else
    echo "FAILED (see $log)"
    return 1
  fi
}

# These run inside `if`, where `set -e` is off, so each step is chained.
format_check() {
  ./bin/clang-format-fix && git diff --exit-code
}

host_tests() {
  local dir=$1 sanitizers=$2
  if [ "$sanitizers" = ON ]; then
    CC="$LLVM_BIN/clang" CXX="$LLVM_BIN/clang++" cmake -S test -B "$dir" -DCMAKE_BUILD_TYPE=Release -DCROSSPOINT_TEST_SANITIZERS=ON || return 1
  else
    cmake -S test -B "$dir" -DCMAKE_BUILD_TYPE=Release || return 1
  fi
  cmake --build "$dir" -j 8 || return 1
  # GlyphRasterParity takes about 70 s under the sanitizers.
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --test-dir "$dir" --output-on-failure --timeout 180 -j 8
}

cmd_check() {
  local mode=${1:-default} dir failed=0 env
  case "$mode" in
    default | --fast | --full) ;;
    *) die "usage: check [--fast|--full]" ;;
  esac
  [ -x "$TOOL_BIN/pio" ] || die "no toolchain in $TOOL_BIN (set CROSSPOINT_TOOL_BIN)"
  cd "$(git rev-parse --show-toplevel)"
  PATH="$TOOL_BIN:$PATH"
  export PATH
  dir=$(ci_dir)
  mkdir -p "$dir"
  cmd_prepare >"$dir/prepare.log" 2>&1 || die "could not check out the SDK (see $dir/prepare.log)"
  echo "checking $(git rev-parse --short HEAD) in $(pwd); logs in $dir"

  # The formatter rewrites files, so it needs a clean tree to compare against.
  if [ -n "$(git status --porcelain)" ]; then
    echo "format                       skipped: uncommitted changes (commit first)"
    failed=1
  else
    run_step "format" "$dir/format.log" format_check || failed=1
  fi
  run_step "host tests (Release)" "$dir/tests-release.log" host_tests "$dir/tests-release" OFF || failed=1
  if [ "$mode" != --fast ]; then
    run_step "host tests (sanitizers)" "$dir/tests-sanitizers.log" host_tests "$dir/tests-sanitizers" ON || failed=1
    run_step "build x4pro-gh_release" "$dir/build-x4pro-gh_release.log" pio run -e x4pro-gh_release || failed=1
  fi
  if [ "$mode" = --full ]; then
    run_step "cppcheck" "$dir/cppcheck.log" pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high || failed=1
    for env in $CI_ENVS; do
      run_step "build $env" "$dir/build-$env.log" pio run -e "$env" || failed=1
    done
  fi
  [ "$failed" -eq 0 ] || die "checks failed"
  echo "all checks passed"
}

cmd_pr() {
  check_remotes
  local branch
  branch=$(current_branch)
  require_task_branch "$branch"
  require_clean
  fetch_all
  git merge-base --is-ancestor "origin/$BASE_BRANCH" HEAD ||
    die "$branch is not based on the current origin/$BASE_BRANCH; run: scripts/fork-workflow.sh restack"
  [ "$(git rev-list --count "origin/$BASE_BRANCH..HEAD")" -gt 0 ] || die "$branch has no commits to propose"
  [ "$(git rev-list --count --merges "origin/$BASE_BRANCH..HEAD")" -eq 0 ] || die "$branch contains merge commits"

  # The branch is only ever pushed to the fork; a rebased branch needs the lease.
  git push --force-with-lease -u origin "HEAD:refs/heads/$branch"

  local existing
  existing=$(gh pr list --repo "$FORK_REPO" --head "$branch" --base "$BASE_BRANCH" --state open --json url --jq '.[0].url // empty')
  if [ -n "$existing" ]; then
    echo "$existing"
    return
  fi
  if [ $# -eq 0 ]; then
    set -- --fill
  fi
  gh pr create --repo "$FORK_REPO" --base "$BASE_BRANCH" --head "$branch" "$@"
}

cmd_restack() {
  check_remotes
  local branch base
  branch=$(current_branch)
  require_task_branch "$branch"
  require_clean
  fetch_all
  if git merge-base --is-ancestor "origin/$BASE_BRANCH" HEAD; then
    echo "$branch is already based on origin/$BASE_BRANCH"
    return
  fi
  # develop is rebased, so the branch's old base is found in origin/develop's reflog.
  base=$(git merge-base --fork-point "origin/$BASE_BRANCH" HEAD) ||
    die "cannot find where $branch left $BASE_BRANCH; run: git rebase --onto origin/$BASE_BRANCH <old base> $branch"
  echo "rebasing $(git rev-list --count "$base..HEAD") commit(s) of $branch onto origin/$BASE_BRANCH"
  git rebase --onto "origin/$BASE_BRANCH" "$base"
  if git rev-parse --verify --quiet "refs/remotes/origin/$branch" >/dev/null; then
    git push --force-with-lease origin "HEAD:refs/heads/$branch"
  fi
}

# T3 Code removes a worktree with a plain `git worktree remove` and only when it
# holds no ignored files. Git refuses that command while a submodule is checked
# out, so both have to go. Everything dropped here is rebuilt by `prepare` and
# `check`; T3 Code recreates a removed worktree from its branch on the next turn.
cmd_release() {
  local top gitdir sdk recorded head
  top=$(git rev-parse --show-toplevel)
  [ -f "$top/.git" ] || die "$top is the permanent checkout, not a thread's worktree"
  cd "$top"
  gitdir=$(git rev-parse --absolute-git-dir)

  sdk="$top/$SDK_PATH"
  if [ -e "$sdk/.git" ]; then
    [ -z "$(git -C "$sdk" status --porcelain)" ] || die "$SDK_PATH has uncommitted changes"
    recorded=$(git rev-parse "HEAD:$SDK_PATH")
    head=$(git -C "$sdk" rev-parse HEAD)
    if [ "$head" != "$recorded" ] &&
      [ -z "$(git -C "$sdk" for-each-ref --contains "$head" refs/remotes | head -n 1)" ]; then
      die "$SDK_PATH is at $head, which is neither the pinned commit nor on a remote branch; push it first"
    fi
    git submodule deinit --quiet --force --all
  fi
  rm -rf "$gitdir/modules"
  # Twice -f: PlatformIO's libdeps include nested git checkouts.
  git clean -ffdX --quiet
  rm -rf "$(ci_dir)"

  local ignored
  ignored=$(git ls-files --others --ignored --exclude-standard --directory | grep -v -E '(^|/)node_modules/$' || true)
  if [ -n "$ignored" ]; then
    echo "ignored files remain; T3 Code will keep this worktree:"
    echo "$ignored"
    return 1
  fi
  if [ -n "$(git status --porcelain)" ]; then
    echo "released, but uncommitted changes remain; T3 Code will keep this worktree:"
    git status --short
    return 1
  fi
  if [ -n "$(git rev-list "HEAD" --not --remotes=origin | head -n 1)" ]; then
    echo "released; note that HEAD has commits not pushed to origin"
  fi
  echo "released: T3 Code can remove this worktree"
}

cmd_sync_publish() {
  [ $# -eq 1 ] || die "usage: sync-publish <commit>"
  check_remotes
  fetch_all
  local candidate old official_tip worktree backup
  candidate=$(git rev-parse --verify --quiet "$1^{commit}") || die "'$1' is not a commit"
  old=$(git rev-parse "origin/$BASE_BRANCH")
  official_tip=$(git rev-parse "$OFFICIAL_REMOTE/$BASE_BRANCH")

  ! git merge-base --is-ancestor "$official_tip" "$old" ||
    die "origin/$BASE_BRANCH already contains the official tip; nothing to sync (task work goes through a pull request)"
  git merge-base --is-ancestor "$official_tip" "$candidate" ||
    die "$1 does not contain $OFFICIAL_REMOTE/$BASE_BRANCH ($(git rev-parse --short "$official_tip"))"
  [ "$(git rev-list --count --merges "$official_tip..$candidate")" -eq 0 ] ||
    die "$1 has merge commits above $OFFICIAL_REMOTE/$BASE_BRANCH"

  worktree=$(develop_worktree)
  if [ -n "$worktree" ]; then
    [ -z "$(git -C "$worktree" status --porcelain --ignore-submodules=none)" ] ||
      die "$worktree has uncommitted changes"
    # A pull request merged on GitHub leaves the local branch behind.
    git -C "$worktree" merge --quiet --ff-only "origin/$BASE_BRANCH" ||
      die "local $BASE_BRANCH in $worktree has commits that are not on origin/$BASE_BRANCH"
    [ "$(git -C "$worktree" rev-parse HEAD)" = "$old" ] ||
      die "local $BASE_BRANCH in $worktree differs from origin/$BASE_BRANCH"
  else
    [ "$(git rev-parse "refs/heads/$BASE_BRANCH")" = "$old" ] ||
      die "local $BASE_BRANCH differs from origin/$BASE_BRANCH"
  fi

  echo "downstream commits: $(git rev-list --count "$(git merge-base "$old" "$official_tip")..$old") before, $(git rev-list --count "$official_tip..$candidate") after"
  backup="refs/fork-backup/$BASE_BRANCH-$(date -u +%Y%m%d-%H%M%S)-$(git rev-parse --short "$old")"
  git update-ref "$backup" "$old"
  echo "previous $BASE_BRANCH kept at $backup"

  # Publish first: a rejected push must leave the local branch where it was.
  git push --force-with-lease="$BASE_BRANCH:$old" origin "$candidate:refs/heads/$BASE_BRANCH"
  git fetch --quiet origin
  [ "$(git rev-parse "origin/$BASE_BRANCH")" = "$candidate" ] || die "origin/$BASE_BRANCH is not $candidate after the push"
  if [ -n "$worktree" ]; then
    git -C "$worktree" reset --quiet --hard "$candidate"
    git -C "$worktree" submodule update --init ||
      echo "fork-workflow: update the submodule in $worktree by hand (is its commit pushed to the SDK fork?)" >&2
  else
    git update-ref "refs/heads/$BASE_BRANCH" "$candidate" "$old"
  fi
  echo "origin/$BASE_BRANCH is now $(git rev-parse --short "$candidate") on official $(git rev-parse --short "$official_tip")"
  echo "open pull requests to restack (scripts/fork-workflow.sh restack in each worktree):"
  open_prs
}

command=${1:-}
[ $# -gt 0 ] && shift
case "$command" in
  setup) cmd_setup "$@" ;;
  status) cmd_status "$@" ;;
  prepare) cmd_prepare "$@" ;;
  check) cmd_check "$@" ;;
  pr) cmd_pr "$@" ;;
  restack) cmd_restack "$@" ;;
  release) cmd_release "$@" ;;
  sync-publish) cmd_sync_publish "$@" ;;
  -h | --help | help | "") usage ;;
  *)
    usage >&2
    exit 2
    ;;
esac
