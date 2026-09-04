#!/bin/bash
# Smoke test for the local Oryx platform API: version (no-auth health check),
# password login, and security-key (Bearer) authentication.
#
# Requires the shared local stack (Redis, SRS, Oryx Go backend) to already be
# running -- start it once with oryx-stack-start.sh. This script does not
# manage server lifecycle itself, so it is safe to run concurrently with
# other oryx-*-test.sh scripts against that same shared stack.

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
ENDPOINT="${ORYX_ENDPOINT:-http://localhost:2022}"
SRS_API="${SRS_API_ENDPOINT:-http://localhost:1985}"
ENV_FILE="$PLATFORM_DIR/containers/data/config/.env"

echo "=== Oryx API Smoke Test ==="
echo "Endpoint: $ENDPOINT"
echo ""

# --- Step 0: Require the shared stack to already be running ---
if ! curl -sS -m 2 -o /dev/null "$SRS_API/api/v1/versions" 2>/dev/null || \
   ! curl -sS -m 2 -o /dev/null "$ENDPOINT/terraform/v1/mgmt/versions" 2>/dev/null; then
  echo "FAIL: local Oryx stack is not running. Start it first:" >&2
  echo "  bash $SCRIPT_DIR/oryx-stack-start.sh" >&2
  exit 1
fi

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
# The backend serializes logins with a mutex and replies "login is running,
# try later" to a losing concurrent request -- expected when multiple
# oryx-*-test.sh scripts log in around the same moment, not a real failure.
# Retry past it instead of hard-failing.
TOKEN=""
BEARER=""
for ((i = 1; i <= 10; i++)); do
  LOGIN_RESP=$(curl -sS -m 5 -X POST "$ENDPOINT/terraform/v1/mgmt/login" \
    -H 'Content-Type: application/json' \
    -d "{\"password\":\"$PASSWORD\"}")
  TOKEN=$(echo "$LOGIN_RESP" | sed -n 's/.*"token":"\([^"]*\)".*/\1/p')
  BEARER=$(echo "$LOGIN_RESP" | sed -n 's/.*"bearer":"\([^"]*\)".*/\1/p')
  [[ -n "$TOKEN" && -n "$BEARER" ]] && break
  sleep 1
done
if [[ -z "$TOKEN" || -z "$BEARER" ]]; then
  echo "FAIL: login did not return a token/security key after retries: $LOGIN_RESP" >&2
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
