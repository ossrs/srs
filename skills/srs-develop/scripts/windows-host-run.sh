#!/bin/bash
# Run a local script on a remote Windows host over SSH, from macOS or Linux, in a checkout on the host
# that mirrors a local one at the same commit.
#
#   windows-host-run.sh [--wip] <checkout> <script> [args]
#
#   checkout   the local git checkout to mirror on the host
#   script     the local script to run there, a path, or a script name in this directory
#   args       passed to the script unchanged
#
# Opt in with SRS_TEST_WINDOWS_HOST, the SSH host of the Windows machine, set in your shell profile
# such as ~/.zshrc:
#   export SRS_TEST_WINDOWS_HOST=<srs-test-windows-host>
#
# Nothing about the setup is hardcoded; it mirrors the local checkout:
#   - The local checkout is $HOME/<path>, and its main repository, for a linked worktree, $HOME/<main>.
#   - Its branch tracks <remote>/<branch>; a branch without an upstream cannot be synced.
#   - Commits are pushed only to that upstream, never to origin, which is left to the user.
#
# Without --wip, the checkout must be clean. Its commit is pushed to the upstream when it is not there
# yet, and the host checkout at <path> is fast-forwarded to it, never reset.
#
# With --wip, the working tree, new files included, is snapshotted into a commit object on no branch,
# with a temporary index, git write-tree and git commit-tree; the branch, the index and the files stay
# untouched. The snapshot is force-pushed to <remote>/<branch>-wip and the host scratch worktree
# <path>-wip, the only one ever reset, is reset to it.
#
# Setup on the host, which aborts the run when it fails:
#   - The main repository must exist at the same <main> path under the host's home; clone it first.
#   - The remote is added with the same name and URL when missing, and must match when present.
#   - A missing worktree is added at <path> on the same branch, or at <path>-wip detached.
#   - The checkout at <path> must be on the branch and clean.
#
# Then the script is copied to the host and run in the host checkout with Git Bash, after vcvars64
# loads the MSVC x64 environment. The host needs OpenSSH with key login and PowerShell as its default
# shell, which passes the exit code on, Git for Windows, and Visual Studio or its Build Tools with the
# C++ x64 tools.
#
# Each run holds the windows-host product lock of full-tier-lock.sh, so two runs never share the host.
#
# Prints SETUP lines, then the script's output. Exits with the script's exit code; before the script
# runs, exit 2 when the run was skipped because the host is not set up for it or the checkout has
# uncommitted changes, and 3 for bad arguments or when setup or sync failed.

SCRIPTS=$(cd "$(dirname "$0")" && pwd)

usage() {
  echo "Usage: $0 [--wip] <checkout> <script> [args]"
  exit 3
}

WIP=0
if [[ $1 == --wip ]]; then
  WIP=1
  shift
