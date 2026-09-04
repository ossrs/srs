#!/bin/bash
# Smoke test for the local Oryx platform API: version (no-auth health check),
# password login, and security-key (Bearer) authentication.
#
# Starts Redis (if unreachable), local SRS, and the Oryx Go backend as needed
# to run the checks, then stops only the processes it started -- anything
# already running before this script was invoked is left alone. Does not
# start the React dashboard (npm start); it is not needed to exercise the API.

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
SRS_BINARY="$WORKSPACE/trunk/objs/srs"
SRS_CONF="containers/conf/srs.release-local.conf"
ENDPOINT="${ORYX_ENDPOINT:-http://localhost:2022}"
SRS_API="${SRS_API_ENDPOINT:-http://localhost:1985}"
ENV_FILE="$PLATFORM_DIR/containers/data/config/.env"

# PIDs of processes this script started; empty means "already running,
# leave it alone" and cleanup skips it.
SRS_PID=""
BACKEND_PID=""

cleanup() {
  echo ""
  echo "=== Cleaning up ==="
  if [[ -n "$BACKEND_PID" ]]; then
    echo "Stopping Oryx backend (pid $BACKEND_PID)..."
    kill "$BACKEND_PID" 2>/dev/null || true
  fi
  if [[ -n "$SRS_PID" ]]; then
    echo "Stopping local SRS (pid $SRS_PID)..."
    kill "$SRS_PID" 2>/dev/null || true
  fi
  sleep 1
  if [[ -n "$BACKEND_PID" ]]; then
    kill -9 "$BACKEND_PID" 2>/dev/null || true
    # "go run" builds and execs a child process; the wrapper PID above may
    # not own it, so also reap anything still bound to the backend ports.
    for port in 2022 2024 2443; do
      lsof -ti :"$port" 2>/dev/null | xargs kill -9 2>/dev/null || true
    done
  fi
  if [[ -n "$SRS_PID" ]]; then
    kill -9 "$SRS_PID" 2>/dev/null || true
    for port in 1935 1985 8080 8000 10080; do
      lsof -ti :"$port" 2>/dev/null | xargs kill -9 2>/dev/null || true
    done
  fi
  echo "Cleanup done. Redis is left running (shared service)."
}
trap cleanup EXIT

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

echo "=== Oryx API Smoke Test ==="
echo "Endpoint: $ENDPOINT"
echo ""

# --- Step 0: Ensure Redis, SRS, and the Oryx Go backend are up ---
echo "=== Step 0: Ensure Redis, SRS, and the Oryx Go backend are running ==="

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

if ! curl -sS -m 2 -o /dev/null "$SRS_API/api/v1/versions" 2>/dev/null; then
  if [[ ! -f "$SRS_BINARY" ]]; then
    echo "FAIL: SRS binary not found at $SRS_BINARY." >&2
    echo "Build it first: cd $WORKSPACE/trunk && ./configure && make" >&2
    exit 1
  fi
  echo "SRS not reachable, starting local SRS..."
  (cd "$PLATFORM_DIR" && exec "$SRS_BINARY" -c "$SRS_CONF") >/tmp/oryx-smoke-srs.log 2>&1 &
  SRS_PID=$!
  if ! wait_for_http "$SRS_API/api/v1/versions" 15; then
    echo "FAIL: SRS did not become ready within 15s. Logs:" >&2
    cat /tmp/oryx-smoke-srs.log >&2
    exit 1
  fi
  echo "SRS: started (pid $SRS_PID)."
else
  echo "SRS: already running, leaving it alone."
fi

