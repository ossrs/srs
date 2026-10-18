#!/bin/bash
# Test windows-host-run.sh, and st-windows-test.sh on it, without a Windows host: a fake ssh and scp
# on PATH stand in for the host, and a local bare repository stands in for the branch's upstream
# remote.
#
#   windows-host-run-test.sh
#
# Covers the argument checks, the refusal of a dirty tree without --wip, the --wip snapshot (branch,
# index and files unchanged, new files included, pushed to <upstream>-wip), the push of a clean
# commit without --wip, the refusal to push to origin, the arguments given to the script on the
# host, its exit code, and the host lock. For st-windows-test.sh: the runner and its arguments on the
# host, the exit code for each result, the skips and setup failures, and the st lock.
#
# Prints PASS or FAIL per check, then a summary. Exit 0 when every check passed, 1 otherwise.

SCRIPTS=$(cd "$(dirname "$0")" && pwd)
RUN="$SCRIPTS/windows-host-run.sh"
PASSED=0
FAILED=0

pass() { echo "PASS $1"; PASSED=$((PASSED + 1)); }
fail() { echo "FAIL $1"; sed 's/^/  | /' /tmp/srs-windows-host-run-test/out; FAILED=$((FAILED + 1)); }
check() { if eval "$2"; then pass "$1"; else fail "$1"; fi; }

rm -rf /tmp/srs-windows-host-run-test
mkdir -p /tmp/srs-windows-host-run-test/home /tmp/srs-windows-host-run-test/bin \
  /tmp/srs-windows-host-run-test/scp /tmp/srs-windows-host-run-test/lock
ROOT=$(cd /tmp/srs-windows-host-run-test && pwd -P)
export HOME=$ROOT/home
export GIT_CONFIG_NOSYSTEM=1
export GIT_AUTHOR_NAME=test GIT_AUTHOR_EMAIL=test@example.com
export GIT_COMMITTER_NAME=test GIT_COMMITTER_EMAIL=test@example.com
export SRS_FULL_TIER_LOCK_DIR=$ROOT/lock/srs.lock
HOST_LOCK=$ROOT/lock/srs-windows-host.lock

# The fake host: ssh logs each command, answers the reachability check, prints the setup line with
# the commit it was asked for, or fails it with STUB_SETUP_FAIL, and for the run step reports whether
# the host and st locks are held, prints STUB_RUN_OUTPUT and exits with STUB_RUN_EXIT. scp keeps a
# copy of each file it is given.
cat > "$ROOT/bin/ssh" <<'EOF'
#!/bin/bash
echo "$*" >> /tmp/srs-windows-host-run-test/ssh.log
cmd=${!#}
[[ $cmd == exit ]] && exit ${STUB_SSH_EXIT:-0}
sha=$(echo "$cmd" | sed -n 's/.* -Sha \([0-9a-f]*\).*/\1/p')
case $cmd in
  *"-Step setup"*)
    if [[ -n $STUB_SETUP_FAIL ]]; then
      echo "SETUP_FAIL: stub setup failed"
      exit 3
    fi
    echo "SETUP: C:\\fake at $sha" ;;
  *"-Step run"*)
    if [[ -d ${SRS_FULL_TIER_LOCK_DIR%.lock}-windows-host.lock ]]; then
      echo "STUB lock held"
    fi
    if [[ -d ${SRS_FULL_TIER_LOCK_DIR%.lock}-st.lock ]]; then
      echo "STUB st lock held"
    fi
    if [[ -n $STUB_RUN_OUTPUT ]]; then
      printf '%b\n' "$STUB_RUN_OUTPUT"
    fi
    echo "STUB run $sha"
    exit ${STUB_RUN_EXIT:-0} ;;
esac
exit 0
EOF
cat > "$ROOT/bin/scp" <<'EOF'
#!/bin/bash
src=
for a in "$@"; do [[ $a == -* ]] || { src=$a; break; }; done
cp "$src" /tmp/srs-windows-host-run-test/scp/
EOF
chmod +x "$ROOT/bin/ssh" "$ROOT/bin/scp"
export PATH=$ROOT/bin:$PATH
export SRS_TEST_WINDOWS_HOST=fake-host

