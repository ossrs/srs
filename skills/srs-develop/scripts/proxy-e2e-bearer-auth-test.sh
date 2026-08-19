#!/bin/bash
# E2E test for Bearer authentication across the Go proxy and C++ SRS server.
# Verifies startup validation, protected API and WHIP/WHEP requests, and
# authenticated origin heartbeat registration.
set -euo pipefail

SCRIPT_DIR="$(cd -P "$(dirname "$0")" && pwd)"
WORKSPACE="$SCRIPT_DIR"
while [[ "$WORKSPACE" != "/" && ! -f "$WORKSPACE/go.mod" ]]; do
  WORKSPACE="$(dirname "$WORKSPACE")"
done

if [[ ! -f "$WORKSPACE/go.mod" ]]; then
  echo "Error: go.mod not found walking up from: $SCRIPT_DIR" >&2
  exit 1
fi

PROXY_RTMP_PORT=11935
PROXY_HTTP_API_PORT=11985
PROXY_HTTP_SERVER_PORT=18080
PROXY_WEBRTC_PORT=18000
PROXY_SRT_PORT=20080
PROXY_SYSTEM_API_PORT=12025

# Ports configured by trunk/conf/origin1-for-proxy.conf.
ORIGIN_RTMP_PORT=19351
ORIGIN_HTTP_PORT=8081
ORIGIN_API_PORT=19851
ORIGIN_RTC_PORT=8001
ORIGIN_SRT_PORT=10081

PROXY_BINARY="$WORKSPACE/bin/srs-proxy"
SRS_BINARY="$WORKSPACE/trunk/objs/srs"
PROXY_AUTH_TOKEN="proxy-bearer-e2e-${USER:-user}-$$"
SRS_AUTH_TOKEN="srs-bearer-e2e-${USER:-user}-$$"
TEST_ID="$$"
PROXY_LOG="/tmp/srs-proxy-bearer-e2e-$TEST_ID.log"
ORIGIN_LOG="/tmp/srs-origin-bearer-e2e-$TEST_ID.log"
PROXY_VALIDATION_LOG="/tmp/srs-proxy-bearer-validation-$TEST_ID.log"
SRS_VALIDATION_LOG="/tmp/srs-bearer-validation-$TEST_ID.log"
HEADERS_FILE="/tmp/srs-bearer-headers-$TEST_ID.txt"
BODY_FILE="/tmp/srs-bearer-body-$TEST_ID.txt"

PROXY_PID=""
ORIGIN_PID=""

cleanup() {
  echo ""
  echo "=== Cleaning up ==="
  for pid in "$ORIGIN_PID" "$PROXY_PID"; do
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
    fi
  done
  sleep 1
  for pid in "$ORIGIN_PID" "$PROXY_PID"; do
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
      kill -9 "$pid" 2>/dev/null || true
    fi
  done
  echo "Cleanup done."
}
trap cleanup EXIT

fail_with_logs() {
  echo "FAIL: $1" >&2
  if [[ -s "$PROXY_LOG" ]]; then
    echo "--- Proxy log ---" >&2
    tail -80 "$PROXY_LOG" >&2
  fi
  if [[ -s "$ORIGIN_LOG" ]]; then
    echo "--- SRS origin log ---" >&2
    tail -80 "$ORIGIN_LOG" >&2
  fi
  exit 1
}

expect_status() {
  local label="$1"
  local got="$2"
  local want="$3"
  if [[ "$got" != "$want" ]]; then
    fail_with_logs "$label returned HTTP $got, expected $want"
  fi
  echo "PASS: $label returned HTTP $want."
}

expect_not_status() {
  local label="$1"
  local got="$2"
  local unwanted="$3"
  if [[ "$got" == "$unwanted" ]]; then
    fail_with_logs "$label unexpectedly returned HTTP $got"
  fi
  echo "PASS: $label passed authentication and returned HTTP $got."
}

expect_bearer_challenge() {
  local label="$1"
  if ! tr -d '\r' <"$HEADERS_FILE" | grep -qi '^WWW-Authenticate: Bearer$'; then
    fail_with_logs "$label did not return WWW-Authenticate: Bearer"
  fi
  echo "PASS: $label returned a Bearer challenge."
}

wait_for_proxy() {
  for _ in {1..50}; do
    if curl -fsS "http://127.0.0.1:$PROXY_SYSTEM_API_PORT/api/v1/versions" >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  return 1
}