fi
if [[ $# -lt 2 || $1 == -* ]]; then
  usage
fi
CHECKOUT=$1
SCRIPT=$2
shift 2

if [[ ! -f $SCRIPT && $SCRIPT != */* && -f $SCRIPTS/$SCRIPT ]]; then
  SCRIPT=$SCRIPTS/$SCRIPT
fi
if [[ ! -f $SCRIPT ]]; then
  echo "SETUP_FAIL: no script $SCRIPT"
  exit 3
fi

HOST=$SRS_TEST_WINDOWS_HOST
if [[ -z $HOST ]]; then
  echo "SKIP windows: SRS_TEST_WINDOWS_HOST is not set"
  exit 2
fi

# The local layout to mirror.
TOP=$(git -C "$CHECKOUT" rev-parse --show-toplevel 2>/dev/null)
if [[ -z $TOP ]]; then
  echo "SETUP_FAIL: $CHECKOUT is not a git checkout"
  exit 3
fi
MAIN=$(dirname "$(git -C "$TOP" rev-parse --path-format=absolute --git-common-dir)")
REL=${TOP#$HOME/}
MAIN_REL=${MAIN#$HOME/}
if [[ $REL == /* || $MAIN_REL == /* ]]; then
  echo "SETUP_FAIL: $TOP and $MAIN must be under $HOME, to find them under the host's home"
  exit 3
fi
BRANCH=$(git -C "$TOP" symbolic-ref --short -q HEAD)
UPSTREAM=$(git -C "$TOP" rev-parse --abbrev-ref --symbolic-full-name '@{u}' 2>/dev/null) || UPSTREAM=
if [[ -z $BRANCH || -z $UPSTREAM ]]; then
  echo "SETUP_FAIL: $TOP must be on a branch that tracks a remote branch"
  exit 3
fi
REMOTE=${UPSTREAM%%/*}
REMOTE_BRANCH=${UPSTREAM#*/}
URL=$(git -C "$TOP" remote get-url "$REMOTE")
SHA=$(git -C "$TOP" rev-parse HEAD)
if [[ $WIP == 0 && -n $(git -C "$TOP" status --porcelain) ]]; then
  echo "SKIP windows: $TOP has uncommitted changes; without --wip the host runs commits only"
  exit 2
fi
if [[ $WIP == 1 && $REMOTE == origin ]]; then
  echo "SETUP_FAIL: --wip pushes a snapshot branch to the upstream, but $BRANCH tracks origin; track a personal remote"
  exit 3
fi

if ! ssh -o BatchMode=yes -o ConnectTimeout=5 "$HOST" exit >/dev/null 2>&1; then
  echo "SKIP windows: cannot ssh to $HOST"
  exit 2
fi

# One run at a time on the host. A run can take longer than the lock's default 300s, so the owner's
# pid, not the age, frees it.
export SRS_FULL_TIER_LOCK_TIMEOUT=3600
echo "lock: waiting, $(bash "$SCRIPTS/full-tier-lock.sh" --product windows-host status)"
LOCK_START=$(date +%s)
if ! TOKEN=$(bash "$SCRIPTS/full-tier-lock.sh" --product windows-host acquire --wait 600 --pid $$); then
  echo "SKIP windows: the windows-host lock was not acquired in 600s"
  exit 2
fi
echo "lock: acquired after $(( $(date +%s) - LOCK_START ))s"

# The files copied to the host's temp folder, named by this process so parallel runs never share them.
mkdir -p /tmp/srs-windows-host-run
PS1=windows-host-run-$$.ps1
WRAPPER=windows-host-run-$$-wrapper.sh
RUNNER=windows-host-run-$$-$(basename "$SCRIPT")
REMOTE_TEMP=AppData/Local/Temp
cleanup() {
  ssh -o BatchMode=yes "$HOST" "powershell -NoProfile -Command Remove-Item -ErrorAction SilentlyContinue $REMOTE_TEMP/$PS1,$REMOTE_TEMP/$WRAPPER,$REMOTE_TEMP/$RUNNER" >/dev/null 2>&1
  rm -f /tmp/srs-windows-host-run/windows-host-run-$$.ps1 /tmp/srs-windows-host-run/windows-host-run-$$-wrapper.sh \
    /tmp/srs-windows-host-run/windows-host-run-$$.out /tmp/srs-windows-host-run/index-$$
  bash "$SCRIPTS/full-tier-lock.sh" --product windows-host release "$TOKEN"
}
trap cleanup EXIT

# Without --wip, push the commit to the upstream, so the host can fetch it.
HOST_REL=$REL
if [[ $WIP == 0 ]] && ! git -C "$TOP" merge-base --is-ancestor HEAD "$UPSTREAM"; then
  if [[ $REMOTE == origin ]]; then
    echo "SETUP_FAIL: $SHA is not on $UPSTREAM; push it, or track a personal remote"
    exit 3
  fi
  echo "SETUP: push $SHA to $UPSTREAM"
  if ! git -C "$TOP" push -q "$REMOTE" "HEAD:refs/heads/$REMOTE_BRANCH"; then
    echo "SETUP_FAIL: git push $REMOTE failed; a rewritten branch needs a force push by the user"
    exit 3
  fi
fi

# With --wip, snapshot the working tree through a temporary index and force-push it to <branch>-wip.
if [[ $WIP == 1 ]]; then
  HOST_REL=$REL-wip
  rm -f /tmp/srs-windows-host-run/index-$$
  if ! GIT_INDEX_FILE=/tmp/srs-windows-host-run/index-$$ git -C "$TOP" read-tree HEAD ||
     ! GIT_INDEX_FILE=/tmp/srs-windows-host-run/index-$$ git -C "$TOP" add -A ||
     ! TREE=$(GIT_INDEX_FILE=/tmp/srs-windows-host-run/index-$$ git -C "$TOP" write-tree) ||
     ! SHA=$(git -C "$TOP" commit-tree "$TREE" -p HEAD -m "WIP snapshot of $BRANCH for the Windows host."); then
    echo "SETUP_FAIL: cannot snapshot the working tree of $TOP"
    exit 3
  fi
  rm -f /tmp/srs-windows-host-run/index-$$
  echo "SETUP: push snapshot $SHA to $UPSTREAM-wip"
  if ! git -C "$TOP" push -q -f "$REMOTE" "$SHA:refs/heads/$REMOTE_BRANCH-wip"; then
    echo "SETUP_FAIL: git push $REMOTE $REMOTE_BRANCH-wip failed"
    exit 3
  fi
fi

# The wrapper runs the script with the arguments, quoted here so neither SSH nor PowerShell sees them.
{
  echo '#!/bin/bash'
  printf 'exec bash "$1"'
  if [[ $# -gt 0 ]]; then printf ' %q' "$@"; fi
  echo
} > /tmp/srs-windows-host-run/$WRAPPER

# The PowerShell side: -Step setup mirrors the checkout and syncs it to the commit, and -Step run
# loads MSVC and runs the script through the wrapper in the checkout.
cat > /tmp/srs-windows-host-run/$PS1 <<'EOF'
param([string]$Step, [string]$Rel, [string]$MainRel, [string]$Branch, [string]$Remote,
      [string]$RemoteBranch, [string]$Url, [string]$Wip, [string]$Sha, [string]$Wrapper, [string]$Runner)
# The paths are relative to the home folder, where SSH starts.
$dir = Join-Path $HOME $Rel
$main = Join-Path $HOME $MainRel

if ($Step -eq 'setup') {
    if (-not (Test-Path "$main\.git")) { "SETUP_FAIL: no repository at $main; clone it first"; exit 3 }

    $cur = git -C $main remote get-url $Remote 2>$null
    if (-not $cur) {
        git -C $main remote add $Remote $Url
        if ($LASTEXITCODE -ne 0) { "SETUP_FAIL: cannot add remote $Remote to $main"; exit 3 }
        "SETUP: added remote $Remote $Url to $main"
    } elseif ($cur -ne $Url) {
        "SETUP_FAIL: remote $Remote of $main is $cur, not $Url"; exit 3
    }
    git -C $main fetch -q $Remote
    if ($LASTEXITCODE -ne 0) { "SETUP_FAIL: cannot fetch $Remote in $main"; exit 3 }
    git -C $main cat-file -e "$Sha^{commit}" 2>$null
    if ($LASTEXITCODE -ne 0) { "SETUP_FAIL: $Remote has no $Sha"; exit 3 }

    if ($Wip -eq '1') {
        if (-not (Test-Path $dir)) {
            git -C $main worktree add -q --detach $dir $Sha
            if ($LASTEXITCODE -ne 0) { "SETUP_FAIL: cannot add worktree $dir"; exit 3 }
            "SETUP: added scratch worktree $dir"
        }
        if (-not (Test-Path "$dir\.git")) { "SETUP_FAIL: $dir is not a git checkout"; exit 3 }
        Set-Location $dir
        git checkout -q -f --detach $Sha
        if ($LASTEXITCODE -eq 0) { git clean -q -fd }
        if ($LASTEXITCODE -ne 0 -or (git rev-parse HEAD) -ne $Sha) { "SETUP_FAIL: cannot reset $dir to $Sha"; exit 3 }
        "SETUP: $dir at $Sha"
        exit 0
    }

    if (-not (Test-Path $dir)) {
        git -C $main show-ref --verify -q "refs/heads/$Branch"
        if ($LASTEXITCODE -eq 0) { git -C $main worktree add -q $dir $Branch }
        else { git -C $main worktree add -q --track -b $Branch $dir "$Remote/$RemoteBranch" }
        if ($LASTEXITCODE -ne 0) { "SETUP_FAIL: cannot add worktree $dir on $Branch"; exit 3 }
        "SETUP: added worktree $dir on $Branch"
    }
    if (-not (Test-Path "$dir\.git")) { "SETUP_FAIL: $dir is not a git checkout"; exit 3 }
    Set-Location $dir
    $cur = git symbolic-ref --short -q HEAD
    if ($cur -ne $Branch) { "SETUP_FAIL: $dir is on '$cur', not $Branch"; exit 3 }
    if (git status --porcelain) { "SETUP_FAIL: $dir has local changes"; exit 3 }
    git merge --ff-only -q $Sha
    if ($LASTEXITCODE -ne 0 -or (git rev-parse HEAD) -ne $Sha) { "SETUP_FAIL: cannot fast-forward $Branch in $dir to $Sha"; exit 3 }
    "SETUP: $dir at $Sha"
    exit 0
}

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = $null
if (Test-Path $vswhere) {
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
$bash = "$env:ProgramFiles\Git\bin\bash.exe"
if (-not $vs) { "SKIP windows: no Visual Studio with the C++ x64 tools"; exit 2 }
if (-not (Test-Path $bash)) { "SKIP windows: no Git Bash at $bash"; exit 2 }
cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2] }
}
Set-Location $dir
& $bash (Join-Path $HOME $Wrapper) (Join-Path $HOME $Runner)
exit $LASTEXITCODE
EOF

if ! scp -q /tmp/srs-windows-host-run/$PS1 "$HOST:$REMOTE_TEMP/$PS1" ||
   ! scp -q /tmp/srs-windows-host-run/$WRAPPER "$HOST:$REMOTE_TEMP/$WRAPPER" ||
   ! scp -q "$SCRIPT" "$HOST:$REMOTE_TEMP/$RUNNER"; then
  echo "SETUP_FAIL: cannot copy the scripts to $HOST"
  exit 3
fi
remote() {
  ssh -o BatchMode=yes "$HOST" "powershell -NoProfile -ExecutionPolicy Bypass -File $REMOTE_TEMP/$PS1 -Step $1 -Rel $HOST_REL -MainRel $MAIN_REL -Branch $BRANCH -Remote $REMOTE -RemoteBranch $REMOTE_BRANCH -Url $URL -Wip $WIP -Sha $SHA -Wrapper $REMOTE_TEMP/$WRAPPER -Runner $REMOTE_TEMP/$RUNNER; exit \$LASTEXITCODE" | tr -d '\r'
  return ${PIPESTATUS[0]}
}

remote setup | tee /tmp/srs-windows-host-run/windows-host-run-$$.out
if ! grep -q "^SETUP: .* at $SHA\$" /tmp/srs-windows-host-run/windows-host-run-$$.out; then
  grep -q '^SETUP_FAIL' /tmp/srs-windows-host-run/windows-host-run-$$.out || echo "SETUP_FAIL: setup on $HOST did not finish"
  exit 3
fi

remote run
exit $?
