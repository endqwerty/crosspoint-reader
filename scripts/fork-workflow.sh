#!/usr/bin/env bash
# Fork workflow helper: task branches reach develop through pull requests on
# the personal fork; develop itself moves only by rebasing onto upstream.
# docs/FORK.md ("Branches and pull requests") describes when to use each command.
set -euo pipefail

UPSTREAM_REPO="crosspoint-reader/crosspoint-reader"
BASE_BRANCH="develop"

die() {
  echo "fork-workflow: $*" >&2
  exit 1
}

usage() {
  cat <<'EOF'
Usage: scripts/fork-workflow.sh <command>

  setup                 Verify remotes and set the per-clone git and gh defaults.
  status                Show upstream/fork/branch state and open pull requests.
  pr [gh pr create options]
                        Push the current task branch to the fork and open a pull
                        request against the fork's develop (never upstream).
  restack               Rebase the current task branch onto the fork's develop
                        after develop was rebased, then update its pull request.
  sync-publish <commit> Make <commit> (a validated rebase of develop onto
                        upstream/develop) the new develop and force-push it with
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
  [ "$(repo_slug upstream)" = "$UPSTREAM_REPO" ] || die "remote 'upstream' is not $UPSTREAM_REPO"
  FORK_REPO=$(repo_slug origin)
  [ "$FORK_REPO" != "$UPSTREAM_REPO" ] || die "remote 'origin' is the official repository, not a personal fork"
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
  git fetch --quiet upstream "$BASE_BRANCH"
  git fetch --quiet --prune origin
}

# Path of the worktree that has develop checked out, or empty.
develop_worktree() {
  git worktree list --porcelain | awk -v ref="refs/heads/$BASE_BRANCH" '
    /^worktree / { path = substr($0, 10) }
    /^branch / && $2 == ref { print path }'
}

open_prs() {
  gh pr list --repo "$FORK_REPO" --base "$BASE_BRANCH" --state open \
    --json number,headRefName,title --template \
    '{{range .}}#{{.number}}	{{.headRefName}}	{{.title}}{{"\n"}}{{end}}'
}

cmd_setup() {
  check_remotes
  # gh resolves a fork's default repository to its parent unless told otherwise.
  gh repo set-default "$FORK_REPO"
  git config rerere.enabled true
  git config remote.pushDefault origin
  git config fetch.prune true
  echo "origin   = $FORK_REPO (pull requests target $BASE_BRANCH here)"
  echo "upstream = $UPSTREAM_REPO (fetch only)"
  gh api "repos/$FORK_REPO" --jq \
    '"merge commits allowed: \(.allow_merge_commit) (want false)\nsquash allowed: \(.allow_squash_merge)\nrebase allowed: \(.allow_rebase_merge)\ndelete branch on merge: \(.delete_branch_on_merge) (want true)"'
}

cmd_status() {
  check_remotes
  fetch_all
  local branch behind
  branch=$(git symbolic-ref --quiet --short HEAD || echo "(detached)")
  behind=$(git rev-list --count "origin/$BASE_BRANCH..upstream/$BASE_BRANCH")
  echo "upstream/$BASE_BRANCH $(git rev-parse --short "upstream/$BASE_BRANCH")"
  echo "origin/$BASE_BRANCH   $(git rev-parse --short "origin/$BASE_BRANCH") ($(git rev-list --count "upstream/$BASE_BRANCH..origin/$BASE_BRANCH") downstream commits)"
  if [ "$behind" -gt 0 ]; then
    echo "upstream is $behind commit(s) ahead: sync $BASE_BRANCH before opening or merging pull requests"
  else
    echo "$BASE_BRANCH contains the upstream tip"
  fi
  if [ "$branch" != "(detached)" ] && [ "$branch" != "$BASE_BRANCH" ]; then
    echo "$branch: $(git rev-list --count "origin/$BASE_BRANCH..HEAD") commit(s) ahead of origin/$BASE_BRANCH, $(git rev-list --count "HEAD..origin/$BASE_BRANCH") behind"
  fi
  echo "open pull requests into $FORK_REPO:$BASE_BRANCH:"
  open_prs
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

cmd_sync_publish() {
  [ $# -eq 1 ] || die "usage: sync-publish <commit>"
  check_remotes
  fetch_all
  local candidate old upstream_tip worktree backup
  candidate=$(git rev-parse --verify --quiet "$1^{commit}") || die "'$1' is not a commit"
  old=$(git rev-parse "origin/$BASE_BRANCH")
  upstream_tip=$(git rev-parse "upstream/$BASE_BRANCH")

  ! git merge-base --is-ancestor "$upstream_tip" "$old" ||
    die "origin/$BASE_BRANCH already contains the upstream tip; nothing to sync (task work goes through a pull request)"
  git merge-base --is-ancestor "$upstream_tip" "$candidate" ||
    die "$1 does not contain upstream/$BASE_BRANCH ($(git rev-parse --short "$upstream_tip"))"
  [ "$(git rev-list --count --merges "$upstream_tip..$candidate")" -eq 0 ] ||
    die "$1 has merge commits above upstream/$BASE_BRANCH"

  worktree=$(develop_worktree)
  if [ -n "$worktree" ]; then
    [ -z "$(git -C "$worktree" status --porcelain --ignore-submodules=none)" ] ||
      die "$worktree has uncommitted changes"
    [ "$(git -C "$worktree" rev-parse HEAD)" = "$old" ] ||
      die "local $BASE_BRANCH in $worktree differs from origin/$BASE_BRANCH"
  else
    [ "$(git rev-parse "refs/heads/$BASE_BRANCH")" = "$old" ] ||
      die "local $BASE_BRANCH differs from origin/$BASE_BRANCH"
  fi

  echo "downstream commits: $(git rev-list --count "$(git merge-base "$old" "$upstream_tip")..$old") before, $(git rev-list --count "$upstream_tip..$candidate") after"
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
  echo "origin/$BASE_BRANCH is now $(git rev-parse --short "$candidate") on upstream $(git rev-parse --short "$upstream_tip")"
  echo "open pull requests to restack (scripts/fork-workflow.sh restack in each worktree):"
  open_prs
}

command=${1:-}
[ $# -gt 0 ] && shift
case "$command" in
  setup) cmd_setup "$@" ;;
  status) cmd_status "$@" ;;
  pr) cmd_pr "$@" ;;
  restack) cmd_restack "$@" ;;
  sync-publish) cmd_sync_publish "$@" ;;
  -h | --help | help | "") usage ;;
  *)
    usage >&2
    exit 2
    ;;
esac