wait_for_origin() {
  for _ in {1..80}; do
    if curl -fsS -H "Authorization: Bearer $SRS_AUTH_TOKEN" \
      "http://127.0.0.1:$ORIGIN_API_PORT/api/v1/versions" >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  return 1
}

echo "=== E2E Bearer Authentication Test ==="
echo "Workspace: $WORKSPACE"
echo ""

for command in curl grep lsof make; do
  if ! command -v "$command" >/dev/null 2>&1; then
    echo "Error: $command not found in PATH" >&2
    exit 1
  fi
done

# --- Step 0: Clean up stale state ---
rm -f "$WORKSPACE/trunk/objs/origin1.pid"
ALL_PORTS="$PROXY_RTMP_PORT $PROXY_HTTP_API_PORT $PROXY_HTTP_SERVER_PORT $PROXY_WEBRTC_PORT $PROXY_SRT_PORT $PROXY_SYSTEM_API_PORT $ORIGIN_RTMP_PORT $ORIGIN_HTTP_PORT $ORIGIN_API_PORT $ORIGIN_RTC_PORT $ORIGIN_SRT_PORT"
for port in $ALL_PORTS; do
  lsof -ti :"$port" 2>/dev/null | xargs kill 2>/dev/null || true
done
sleep 1

# --- Step 1: Build current proxy and SRS binaries ---
echo "=== Step 1: Building proxy ==="
cd "$WORKSPACE"
make -s

echo "=== Step 2: Building SRS origin ==="
cd "$WORKSPACE/trunk"
if [[ ! -f objs/Makefile ]]; then
  ./configure
fi
make -s

# --- Step 3: Verify startup validation ---
echo "=== Step 3: Verifying authentication startup validation ==="
cd "$WORKSPACE"
if env PROXY_HTTP_API_AUTH_ENABLED=on \
  PROXY_HTTP_API_AUTH_TYPE= \
  PROXY_HTTP_API_AUTH_TOKEN= \
  "$PROXY_BINARY" >"$PROXY_VALIDATION_LOG" 2>&1; then
  fail_with_logs "proxy started without PROXY_HTTP_API_AUTH_TYPE"
fi
if ! grep -q 'PROXY_HTTP_API_AUTH_TYPE' "$PROXY_VALIDATION_LOG"; then
  fail_with_logs "proxy missing-type failure did not identify PROXY_HTTP_API_AUTH_TYPE"
fi
echo "PASS: Proxy rejects enabled authentication without a type."

if env PROXY_HTTP_API_AUTH_ENABLED=on \
  PROXY_HTTP_API_AUTH_TYPE=bearer \
  PROXY_HTTP_API_AUTH_TOKEN= \
  "$PROXY_BINARY" >"$PROXY_VALIDATION_LOG" 2>&1; then
  fail_with_logs "proxy accepted Bearer authentication without a token"
fi
if ! grep -q 'PROXY_HTTP_API_AUTH_TOKEN' "$PROXY_VALIDATION_LOG"; then
  fail_with_logs "proxy missing-token failure did not identify PROXY_HTTP_API_AUTH_TOKEN"
fi
echo "PASS: Proxy rejects Bearer authentication without a token."

if env PROXY_HTTP_API_AUTH_ENABLED=on \
  PROXY_HTTP_API_AUTH_TYPE=basic \
  PROXY_HTTP_API_AUTH_TOKEN="$PROXY_AUTH_TOKEN" \
  "$PROXY_BINARY" >"$PROXY_VALIDATION_LOG" 2>&1; then
  fail_with_logs "proxy accepted Basic authentication"
fi
if ! grep -q 'only supports bearer' "$PROXY_VALIDATION_LOG"; then
  fail_with_logs "proxy Basic-authentication failure did not identify Bearer as required"
fi
echo "PASS: Proxy rejects Basic authentication."

cd "$WORKSPACE/trunk"
if env SRS_HTTP_API_AUTH_ENABLED=on \
  SRS_HTTP_API_AUTH_TYPE=bearer \
  SRS_HTTP_API_AUTH_TOKEN= \
  "$SRS_BINARY" -t -c conf/srs.conf >"$SRS_VALIDATION_LOG" 2>&1; then
  fail_with_logs "SRS accepted Bearer authentication without a token"
fi
if ! grep -q 'SRS_HTTP_API_AUTH_TOKEN' "$SRS_VALIDATION_LOG"; then
  fail_with_logs "SRS missing-token failure did not identify SRS_HTTP_API_AUTH_TOKEN"
