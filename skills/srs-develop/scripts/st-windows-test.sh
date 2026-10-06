#!/bin/bash
# Run the State Threads tests on a remote Windows host over SSH, from macOS or Linux: native Windows
# x64 with MSVC, in a checkout on the host that mirrors the local one. Linux is tested in Docker, not in
# WSL there.
#
#   st-windows-test.sh [ST_DIR]     ST_DIR is the local ST checkout, state-threads/ by default
#
# Opt in with SRS_TEST_WINDOWS_HOST, the SSH host of the Windows machine, such as win in ~/.zshrc.
#
# Nothing about the setup is hardcoded; it mirrors the local checkout:
#   - The local checkout is $HOME/<path>, and its main repository, for a linked worktree, $HOME/<main>.
#   - Its branch tracks <remote>/<branch>; a branch without an upstream cannot be tested.
#   - The commit must be clean and on that upstream. If it is not there yet, it is pushed to the
#     upstream, except when the upstream is origin, which is left to the user.
#
# Setup on the host, which aborts the test when it fails:
#   - The main repository must exist at the same <main> path under the host's home; clone it first.
#   - The remote is added with the same name and URL when missing, and must match when present.
#   - A missing worktree is added at the same <path> on the same branch, tracking the same upstream.
#   - The checkout must be on the branch and clean; it is fast-forwarded to the commit, never reset.
#
# The host needs OpenSSH with key login, Git for Windows, GNU make on PATH, Visual Studio or its Build
# Tools with the C++ x64 tools.
#
# Run alone, it holds the st product lock of full-tier-lock.sh, as st-test.sh does for the whole run;
# st-test.sh sets SRS_ST_TEST_LOCKED=1 when it starts this script under its own lock.
#
# Prints SETUP lines, then the RESULT lines of st-test-runner.sh per OS and run.
# Exit 0 when every run passed, 1 when one failed, 2 when the test was skipped because the host is
# not set up for it or the checkout has uncommitted changes, and 3 when setup or sync failed.

ST_DIR=${1:-state-threads/}
HOST=$SRS_TEST_WINDOWS_HOST
SCRIPTS=$(cd "$(dirname "$0")" && pwd)

if [[ -z $HOST ]]; then
  echo "SKIP windows: SRS_TEST_WINDOWS_HOST is not set"
  exit 2
fi

# The local layout to mirror.
TOP=$(git -C "$ST_DIR" rev-parse --show-toplevel 2>/dev/null)
if [[ -z $TOP ]]; then
  echo "SETUP_FAIL: $ST_DIR is not a git checkout"
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
UPSTREAM=$(git -C "$TOP" rev-parse --abbrev-ref -q '@{u}' 2>/dev/null)
if [[ -z $BRANCH || -z $UPSTREAM ]]; then
  echo "SETUP_FAIL: $TOP must be on a branch that tracks a remote branch"
  exit 3
fi
REMOTE=${UPSTREAM%%/*}
REMOTE_BRANCH=${UPSTREAM#*/}
URL=$(git -C "$TOP" remote get-url "$REMOTE")
SHA=$(git -C "$TOP" rev-parse HEAD)
if [[ -n $(git -C "$TOP" status --porcelain) ]]; then
  echo "SKIP windows: $TOP has uncommitted changes; the host tests commits only"
  exit 2
fi

if ! ssh -o BatchMode=yes -o ConnectTimeout=5 "$HOST" exit >/dev/null 2>&1; then
  echo "SKIP windows: cannot ssh to $HOST"
  exit 2
fi

if [[ $SRS_ST_TEST_LOCKED != 1 ]]; then
  echo "lock: waiting, $(bash "$SCRIPTS/full-tier-lock.sh" --product st status)"
  LOCK_START=$(date +%s)
  if ! TOKEN=$(bash "$SCRIPTS/full-tier-lock.sh" --product st acquire --wait 600 --pid $$); then
    echo "SKIP windows: the st lock was not acquired in 600s"
    exit 2
  fi
  echo "lock: acquired after $(( $(date +%s) - LOCK_START ))s"
  # The cleanup below replaces this trap and releases the lock too.
  trap 'bash "$SCRIPTS/full-tier-lock.sh" --product st release "$TOKEN"' EXIT
