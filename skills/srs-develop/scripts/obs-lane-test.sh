#!/bin/bash
# Run the RTMP, SRT and WHIP OBS cases in parallel, each on its own 100-port
# slot from SRS_TEST_PORT_BASE (default 15100), within the 60 s lane budget.
set -eu
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
[[ "${SRS_TEST_OBS:-1}" != 0 ]] || { echo 'SKIP SRS_TEST_OBS=0'; exit 0; }
base=${SRS_TEST_PORT_BASE:-15100}
case "$base" in ''|*[!0-9]*) echo 'FAIL invalid port base' >&2; exit 1;; esac
LANE_DIR=/tmp/srs-obs-lane
mkdir -p "$LANE_DIR"
started=$SECONDS
pids=
slot=$base
# Start the cases 2 s apart: OBS can hang in obs_reset_video when several
# fresh instances reset video at the same moment.
for name in rtmp srt whip; do
  [[ -z "$pids" ]] || sleep 2
  SRS_TEST_PORT_BASE=$slot bash "$SCRIPT_DIR/obs-$name-publish-test.sh" >"$LANE_DIR/$name.log" 2>&1 &
  pids="$pids $!"
  slot=$((slot + 100))
done
# Forward a stop to the cases, whose own traps remove their instance and SRS.
lane_stop() { kill -TERM $pids 2>/dev/null || true; wait; exit 143; }
trap lane_stop INT TERM
passed=0 skipped=0 known=0 failed=0
set -- $pids
for name in rtmp srt whip; do
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
echo "LANE obs passed=$passed skipped=$skipped known=$known failed=$failed elapsed=${elapsed}s"
[[ $failed -eq 0 ]]
