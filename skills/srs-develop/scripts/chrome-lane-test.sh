#!/bin/bash
# Lane I of the Full tier: the Chrome driver's unit tests and the helper's
# self-test, then the WebRTC, loss, HTTP and codec cases in parallel, each on
# its own 100-port slot from SRS_TEST_PORT_BASE (default 15600, then 15700,
# 15800 and 15900), within the 60 s lane budget.
#
#   chrome-lane-test.sh [--cases-only]
#
# --cases-only runs the four cases without the unit suites, as
# chrome-linux-test.sh does against SRS in Docker.
set -eu
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
case "${1:-}" in
  '') units=1;;
  --cases-only) units=;;
  *) echo "Usage: $0 [--cases-only]" >&2; exit 3;;
esac
[[ "${SRS_TEST_CHROME:-1}" != 0 ]] || { echo 'SKIP SRS_TEST_CHROME=0'; exit 0; }
source "$SCRIPT_DIR/chrome-test-lib.sh"
base=${SRS_TEST_PORT_BASE:-15600}
case "$base" in ''|*[!0-9]*) echo 'FAIL invalid port base' >&2; exit 1;; esac
LANE_DIR=${CHROME_TEST_ROOT:-/tmp}/srs-chrome-lane
SKILL_DIR=${CHROME_SKILL_DIR:-$(cd "$SCRIPT_DIR/../.." && pwd)/internal-chrome-control}
CASES='webrtc loss http codec'
mkdir -p "$LANE_DIR"
started=$SECONDS
passed=0 skipped=0 known=0 failed=0

# The driver's suite needs the skill's packages; installing them here also
# keeps the four cases from waiting on each other's npm ci.
unit_driver() {
  local version
  CHROME_CASE=driver CHROME_STARTED=$SECONDS CHROME_TEST_DIR=$LANE_DIR
  command -v "${CHROME_NODE:-node}" >/dev/null || chrome_test_unavailable 'no node'
  version=$("${CHROME_NODE:-node}" --version 2>/dev/null) || version=
  chrome_test_node_ok "$version" || chrome_test_unavailable "node ${version:-unknown}, needs 22.12"
  chrome_test_npm_ci "$SKILL_DIR" || return 1
  cd "$SKILL_DIR" && "${CHROME_NODE:-node}" --test 'scripts/*.test.js'
}
# Forward a stop to the children, whose own traps stop their SRS and browser.
pids=
lane_stop() { kill -TERM $pids 2>/dev/null || true; wait; exit 143; }
trap lane_stop INT TERM

run_units() {
  ( unit_driver ) >"$LANE_DIR/driver.log" 2>&1 &
  driver_pid=$!
  bash "$SCRIPT_DIR/chrome-test-lib-test.sh" >"$LANE_DIR/lib.log" 2>&1 &
  lib_pid=$!
  pids="$driver_pid $lib_pid"

  # The unit suites print one line each; their logs stay in the lane folder and
  # are shown, indented, only on failure.
  status=0
  wait "$driver_pid" || status=$?
  tests=$(sed -n 's/^# pass \([0-9]*\)$/\1/p' "$LANE_DIR/driver.log" | tail -n 1)
  bad=$(sed -n 's/^# fail \([0-9]*\)$/\1/p' "$LANE_DIR/driver.log" | tail -n 1)
  if [[ $status -ne 0 ]]; then
    failed=$((failed + 1))
    sed 's/^/  /' "$LANE_DIR/driver.log"
    echo "FAIL unit driver: exit $status, ${bad:-?} of $((${tests:-0} + ${bad:-0})) tests failed (log: $LANE_DIR/driver.log)"
  elif tail -n 1 "$LANE_DIR/driver.log" | grep -q '^SKIP '; then
    skipped=$((skipped + 1))
    tail -n 1 "$LANE_DIR/driver.log"
  else
    passed=$((passed + 1))
    echo "PASS unit driver: ${tests:-0} tests"
  fi
  status=0
  wait "$lib_pid" || status=$?
  if [[ $status -ne 0 ]]; then
    failed=$((failed + 1))
    sed 's/^/  /' "$LANE_DIR/lib.log"
    echo "FAIL unit chrome-test-lib-test.sh: exit $status (log: $LANE_DIR/lib.log)"
  else
    passed=$((passed + 1))
    echo "PASS unit chrome-test-lib-test.sh: $(sed -n 's/^PASS \([0-9]*\) harness regressions$/\1/p' "$LANE_DIR/lib.log") checks"
  fi
}
[[ -z "$units" ]] || run_units

pids=
slot=$base
for name in $CASES; do
  SRS_TEST_PORT_BASE=$slot bash "$SCRIPT_DIR/chrome-$name-test.sh" >"$LANE_DIR/$name.log" 2>&1 &
  pids="$pids $!"
  slot=$((slot + 100))
done
set -- $pids
for name in $CASES; do
  status=0
  wait "$1" || status=$?
  shift
  cat "$LANE_DIR/$name.log"
  last=$(tail -n 1 "$LANE_DIR/$name.log")
  case_known=$(sed -n 's/^RESULT .* known=\([0-9]*\) .*/\1/p' "$LANE_DIR/$name.log" | tail -n 1)
  known=$((known + ${case_known:-0}))
  if [[ $status -ne 0 ]]; then
    failed=$((failed + 1)); echo "LANE $name failed exit=$status"
  elif [[ "$last" == SKIP* ]]; then
    skipped=$((skipped + 1)); echo "LANE $name skipped"
  else
    passed=$((passed + 1)); echo "LANE $name passed"
  fi
done
elapsed=$((SECONDS - started))
[[ $elapsed -le 60 ]] || { failed=$((failed + 1)); echo "FAIL budget ${elapsed}s > 60s" >&2; }
echo "LANE chrome passed=$passed skipped=$skipped known=$known failed=$failed elapsed=${elapsed}s"
[[ $failed -eq 0 ]]