# The checkout: branch work tracking fork/work in a bare repository, plus an origin remote.
git init -q --bare "$ROOT/fork.git"
git init -q --bare "$ROOT/origin.git"
git init -q "$HOME/repo"
REPO=$HOME/repo
git -C "$REPO" checkout -q -b work
echo one > "$REPO/a.txt"
echo keep > "$REPO/b.txt"
git -C "$REPO" add a.txt b.txt
git -C "$REPO" commit -q -m first
git -C "$REPO" remote add fork "$ROOT/fork.git"
git -C "$REPO" remote add origin "$ROOT/origin.git"
git -C "$REPO" push -q -u fork work
BASE=$(git -C "$REPO" rev-parse HEAD)

# A script for the host, and one that prints its arguments, to run the generated wrapper locally.
echo 'echo hello' > "$ROOT/hello.sh"
printf '%s\n' 'for a in "$@"; do echo "ARG[$a]"; done' > "$ROOT/args.sh"

out() { cat /tmp/srs-windows-host-run-test/out; }
run() {
  : > /tmp/srs-windows-host-run-test/ssh.log
  bash "$RUN" "$@" > /tmp/srs-windows-host-run-test/out 2>&1
  CODE=$?
}

# Arguments.
run
check "no arguments: exit 3" '[[ $CODE == 3 ]]'
check "no arguments: usage" 'out | grep -q "^Usage: "'
run --wip
check "--wip alone: exit 3 with usage" '[[ $CODE == 3 ]] && out | grep -q "^Usage: "'
run "$REPO"
check "no script: exit 3 with usage" '[[ $CODE == 3 ]] && out | grep -q "^Usage: "'
run --bad "$REPO" "$ROOT/hello.sh"
check "unknown option: exit 3 with usage" '[[ $CODE == 3 ]] && out | grep -q "^Usage: "'
run "$REPO" "$ROOT/missing.sh"
check "missing script: exit 3" '[[ $CODE == 3 ]] && out | grep -q "^SETUP_FAIL: .*missing.sh"'
run "$ROOT/scp" "$ROOT/hello.sh"
check "not a checkout: exit 3" '[[ $CODE == 3 ]] && out | grep -q "^SETUP_FAIL: .* is not a git checkout"'
SRS_TEST_WINDOWS_HOST= run "$REPO" "$ROOT/hello.sh"
check "no host: skip, exit 2" '[[ $CODE == 2 ]] && out | grep -q "^SKIP windows: SRS_TEST_WINDOWS_HOST is not set"'
STUB_SSH_EXIT=255 run "$REPO" "$ROOT/hello.sh"
check "host unreachable: skip, exit 2" '[[ $CODE == 2 ]] && out | grep -q "^SKIP windows: cannot ssh to fake-host"'

# A dirty tree without --wip is refused before anything reaches the host.
echo two > "$REPO/a.txt"
run "$REPO" "$ROOT/hello.sh"
check "dirty tree: skip, exit 2" '[[ $CODE == 2 ]] && out | grep -q "^SKIP windows: .* has uncommitted changes"'
check "dirty tree: no run on the host" '! grep -q -- "-Step" /tmp/srs-windows-host-run-test/ssh.log'
check "dirty tree: nothing pushed" '! git -C "$ROOT/fork.git" rev-parse -q --verify refs/heads/work-wip >/dev/null'