fi
echo "PASS: SRS rejects Bearer authentication without a token."

if env SRS_HEARTBEAT_AUTH_ENABLED=on \
  SRS_HEARTBEAT_AUTH_TYPE=bearer \
  SRS_HEARTBEAT_AUTH_TOKEN= \
  "$SRS_BINARY" -t -c conf/srs.conf >"$SRS_VALIDATION_LOG" 2>&1; then
  fail_with_logs "SRS accepted heartbeat Bearer authentication without a token"
fi
if ! grep -q 'SRS_HEARTBEAT_AUTH_TOKEN' "$SRS_VALIDATION_LOG"; then
  fail_with_logs "SRS heartbeat missing-token failure did not identify SRS_HEARTBEAT_AUTH_TOKEN"
fi
echo "PASS: SRS rejects heartbeat Bearer authentication without a token."

# --- Step 4: Start authenticated proxy ---
echo "=== Step 4: Starting authenticated proxy ==="
cd "$WORKSPACE"
env PROXY_HTTP_API_AUTH_ENABLED=on \
  PROXY_HTTP_API_AUTH_TYPE=bearer \
  PROXY_HTTP_API_AUTH_TOKEN="$PROXY_AUTH_TOKEN" \
  PROXY_RTMP_SERVER="$PROXY_RTMP_PORT" \
  PROXY_HTTP_API="$PROXY_HTTP_API_PORT" \
  PROXY_HTTP_SERVER="$PROXY_HTTP_SERVER_PORT" \
  PROXY_WEBRTC_SERVER="$PROXY_WEBRTC_PORT" \
  PROXY_SRT_SERVER="$PROXY_SRT_PORT" \
  PROXY_SYSTEM_API="$PROXY_SYSTEM_API_PORT" \
  PROXY_LOAD_BALANCER_TYPE=memory \
  "$PROXY_BINARY" >"$PROXY_LOG" 2>&1 &
PROXY_PID=$!

if ! wait_for_proxy || ! kill -0 "$PROXY_PID" 2>/dev/null; then
  fail_with_logs "authenticated proxy failed to start"
fi
echo "PASS: Authenticated proxy started."

# --- Step 5: Verify proxy registration authentication ---
echo "=== Step 5: Verifying proxy registration authentication ==="
REGISTER_BODY='{"ip":"1.2.3.4","server":"srv-auth","service":"svc-auth","pid":"12345","rtmp":["1935"],"device_id":"bearer-test"}'

status=$(curl -sS -D "$HEADERS_FILE" -o "$BODY_FILE" -w '%{http_code}' \
  -H 'Content-Type: application/json' -d "$REGISTER_BODY" \
  "http://127.0.0.1:$PROXY_SYSTEM_API_PORT/api/v1/srs/register")
expect_status "Proxy registration without a token" "$status" 401
expect_bearer_challenge "Proxy registration without a token"

status=$(curl -sS -D "$HEADERS_FILE" -o "$BODY_FILE" -w '%{http_code}' \
  -H "Authorization: Bearer $SRS_AUTH_TOKEN" -H 'Content-Type: application/json' \
  -d "$REGISTER_BODY" \
  "http://127.0.0.1:$PROXY_SYSTEM_API_PORT/api/v1/srs/register")
expect_status "Proxy registration with the SRS API token" "$status" 401
expect_bearer_challenge "Proxy registration with the SRS API token"

status=$(curl -sS -D "$HEADERS_FILE" -o "$BODY_FILE" -w '%{http_code}' \
  -H "Authorization: Bearer $PROXY_AUTH_TOKEN" -H 'Content-Type: application/json' \
  -d "$REGISTER_BODY" \
  "http://127.0.0.1:$PROXY_SYSTEM_API_PORT/api/v1/srs/register")
expect_status "Proxy registration with the correct token" "$status" 200
if ! grep -q '"code":0' "$BODY_FILE"; then
  fail_with_logs "authenticated proxy registration did not return code 0"
fi

# --- Step 6: Start SRS and verify its protected API ---
echo "=== Step 6: Starting authenticated SRS origin ==="
cd "$WORKSPACE/trunk"
env SRS_HTTP_API_AUTH_ENABLED=on \
  SRS_HTTP_API_AUTH_TYPE=bearer \
  SRS_HTTP_API_AUTH_TOKEN="$SRS_AUTH_TOKEN" \
  SRS_HTTP_API_AUTH_RTC_BEARER_ENABLED=on \
  SRS_HEARTBEAT_AUTH_ENABLED=on \
  SRS_HEARTBEAT_AUTH_TYPE=bearer \
  SRS_HEARTBEAT_AUTH_TOKEN="$PROXY_AUTH_TOKEN" \
  "$SRS_BINARY" -c conf/origin1-for-proxy.conf >"$ORIGIN_LOG" 2>&1 &