if ! curl -sS -m 2 -o /dev/null "$ENDPOINT/terraform/v1/mgmt/versions" 2>/dev/null; then
  echo "Oryx backend not reachable, starting 'go run .'..."
  (cd "$PLATFORM_DIR" && exec env AUTO_SELF_SIGNED_CERTIFICATE=off go run .) >/tmp/oryx-smoke-backend.log 2>&1 &
  BACKEND_PID=$!
  if ! wait_for_http "$ENDPOINT/terraform/v1/mgmt/versions" 60; then
    echo "FAIL: Oryx backend did not become ready within 60s. Logs:" >&2
    cat /tmp/oryx-smoke-backend.log >&2
    exit 1
  fi
  echo "Oryx backend: started (pid $BACKEND_PID)."
else
  echo "Oryx backend: already running, leaving it alone."
fi
echo ""

# --- Step 1: Version, the no-auth health-check API ---
echo "=== Step 1: GET /terraform/v1/mgmt/versions ==="
if ! VERSIONS_RESP=$(curl -sS -m 5 "$ENDPOINT/terraform/v1/mgmt/versions" 2>&1); then
  echo "FAIL: cannot reach $ENDPOINT: $VERSIONS_RESP" >&2
  exit 1
fi
VERSION=$(echo "$VERSIONS_RESP" | sed -n 's/.*"version":"\([^"]*\)".*/\1/p')
if [[ -z "$VERSION" ]]; then
  echo "FAIL: no version in response: $VERSIONS_RESP" >&2
  exit 1
fi
echo "PASS: platform version=$VERSION"
echo ""

# --- Step 2: Resolve the mgmt password used for login ---
PASSWORD="${MGMT_PASSWORD:-}"
if [[ -z "$PASSWORD" && -f "$ENV_FILE" ]]; then
  # godotenv quotes values, e.g. MGMT_PASSWORD="abc123" -- strip the quotes.
  PASSWORD=$(sed -n 's/^MGMT_PASSWORD="\(.*\)"$/\1/p' "$ENV_FILE")
fi
if [[ -z "$PASSWORD" ]]; then
  echo "FAIL: no MGMT_PASSWORD found (env var or $ENV_FILE)." >&2
  echo "Set MGMT_PASSWORD, or initialize Oryx once via http://localhost:3000 first." >&2
  exit 1
fi

# --- Step 3: Login by password, exchange it for the security key (Bearer) ---
echo "=== Step 2: POST /terraform/v1/mgmt/login (password -> security key) ==="
LOGIN_RESP=$(curl -sS -m 5 -X POST "$ENDPOINT/terraform/v1/mgmt/login" \
  -H 'Content-Type: application/json' \
  -d "{\"password\":\"$PASSWORD\"}")
TOKEN=$(echo "$LOGIN_RESP" | sed -n 's/.*"token":"\([^"]*\)".*/\1/p')
BEARER=$(echo "$LOGIN_RESP" | sed -n 's/.*"bearer":"\([^"]*\)".*/\1/p')
if [[ -z "$TOKEN" || -z "$BEARER" ]]; then
  echo "FAIL: login did not return a token/security key: $LOGIN_RESP" >&2
  exit 1
fi
echo "PASS: login ok, token=${#TOKEN} bytes, security key=${#BEARER} bytes"
echo ""

# --- Step 4: Use the security key to authenticate an API request ---
echo "=== Step 3: POST /terraform/v1/mgmt/token with Authorization: Bearer <security key> ==="
TOKEN_RESP=$(curl -sS -m 5 -X POST "$ENDPOINT/terraform/v1/mgmt/token" \
  -H "Authorization: Bearer $BEARER" \
  -H 'Content-Type: application/json' \
  -d '{}')
NEW_TOKEN=$(echo "$TOKEN_RESP" | sed -n 's/.*"token":"\([^"]*\)".*/\1/p')
if [[ -z "$NEW_TOKEN" ]]; then
  echo "FAIL: security key did not authenticate: $TOKEN_RESP" >&2
  exit 1
fi
echo "PASS: security key authenticated, new token=${#NEW_TOKEN} bytes"
echo ""

echo "=== Oryx API Smoke Test PASSED ==="