# --wip snapshots a modified file, a staged change, a new file and a deleted file.
echo staged > "$REPO/c.txt"
git -C "$REPO" add c.txt
echo new > "$REPO/new.txt"
rm "$REPO/b.txt"
STATUS_BEFORE=$(git -C "$REPO" status --porcelain)
CACHED_BEFORE=$(git -C "$REPO" diff --cached)
STUB_RUN_EXIT=7 run --wip "$REPO" "$ROOT/hello.sh"
SNAP=$(git -C "$ROOT/fork.git" rev-parse -q --verify refs/heads/work-wip)
check "--wip: snapshot pushed to work-wip" '[[ -n $SNAP ]]'
check "--wip: snapshot parent is HEAD" '[[ $(git -C "$ROOT/fork.git" rev-parse "$SNAP^") == $BASE ]]'
check "--wip: modified file in snapshot" '[[ $(git -C "$ROOT/fork.git" show "$SNAP:a.txt") == two ]]'
check "--wip: staged file in snapshot" '[[ $(git -C "$ROOT/fork.git" show "$SNAP:c.txt") == staged ]]'
check "--wip: new file in snapshot" '[[ $(git -C "$ROOT/fork.git" show "$SNAP:new.txt") == new ]]'
check "--wip: deleted file not in snapshot" '! git -C "$ROOT/fork.git" cat-file -e "$SNAP:b.txt" 2>/dev/null'
check "--wip: branch unchanged" '[[ $(git -C "$REPO" rev-parse HEAD) == $BASE && $(git -C "$REPO" symbolic-ref --short HEAD) == work ]]'
check "--wip: upstream unchanged" '[[ $(git -C "$ROOT/fork.git" rev-parse refs/heads/work) == $BASE ]]'
check "--wip: status unchanged" '[[ $(git -C "$REPO" status --porcelain) == "$STATUS_BEFORE" ]]'
check "--wip: index unchanged" '[[ $(git -C "$REPO" diff --cached) == "$CACHED_BEFORE" ]]'
check "--wip: host runs the snapshot in the scratch worktree" 'grep -- "-Step setup" /tmp/srs-windows-host-run-test/ssh.log | grep -q -- "-Rel repo-wip .*-Wip 1 .*-Sha $SNAP"'
check "--wip: script output returned" 'out | grep -q "^STUB run $SNAP$"'
check "--wip: script exit code returned" '[[ $CODE == 7 ]]'
check "--wip: the host shell passes the exit code on" 'grep -- "-Step run" /tmp/srs-windows-host-run-test/ssh.log | grep -q "; exit \$LASTEXITCODE$"'
check "--wip: host lock held during the run" 'out | grep -q "^STUB lock held$"'
check "--wip: host lock released" '[[ ! -d $HOST_LOCK ]]'