fi

# Push the commit to the upstream, so the host can fetch it.
if ! git -C "$TOP" merge-base --is-ancestor HEAD "$UPSTREAM"; then
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

# The files copied to the host's temp folder, named by this process so parallel runs never share them.
mkdir -p /tmp/srs-st-windows-test
PS1=st-windows-test-$$.ps1
RUNNER=st-windows-test-$$.sh
REMOTE_TEMP=AppData/Local/Temp
cleanup() {
  ssh -o BatchMode=yes "$HOST" "powershell -NoProfile -Command Remove-Item -ErrorAction SilentlyContinue $REMOTE_TEMP/$PS1,$REMOTE_TEMP/$RUNNER" >/dev/null 2>&1
  rm -f /tmp/srs-st-windows-test/st-windows-test-$$.ps1 /tmp/srs-st-windows-test/st-windows-test-$$.out
  if [[ -n $TOKEN ]]; then
    bash "$SCRIPTS/full-tier-lock.sh" --product st release "$TOKEN"
  fi
}
trap cleanup EXIT

# The PowerShell side: -Step setup mirrors the checkout and syncs it to the commit, and -Step test
# loads MSVC and starts the runner.
cat > /tmp/srs-st-windows-test/$PS1 <<'EOF'
param([string]$Step, [string]$Rel, [string]$MainRel, [string]$Branch, [string]$Remote,
      [string]$RemoteBranch, [string]$Url, [string]$Sha, [string]$Runner)
# The paths are relative to the home folder, where SSH starts.
$dir = Join-Path $HOME $Rel
$main = Join-Path $HOME $MainRel
$runner = Join-Path $HOME $Runner

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

$unixDir = $dir -replace '\\', '/'
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = $null
if (Test-Path $vswhere) {
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
$bash = "$env:ProgramFiles\Git\bin\bash.exe"
if (-not $vs) {
    "SKIP windows-x64: no Visual Studio with the C++ x64 tools"
} elseif (-not (Test-Path $bash)) {
    "SKIP windows-x64: no Git Bash at $bash"
} else {
    cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2] }
    }
    & $bash $runner windows-x64 $unixDir
}
EOF

if ! scp -q /tmp/srs-st-windows-test/$PS1 "$HOST:$REMOTE_TEMP/$PS1" || ! scp -q "$SCRIPTS/st-test-runner.sh" "$HOST:$REMOTE_TEMP/$RUNNER"; then
  echo "SETUP_FAIL: cannot copy the scripts to $HOST"
  exit 3
fi
remote() {
  ssh -o BatchMode=yes "$HOST" "powershell -NoProfile -ExecutionPolicy Bypass -File $REMOTE_TEMP/$PS1 -Step $1 -Rel $REL -MainRel $MAIN_REL -Branch $BRANCH -Remote $REMOTE -RemoteBranch $REMOTE_BRANCH -Url $URL -Sha $SHA -Runner $REMOTE_TEMP/$RUNNER" | tr -d '\r'
}

remote setup | tee /tmp/srs-st-windows-test/st-windows-test-$$.out
if ! grep -q '^SETUP: .* at [0-9a-f]*$' /tmp/srs-st-windows-test/st-windows-test-$$.out; then
  grep -q '^SETUP_FAIL' /tmp/srs-st-windows-test/st-windows-test-$$.out || echo "SETUP_FAIL: setup on $HOST did not finish"
  exit 3
fi

remote test | tee /tmp/srs-st-windows-test/st-windows-test-$$.out
if grep -q '^RESULT .* FAIL ' /tmp/srs-st-windows-test/st-windows-test-$$.out; then exit 1; fi
if ! grep -q '^RESULT ' /tmp/srs-st-windows-test/st-windows-test-$$.out; then exit 2; fi
exit 0
