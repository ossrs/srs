#!/bin/bash
# Run the C++ unit tests on Linux x64, in WSL on the Windows test host.
#
#   srs-utest-wsl.sh [--wip] [gtest-filter]
#
# Opt in with SRS_TEST_WINDOWS_HOST. windows-host-run.sh syncs this checkout to the host, with --wip as
# a snapshot of the working tree, and runs the --host step in the host checkout:
#   - In Git Bash, wsl.exe runs this script again with --wsl, in the default WSL distribution.
#   - In WSL, the commit is fetched into ~/projects/srs from its origin, the host's main repository.
#   - The detached worktree ~/projects/<host checkout name> is added at the commit, or moved to it;
#     a worktree with local changes is never reset.
#   - srs-utest-run.sh builds and runs the utest there, with its configure the first time.
#
# The first run builds the third-party libraries in WSL, several minutes; later runs are make-only.
# WSL needs the SRS build tools and a clone at ~/projects/srs whose origin is the host's main repository.
#
# Prints the utest output, then
#   RESULT linux-x64 utest PASS|FAIL <passed> passed, <failed> failed (<seconds>s)
# Exits with the utest's exit code (1 when the build fails), 2 when skipped, and 3 for bad arguments
# or a failed setup on the host or in WSL.
set -u
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

usage() {
  echo "Usage: $0 [--wip] [gtest-filter]" >&2
  exit 3
}

# On the Mac: sync the checkout to the host and run the --host step there.
mac_main() {
  local wip= checkout
  if [[ ${1:-} == --wip ]]; then
    wip=--wip
    shift
  fi
  [[ $# -le 1 && ${1:-} != -* ]] || usage
  checkout=$(cd "$SCRIPT_DIR/../../.." && pwd)
  bash "$SCRIPT_DIR/windows-host-run.sh" $wip "$checkout" "$SCRIPT_DIR/srs-utest-wsl.sh" --host "$@"
  exit $?
}

# In Git Bash on the host, in the host checkout: run the --wsl step in WSL at the same commit.
host_main() {
  local sha name self drive
  [[ -f skills/srs-develop/scripts/srs-utest-run.sh ]] || { echo "SETUP_FAIL: $PWD is not an SRS checkout"; exit 3; }
  command -v wsl.exe >/dev/null || { echo 'SKIP linux-x64: no wsl.exe on the host'; exit 2; }
  sha=$(git rev-parse HEAD)
  name=$(basename "$PWD")
  # This copy of the script, such as C:/Users/me/x.sh, as WSL sees it: /mnt/c/Users/me/x.sh.
  self=$(cygpath -m "$0")
  drive=${self%%:*}
  self=/mnt/$(echo "$drive" | tr '[:upper:]' '[:lower:]')${self#*:}
  # Git Bash would rewrite the /mnt path for a Windows program; WSL needs it as is.
  MSYS_NO_PATHCONV=1 wsl.exe --cd '~' -e bash -l "$self" --wsl "$sha" "$name" "$@"
  exit $?
}

# In WSL: sync the worktree to the commit, then build and run the utest.
wsl_main() {
  local sha=$1 name=$2 repo=$HOME/projects/srs dir log started status passed failed result
  shift 2
  dir=$HOME/projects/$name
  [[ -d $repo/.git ]] || { echo "SETUP_FAIL: no repository at $repo in WSL; clone it first"; exit 3; }
  if ! git -C "$repo" fetch -q origin "$sha"; then
    echo "SETUP_FAIL: cannot fetch $sha from origin $(git -C "$repo" remote get-url origin) in $repo"
    exit 3
  fi
  if [[ ! -e $dir ]]; then
    git -C "$repo" worktree add -q --detach "$dir" "$sha" || { echo "SETUP_FAIL: cannot add worktree $dir"; exit 3; }
    echo "SETUP: added worktree $dir"
  fi
  [[ -e $dir/.git ]] || { echo "SETUP_FAIL: $dir is not a git checkout"; exit 3; }
  [[ -z $(git -C "$dir" status --porcelain) ]] || { echo "SETUP_FAIL: $dir has local changes"; exit 3; }
  git -C "$dir" checkout -q --detach "$sha" || { echo "SETUP_FAIL: cannot check out $sha in $dir"; exit 3; }
  echo "SETUP: $dir at $sha"

  log=$dir/trunk/objs/utest-wsl.log
  mkdir -p "$dir/trunk/objs"
  started=$SECONDS
  bash "$dir/skills/srs-develop/scripts/srs-utest-run.sh" "$@" 2>&1 | tee "$log"
  status=${PIPESTATUS[0]}
  passed=$(sed -n 's/^\[  PASSED  \] \([0-9]*\) test.*/\1/p' "$log" | tail -n 1)
  failed=$(sed -n 's/^\[  FAILED  \] \([0-9]*\) test.*/\1/p' "$log" | tail -n 1)
  result=PASS
  [[ $status -eq 0 ]] || result=FAIL
  echo "RESULT linux-x64 utest $result ${passed:-0} passed, ${failed:-0} failed ($((SECONDS - started))s)"
  exit "$status"
}

case "${1:-}" in
  --host) shift; host_main "$@";;
  --wsl) [[ $# -ge 3 ]] || usage; shift; wsl_main "$@";;
  *) mac_main "$@";;
esac
