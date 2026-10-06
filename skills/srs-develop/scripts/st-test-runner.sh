#!/bin/bash
# Run every State Threads utest and tools build of one OS in an ST checkout, and print one line per run:
#   RESULT <os> <run> PASS|FAIL <detail> (<seconds>s)
# with the failed tests and skipped tools before it, or the end of the log of another failure.
#
#   st-test-runner.sh OS ST_DIR
#
# OS names the platform, such as macos-arm64, and picks the build by its prefix: macos-*, linux-*, or
# windows-*, which runs in Git Bash with the MSVC environment on PATH. macOS adds an ASAN utest run.
# The platforms of one checkout share obj/, so every utest run starts with make clean, and the
# checkout is left clean. st-test.sh and st-windows-test.sh run this script on each platform.
#
# Exit 0 when every run passed, and 1 otherwise.

OS=$1
if [[ -z $OS || -z $2 ]]; then
  echo "Usage: $0 OS ST_DIR" >&2
  exit 2
fi
if ! cd "$2"; then
  echo "RESULT $OS all FAIL cannot cd to $2"
  exit 1
fi

case $OS in
  macos-*) UTEST=darwin-debug-utest; BIN=./obj/st_utest ;;
  windows-*) UTEST=win64-debug-utest; BIN=./obj/st_utest.exe ;;
  *) UTEST=linux-debug-utest; BIN=./obj/st_utest ;;
esac
LOG=/tmp/srs-st-test-runner-$OS.log
FAILED=0

# The number of the gtest summary line "[  PASSED  ] 155 tests.", or 0 without one.
count() {
  local n
  n=$(grep -E "^\[  $1 +\] [0-9]+ tests?[.,]" "$LOG" | tail -1 | sed -E 's/^[^]]*\] ([0-9]+).*/\1/')
  echo "${n:-0}"
}

# Run one step and print its RESULT line.
step() {
  local run=$1 start status=PASS detail
  shift
  start=$(date +%s)
  "$@" > "$LOG" 2>&1 || status=FAIL
  # A gtest log names its failed tests; any other failure, such as a build, shows the end of its log.
  if grep -q '^\[==========\]' "$LOG"; then
    detail="passed $(count PASSED), failed $(count FAILED), skipped $(count SKIPPED)"
    grep '^\[  FAILED  \] [A-Za-z_].*\.' "$LOG" | grep -v '(.* ms)' | sed 's/^\[  FAILED  \] /  FAILED /'
  else
    detail="$(grep -c ' OK$' "$LOG") OK, $(grep -c '^SKIP ' "$LOG") skipped"
    grep -E '^SKIP |^FAILED ' "$LOG" | sed 's/^/  /'
    if [[ $status == FAIL ]]; then
      tail -20 "$LOG" | sed 's/^/  | /'
    fi
  fi
  [[ $status == FAIL ]] && FAILED=1
  echo "RESULT $OS $run $status $detail ($(( $(date +%s) - start ))s)"
}

# Build and run the utest with EXTRA_CFLAGS $1, and UTEST_FLAGS $2 when set; an empty UTEST_FLAGS
# would replace the -std the Linux target passes to the utest make.
utest() {
  make clean >/dev/null 2>&1
  if [[ -n $2 ]]; then
    make $UTEST EXTRA_CFLAGS="$1" UTEST_FLAGS="$2" || return 1
  else
    make $UTEST EXTRA_CFLAGS="$1" || return 1
  fi
  $BIN
}

step utest-default utest "" ""
step utest-malloc-stack utest -DMALLOC_STACK ""
if [[ $OS == macos-* ]]; then
  step utest-asan utest "-DMALLOC_STACK -DMD_ASAN -fsanitize=address -fno-omit-frame-pointer" -fsanitize=address
fi
step tools-default ./auto/tools.sh
step tools-malloc-stack env EXTRA_CFLAGS=-DMALLOC_STACK ./auto/tools.sh
make clean >/dev/null 2>&1
rm -f /tmp/srs-st-test-runner-$OS.log
exit $FAILED
