#!/bin/bash
# Shared teardown for local Oryx verification: stops only what
# oryx-stack-start.sh actually started, per $STATE_FILE. Redis is never
# stopped (shared system service). Safe to call even if nothing was started
# or the state file is missing -- it's then a no-op.

STATE_FILE="${ORYX_STACK_STATE_FILE:-/tmp/oryx-stack-state.env}"

if [[ ! -f "$STATE_FILE" ]]; then
  echo "No $STATE_FILE found, nothing to stop."
  exit 0
fi

SRS_STARTED_PID=""
BACKEND_STARTED_PID=""
UI_STARTED_PID=""
# shellcheck disable=SC1090
source "$STATE_FILE"

echo "=== Oryx Stack Stop ==="

if [[ -n "$UI_STARTED_PID" ]]; then
  echo "Stopping React dashboard (pid $UI_STARTED_PID)..."
  kill "$UI_STARTED_PID" 2>/dev/null || true
fi
if [[ -n "$BACKEND_STARTED_PID" ]]; then
  echo "Stopping Oryx backend (pid $BACKEND_STARTED_PID)..."
  kill "$BACKEND_STARTED_PID" 2>/dev/null || true
fi
if [[ -n "$SRS_STARTED_PID" ]]; then
  echo "Stopping local SRS (pid $SRS_STARTED_PID)..."
  kill "$SRS_STARTED_PID" 2>/dev/null || true
fi
sleep 1
if [[ -n "$UI_STARTED_PID" ]]; then
  kill -9 "$UI_STARTED_PID" 2>/dev/null || true
  # "npm start" runs react-scripts as a child process; the wrapper PID above
  # may not own it, so also reap anything still bound to the dev-server port.
  lsof -ti :3000 2>/dev/null | xargs kill -9 2>/dev/null || true
fi
if [[ -n "$BACKEND_STARTED_PID" ]]; then
  kill -9 "$BACKEND_STARTED_PID" 2>/dev/null || true
  # "go run" builds and execs a child process; the wrapper PID above may not
  # own it, so also reap anything still bound to the backend ports.
  for port in 2022 2024 2443; do
    lsof -ti :"$port" 2>/dev/null | xargs kill -9 2>/dev/null || true
  done
fi
if [[ -n "$SRS_STARTED_PID" ]]; then
  kill -9 "$SRS_STARTED_PID" 2>/dev/null || true
  for port in 1935 1985 8080 8000 10080; do
    lsof -ti :"$port" 2>/dev/null | xargs kill -9 2>/dev/null || true
  done
fi

if [[ -z "$SRS_STARTED_PID" && -z "$BACKEND_STARTED_PID" && -z "$UI_STARTED_PID" ]]; then
  echo "Nothing recorded as started by oryx-stack-start.sh; leaving the stack alone."
fi

rm -f "$STATE_FILE"
echo "Cleanup done. Redis is left running (shared service)."
