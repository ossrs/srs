#!/bin/bash
# Stop the local development stack by its server sockets, even without PID
# state. These ports are reserved for this stack. Never stop Redis.
# TCP clients connected to these ports are NOT selected.
STATE_FILE="${ORYX_STACK_STATE_FILE:-/tmp/oryx-stack-state.env}"

if ! command -v lsof >/dev/null; then
  echo "FAIL: lsof is required to discover and verify running services." >&2
  exit 1
fi

stack_pids() {
  local port pids status
  for port in 3000 2022 2024 2443 1935 1985 8080; do
    pids=$(lsof -nP -a -t -iTCP:"$port" -sTCP:LISTEN)
    status=$?
    # lsof returns 1 when there are no matching sockets.
    if [[ "$status" -gt 1 ]]; then return "$status"; fi
    printf '%s\n' "$pids"
  done
  for port in 8000 10080; do
    pids=$(lsof -nP -a -t -iUDP:"$port")
    status=$?
    if [[ "$status" -gt 1 ]]; then return "$status"; fi
    printf '%s\n' "$pids"
  done
}

read_pids() {
  local found
  found=$(stack_pids) || return 1
  PIDS=$(printf '%s\n' "$found" | awk '/^[0-9]+$/ && $1 > 1' | sort -un)
}

signal_stack() {
  local pid
  for pid in $PIDS; do
    echo "Stopping stack listener $pid ($1)..."
    kill "-$1" "$pid" 2>/dev/null || true
  done
}

wait_stopped() {
  local attempt
  for attempt in 1 2 3 4 5; do
    read_pids || return 1
    [[ -z "$PIDS" ]] && return 0
    sleep 1
  done
  read_pids || return 1
  [[ -z "$PIDS" ]]
}

echo "=== Oryx Stack Stop ==="
read_pids || exit 1
signal_stack TERM
if ! wait_stopped; then
  read_pids || exit 1
  signal_stack KILL
  if ! wait_stopped; then
    echo "FAIL: stack ports are still occupied; check permissions or a service supervisor. Redis was not touched." >&2
    exit 1
  fi
fi

# Do not signal stale recorded PIDs: they may now belong to unrelated processes.
rm -f "$STATE_FILE" || exit 1
echo "Cleanup done. All local stack ports are clear. Redis is left running."
