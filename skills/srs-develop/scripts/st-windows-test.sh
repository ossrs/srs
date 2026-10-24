#!/bin/bash
# Run the State Threads tests on a remote Windows host over SSH, from macOS or Linux: native Windows
# x64 with MSVC, in a checkout on the host that mirrors the local one. Linux is tested in Docker, not in
# WSL there.
#
#   st-windows-test.sh [ST_DIR]     ST_DIR is the local ST checkout, state-threads/ by default
#
# Opt in with SRS_TEST_WINDOWS_HOST, the SSH host of the Windows machine, set in your shell profile
# such as ~/.zshrc:
#   export SRS_TEST_WINDOWS_HOST=<srs-test-windows-host>
#
# windows-host-run.sh mirrors the checkout on the host and runs st-test-runner.sh there; its header
# lists the setup. The commit must be clean and on the branch's upstream; when it is not there yet,
# it is pushed to the upstream, except when the upstream is origin, which is left to the user.
#
# Run alone, it holds the st product lock of full-tier-lock.sh, as st-test.sh does for the whole run;
# st-test.sh sets SRS_ST_TEST_LOCKED=1 when it starts this script under its own lock.
#
# Prints SETUP lines, then the RESULT lines of st-test-runner.sh per OS and run.
# Exit 0 when every run passed, 1 when one failed, 2 when the test was skipped because the host is
# not set up for it or the checkout has uncommitted changes, and 3 when setup or sync failed.

ST_DIR=${1:-state-threads/}
SCRIPTS=$(cd "$(dirname "$0")" && pwd)

if [[ -z $SRS_TEST_WINDOWS_HOST ]]; then
  echo "SKIP windows: SRS_TEST_WINDOWS_HOST is not set"
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
  trap 'bash "$SCRIPTS/full-tier-lock.sh" --product st release "$TOKEN"' EXIT
fi

# The runner tests the host checkout, its working directory there.
mkdir -p /tmp/srs-st-windows-test
bash "$SCRIPTS/windows-host-run.sh" "$ST_DIR" st-test-runner.sh windows-x64 . |
  tee /tmp/srs-st-windows-test/st-windows-test-$$.out
CODE=${PIPESTATUS[0]}
if grep -q '^RESULT .* FAIL ' /tmp/srs-st-windows-test/st-windows-test-$$.out; then CODE=1
elif grep -q '^RESULT ' /tmp/srs-st-windows-test/st-windows-test-$$.out; then CODE=0
elif [[ $CODE != 3 ]]; then CODE=2
fi
rm -f /tmp/srs-st-windows-test/st-windows-test-$$.out
exit $CODE
