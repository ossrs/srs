#!/bin/bash
# Runs every Oryx local verification script in parallel against one shared
# stack: starts the stack (oryx-stack-start.sh), launches all test scripts
# backgrounded and waits for them, always stops the stack afterward
# (oryx-stack-stop.sh) regardless of pass/fail, then prints a summary.
#
# This exists so "run all the tests" is one command instead of a hand-typed
# background/wait snippet that is easy to paste wrong and end up serialized.
# Add new oryx-*-test.sh scripts to TEST_SCRIPTS below to include them here.

SCRIPT_DIR="$(cd -P "$(dirname "$0")" && pwd)"

TEST_SCRIPTS=(
  oryx-api-smoke-test.sh
  oryx-live-streaming-test.sh
  oryx-live-room-test.sh
  oryx-forward-test.sh
  oryx-record-test.sh
  oryx-transcode-test.sh
  oryx-vlive-test.sh
  oryx-camera-test.sh
)

echo "=== Oryx Test Suite ==="
echo "Scripts: ${TEST_SCRIPTS[*]}"
echo ""

bash "$SCRIPT_DIR/oryx-stack-start.sh"
START_STATUS=$?
if [[ "$START_STATUS" -ne 0 ]]; then
  echo "FAIL: oryx-stack-start.sh failed, aborting before any test runs." >&2
  exit "$START_STATUS"
fi
echo ""

echo "=== Running ${#TEST_SCRIPTS[@]} test scripts in parallel ==="
# Plain indexed arrays only -- the default macOS /bin/bash is 3.2 and has no
# associative arrays (declare -A). LOG_FILES[i] corresponds to
# TEST_SCRIPTS[i] and PIDS[i] by matching index.
LOG_FILES=()
PIDS=()
for name in "${TEST_SCRIPTS[@]}"; do
  log="/tmp/oryx-run-tests-$name.log"
  LOG_FILES+=("$log")
  bash "$SCRIPT_DIR/$name" >"$log" 2>&1 &
  PIDS+=("$!")
  echo "Started $name (pid $!), log: $log"
done
echo ""

FAILED_NAMES=()
FAILED_LOGS=()
for i in "${!TEST_SCRIPTS[@]}"; do
  name="${TEST_SCRIPTS[$i]}"
  pid="${PIDS[$i]}"
  if wait "$pid"; then
    echo "PASS: $name"
  else
    echo "FAIL: $name"
    FAILED_NAMES+=("$name")
    FAILED_LOGS+=("${LOG_FILES[$i]}")
  fi
done
echo ""

# Always tear down the shared stack, whether tests passed or not.
bash "$SCRIPT_DIR/oryx-stack-stop.sh"
echo ""

if [[ "${#FAILED_NAMES[@]}" -gt 0 ]]; then
  echo "=== Failed scripts: ${FAILED_NAMES[*]} ==="
  for i in "${!FAILED_NAMES[@]}"; do
    echo ""
    echo "--- ${FAILED_NAMES[$i]} output (${FAILED_LOGS[$i]}) ---"
    cat "${FAILED_LOGS[$i]}"
  done
  echo ""
  echo "=== Oryx Test Suite FAILED ==="
  exit 1
fi

echo "=== Oryx Test Suite PASSED (${#TEST_SCRIPTS[@]}/${#TEST_SCRIPTS[@]}) ==="