# The wrapper copied to the host passes the arguments unchanged, spaces and quotes included.
rm -f /tmp/srs-windows-host-run-test/scp/*
run --wip "$REPO" "$ROOT/args.sh" "a b" "it's" '$HOME'
WRAPPER=$(ls /tmp/srs-windows-host-run-test/scp/*wrapper* 2>/dev/null | head -n 1)
check "wrapper copied to the host" '[[ -n $WRAPPER ]]'
bash "$WRAPPER" "$ROOT/args.sh" > /tmp/srs-windows-host-run-test/out 2>&1
check "wrapper passes the arguments" '[[ $(out) == "ARG[a b]
ARG[it'"'"'s]
ARG[\$HOME]" ]]'
check "script copied to the host" 'ls /tmp/srs-windows-host-run-test/scp/ | grep -q "args.sh$"'

# Without --wip, a clean commit that is not on the upstream yet is pushed there.
git -C "$REPO" add -A
git -C "$REPO" commit -q -m second
HEAD2=$(git -C "$REPO" rev-parse HEAD)
run "$REPO" "$ROOT/hello.sh"
check "clean: exit 0" '[[ $CODE == 0 ]]'
check "clean: commit pushed to the upstream" '[[ $(git -C "$ROOT/fork.git" rev-parse refs/heads/work) == $HEAD2 ]]'
check "clean: host runs the commit in the mirror" 'grep -- "-Step setup" /tmp/srs-windows-host-run-test/ssh.log | grep -q -- "-Rel repo .*-Wip 0 .*-Sha $HEAD2"'
check "clean: script output returned" 'out | grep -q "^STUB run $HEAD2$"'

# st-windows-test.sh runs st-test-runner.sh in the host checkout through windows-host-run.sh, with
# its options, output and exit codes unchanged.
ST_LOCK=$ROOT/lock/srs-st.lock
st() {
  : > /tmp/srs-windows-host-run-test/ssh.log
  rm -f /tmp/srs-windows-host-run-test/scp/*
  bash "$SCRIPTS/st-windows-test.sh" "$@" > /tmp/srs-windows-host-run-test/out 2>&1
  CODE=$?
}
PASS_LINE='RESULT windows-x64 utest-default PASS passed 1, failed 0, skipped 0 (1s)'
FAIL_LINE='RESULT windows-x64 tools-default FAIL 0 OK, 0 skipped (1s)'
STUB_RUN_OUTPUT=$PASS_LINE st "$REPO"
check "st: every run passed: exit 0" '[[ $CODE == 0 ]]'
check "st: RESULT lines printed" 'out | grep -qF "$PASS_LINE"'
check "st: host runs the commit in the mirror" 'grep -- "-Step setup" /tmp/srs-windows-host-run-test/ssh.log | grep -q -- "-Rel repo .*-Wip 0 .*-Sha $HEAD2"'
check "st: st-test-runner.sh copied to the host" 'ls /tmp/srs-windows-host-run-test/scp/ | grep -q "st-test-runner.sh$"'
WRAPPER=$(ls /tmp/srs-windows-host-run-test/scp/*wrapper* 2>/dev/null | head -n 1)
bash "$WRAPPER" "$ROOT/args.sh" > /tmp/srs-windows-host-run-test/out 2>&1
check "st: runner gets windows-x64 and the host checkout" '[[ $(out) == "ARG[windows-x64]
ARG[.]" ]]'
STUB_RUN_OUTPUT=$PASS_LINE st "$REPO"
check "st: st and host locks held during the run" 'out | grep -q "^STUB st lock held$" && out | grep -q "^STUB lock held$"'
check "st: locks released" '[[ ! -d $ST_LOCK && ! -d $HOST_LOCK ]]'
SRS_ST_TEST_LOCKED=1 STUB_RUN_OUTPUT=$PASS_LINE st "$REPO"
check "st: under st-test.sh's lock: no st lock of its own" '[[ $CODE == 0 ]] && ! out | grep -q "^STUB st lock held$"'
STUB_RUN_OUTPUT="$PASS_LINE\n$FAIL_LINE" STUB_RUN_EXIT=1 st "$REPO"
check "st: a failed run: exit 1" '[[ $CODE == 1 ]] && out | grep -qF "$FAIL_LINE"'
st "$REPO"
check "st: no RESULT line: exit 2" '[[ $CODE == 2 ]]'
STUB_SETUP_FAIL=1 STUB_RUN_OUTPUT=$PASS_LINE st "$REPO"
check "st: setup failed: exit 3" '[[ $CODE == 3 ]] && out | grep -q "^SETUP_FAIL: stub setup failed"'
SRS_TEST_WINDOWS_HOST= st "$REPO"
check "st: no host: skip, exit 2" '[[ $CODE == 2 ]] && out | grep -q "^SKIP windows: SRS_TEST_WINDOWS_HOST is not set"'
echo dirty > "$REPO/dirty.txt"
st "$REPO"
check "st: dirty tree: skip, exit 2" '[[ $CODE == 2 ]] && out | grep -q "^SKIP windows: .* has uncommitted changes"'
check "st: dirty tree: no run on the host" '! grep -q -- "-Step" /tmp/srs-windows-host-run-test/ssh.log'
rm "$REPO/dirty.txt"

# Never push to origin, with or without --wip.
git -C "$REPO" push -q origin work
git -C "$REPO" branch -q -u origin/work
echo three > "$REPO/a.txt"
git -C "$REPO" commit -q -am third
ORIGIN_BEFORE=$(git -C "$ROOT/origin.git" rev-parse refs/heads/work)
run "$REPO" "$ROOT/hello.sh"
check "origin upstream: exit 3" '[[ $CODE == 3 ]] && out | grep -q "^SETUP_FAIL: .* is not on origin/work"'
echo four > "$REPO/a.txt"
run --wip "$REPO" "$ROOT/hello.sh"
check "origin upstream with --wip: exit 3" '[[ $CODE == 3 ]] && out | grep -q "^SETUP_FAIL: .*origin"'
check "origin: nothing pushed" '[[ $(git -C "$ROOT/origin.git" rev-parse refs/heads/work) == $ORIGIN_BEFORE ]] && ! git -C "$ROOT/origin.git" rev-parse -q --verify refs/heads/work-wip >/dev/null'

# A branch without an upstream cannot be synced.
git -C "$REPO" checkout -q -f -b lonely
run --wip "$REPO" "$ROOT/hello.sh"
check "no upstream: exit 3" '[[ $CODE == 3 ]] && out | grep -q "^SETUP_FAIL: .* must be on a branch that tracks a remote branch"'
st "$REPO"
check "st: no upstream: exit 3" '[[ $CODE == 3 ]] && out | grep -q "^SETUP_FAIL: .* must be on a branch that tracks a remote branch"'

check "no leftover temp index" '! ls /tmp/srs-windows-host-run/ 2>/dev/null | grep -q index'

rm -rf /tmp/srs-windows-host-run-test
echo "windows-host-run-test: passed=$PASSED failed=$FAILED"
[[ $FAILED == 0 ]]
