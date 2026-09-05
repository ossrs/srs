#!/bin/bash
# Clean restart of local SRS, the Oryx backend, and the React dashboard.
# Redis is started only if unavailable and is never stopped. Run lifecycle
# commands sequentially; tests can then share the running stack.

SCRIPT_DIR="$(cd -P "$(dirname "$0")" && pwd)"
# Walk up from SCRIPT_DIR looking for go.mod, the SRS repo root. This avoids
# brittle "../../../.." counting when the skills directory is reached via a
# symlink (which changes the symbolic vs. physical depth).
WORKSPACE="$SCRIPT_DIR"
while [[ "$WORKSPACE" != "/" && ! -f "$WORKSPACE/go.mod" ]]; do
  WORKSPACE="$(dirname "$WORKSPACE")"
done
if [[ ! -f "$WORKSPACE/go.mod" ]]; then
  echo "Error: go.mod not found walking up from: $SCRIPT_DIR" >&2
  exit 1
fi

ORYX_DIR="$WORKSPACE/oryx"
PLATFORM_DIR="$ORYX_DIR/platform"
UI_DIR="$ORYX_DIR/ui"
SRS_BINARY="$WORKSPACE/trunk/objs/srs"
SRS_CONF="containers/conf/srs.release-local.conf"
ENDPOINT="${ORYX_ENDPOINT:-http://localhost:2022}"
SRS_API="${SRS_API_ENDPOINT:-http://localhost:1985}"
UI_ENDPOINT="${ORYX_UI_ENDPOINT:-http://localhost:3000}"
STATE_FILE="${ORYX_STACK_STATE_FILE:-/tmp/oryx-stack-state.env}"

wait_for_http() {
  local url="$1" max="$2" waited=0
  while ! curl -sS -m 2 -o /dev/null "$url" 2>/dev/null; do
    waited=$((waited + 1))
    if [[ "$waited" -ge "$max" ]]; then
      return 1
    fi
    sleep 1
  done
  return 0
}

wait_for_redis() {
  local max="$1" waited=0
  while [[ "$(redis-cli ping 2>/dev/null)" != "PONG" ]]; do
    waited=$((waited + 1))
    if [[ "$waited" -ge "$max" ]]; then
      return 1
    fi
    sleep 1
  done
  return 0
}

echo "=== Oryx Stack Start ==="

# Cleanup always targets the standard local development ports. Do not use
# remote/custom readiness endpoints to accidentally accept a different stack.
if [[ "$ENDPOINT" != "http://localhost:2022" ||
      "$SRS_API" != "http://localhost:1985" ||
      "$UI_ENDPOINT" != "http://localhost:3000" ]]; then
  echo "FAIL: clean startup supports only the default local stack endpoints." >&2
  exit 1
fi

# Check prerequisites before disrupting an existing stack.
if [[ ! -f "$SRS_BINARY" || ! -d "$UI_DIR/node_modules" ]]; then
  echo "FAIL: build trunk/objs/srs and install oryx/ui dependencies first." >&2
  exit 1
fi
for tool in go npm curl redis-cli lsof; do
  command -v "$tool" >/dev/null || { echo "FAIL: missing $tool" >&2; exit 1; }
done
bash "$SCRIPT_DIR/oryx-stack-stop.sh" || exit 1

SRS_PID=""
BACKEND_PID=""
UI_PID=""

save_state() {
  local temporary
  temporary=$(mktemp "${STATE_FILE}.XXXXXX") || return 1
  if ! printf 'SRS_STARTED_PID="%s"\nBACKEND_STARTED_PID="%s"\nUI_STARTED_PID="%s"\n' \
      "$SRS_PID" "$BACKEND_PID" "$UI_PID" > "$temporary" ||
      ! mv -f "$temporary" "$STATE_FILE"; then
    rm -f "$temporary"
    return 1
  fi
}

# Check persistence before starting anything, and save partial startup on
# failure too, so an unsuccessful readiness check cannot orphan tracking.
save_state || exit 1
trap 'save_state || { echo "FAIL: cannot save $STATE_FILE" >&2; exit 1; }' EXIT

if [[ "$(redis-cli ping 2>/dev/null)" != "PONG" ]]; then
  echo "Redis not reachable, starting via 'brew services start redis'..."
  if ! command -v brew &>/dev/null; then
    echo "FAIL: redis is not running and 'brew' is not available to start it." >&2
    exit 1
  fi
  brew services start redis >/dev/null
  if ! wait_for_redis 15; then
    echo "FAIL: redis did not become ready within 15s." >&2
    exit 1
  fi
  echo "Redis: started."
else
  echo "Redis: already running, leaving it alone."
fi

{
  if [[ ! -f "$SRS_BINARY" ]]; then
    echo "FAIL: SRS binary not found at $SRS_BINARY." >&2
    echo "Build it first: cd $WORKSPACE/trunk && ./configure && make" >&2
    exit 1
  fi
  echo "Starting local SRS..."
  (cd "$PLATFORM_DIR" && exec "$SRS_BINARY" -c "$SRS_CONF") >/tmp/oryx-stack-srs.log 2>&1 &
  SRS_PID=$!
  disown "$SRS_PID" 2>/dev/null || true
  if ! wait_for_http "$SRS_API/api/v1/versions" 15; then
    echo "FAIL: SRS did not become ready within 15s. Logs:" >&2
    cat /tmp/oryx-stack-srs.log >&2
    exit 1
  fi
  echo "SRS: started (pid $SRS_PID)."
}

{
  echo "Starting Oryx backend with 'go run .'..."
  (cd "$PLATFORM_DIR" && exec env AUTO_SELF_SIGNED_CERTIFICATE=off go run .) >/tmp/oryx-stack-backend.log 2>&1 &
  BACKEND_PID=$!
  disown "$BACKEND_PID" 2>/dev/null || true
  if ! wait_for_http "$ENDPOINT/terraform/v1/mgmt/versions" 60; then
    echo "FAIL: Oryx backend did not become ready within 60s. Logs:" >&2
    cat /tmp/oryx-stack-backend.log >&2
    exit 1
  fi
  echo "Oryx backend: started (pid $BACKEND_PID)."
}

{
  if [[ ! -d "$UI_DIR/node_modules" ]]; then
    echo "FAIL: UI dependencies not installed at $UI_DIR/node_modules." >&2
    echo "Install them first: cd $UI_DIR && npm install" >&2
    exit 1
  fi
  echo "Starting React dashboard with 'npm start'..."
  (cd "$UI_DIR" && exec npm start) >/tmp/oryx-stack-ui.log 2>&1 &
  UI_PID=$!
  disown "$UI_PID" 2>/dev/null || true
  if ! wait_for_http "$UI_ENDPOINT" 90; then
    echo "FAIL: React dashboard did not become ready within 90s. Logs:" >&2
    cat /tmp/oryx-stack-ui.log >&2
    exit 1
  fi
  echo "React dashboard: started (pid $UI_PID)."
}

save_state || exit 1

echo ""
echo "State recorded in $STATE_FILE"
echo "=== Oryx Stack Start done ==="