ORIGIN_PID=$!

if ! wait_for_origin || ! kill -0 "$ORIGIN_PID" 2>/dev/null; then
  fail_with_logs "authenticated SRS origin failed to start"
fi
echo "PASS: Authenticated SRS origin started."

status=$(curl -sS -D "$HEADERS_FILE" -o "$BODY_FILE" -w '%{http_code}' \
  "http://127.0.0.1:$ORIGIN_API_PORT/api/v1/versions")
expect_status "SRS API without a token" "$status" 401
expect_bearer_challenge "SRS API without a token"

status=$(curl -sS -D "$HEADERS_FILE" -o "$BODY_FILE" -w '%{http_code}' \
  -H "Authorization: Bearer $PROXY_AUTH_TOKEN" \
  "http://127.0.0.1:$ORIGIN_API_PORT/api/v1/versions")
expect_status "SRS API with the proxy token" "$status" 401
expect_bearer_challenge "SRS API with the proxy token"

status=$(curl -sS -D "$HEADERS_FILE" -o "$BODY_FILE" -w '%{http_code}' \
  -H "Authorization: Bearer $SRS_AUTH_TOKEN" \
  "http://127.0.0.1:$ORIGIN_API_PORT/api/v1/versions")
expect_status "SRS API with the correct token" "$status" 200
if ! grep -q '"code":0' "$BODY_FILE"; then
  fail_with_logs "authenticated SRS API request did not return code 0"
fi

# --- Step 7: Verify WHIP and WHEP Bearer authentication ---
echo "=== Step 7: Verifying WHIP and WHEP Bearer authentication ==="
status=$(curl -sS -X POST -D "$HEADERS_FILE" -o "$BODY_FILE" -w '%{http_code}' \
  -H 'Content-Type: application/sdp' --data-binary 'v=0' \
  "http://127.0.0.1:$ORIGIN_API_PORT/rtc/v1/whip/?app=live&stream=bearer-auth")
expect_status "WHIP without a token" "$status" 401
expect_bearer_challenge "WHIP without a token"

status=$(curl -sS -X POST -D "$HEADERS_FILE" -o "$BODY_FILE" -w '%{http_code}' \
  -H "Authorization: Bearer $PROXY_AUTH_TOKEN" \
  -H 'Content-Type: application/sdp' --data-binary 'v=0' \
  "http://127.0.0.1:$ORIGIN_API_PORT/rtc/v1/whep/?app=live&stream=bearer-auth")
expect_status "WHEP with the proxy token" "$status" 401
expect_bearer_challenge "WHEP with the proxy token"

status=$(curl -sS -X POST -D "$HEADERS_FILE" -o "$BODY_FILE" -w '%{http_code}' \
  -H "Authorization: Bearer $SRS_AUTH_TOKEN" \
  -H 'Content-Type: application/sdp' --data-binary 'v=0' \
  "http://127.0.0.1:$ORIGIN_API_PORT/rtc/v1/whip/?app=live&stream=bearer-auth")
expect_not_status "WHIP with the correct token" "$status" 401

status=$(curl -sS -X POST -D "$HEADERS_FILE" -o "$BODY_FILE" -w '%{http_code}' \
  -H "Authorization: Bearer $SRS_AUTH_TOKEN" \
  -H 'Content-Type: application/sdp' --data-binary 'v=0' \
  "http://127.0.0.1:$ORIGIN_API_PORT/rtc/v1/whep/?app=live&stream=bearer-auth")
expect_not_status "WHEP with the correct token" "$status" 401

# --- Step 8: Verify authenticated heartbeat registration ---
echo "=== Step 8: Verifying authenticated SRS heartbeat ==="
registered=0
for _ in {1..120}; do
  if grep -q 'device=origin1' "$PROXY_LOG"; then
    registered=1
    break
  fi
  sleep 0.1
done
if [[ "$registered" != 1 ]]; then
  fail_with_logs "SRS heartbeat did not register with the authenticated proxy"
fi
echo "PASS: SRS heartbeat registered with the authenticated proxy."

echo ""
echo "=== E2E Bearer Authentication Test PASSED ==="
