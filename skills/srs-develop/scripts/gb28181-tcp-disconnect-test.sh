#!/bin/bash
# E2E test for external-SIP GB28181 TCP disconnect cleanup. It builds and
# starts a disposable SRS, publishes media over TCP, closes it, then verifies
# that the same stream ID and SSRC are reusable.
set -euo pipefail

SCRIPT_DIR="$(cd -P "$(dirname "$0")" && pwd)"
WORKSPACE="$SCRIPT_DIR"
while [[ "$WORKSPACE" != "/" && ! -f "$WORKSPACE/trunk/configure" ]]; do
  WORKSPACE="$(dirname "$WORKSPACE")"
done

if [[ ! -f "$WORKSPACE/trunk/configure" ]]; then
  echo "Error: SRS workspace not found walking up from: $SCRIPT_DIR" >&2
  exit 1
fi

SRS_BINARY="$WORKSPACE/trunk/objs/srs"
GB_PUBLISH_STREAM="$SCRIPT_DIR/gb28181-publish-stream.sh"
RTMP_PORT="${SRS_GB_RTMP_PORT:-21935}"
HTTP_API_PORT="${SRS_GB_HTTP_API_PORT:-21985}"
MEDIA_PORT="${SRS_GB_MEDIA_PORT:-29000}"
STREAM_ID="${SRS_GB_STREAM_ID:-gb-lifecycle-$$}"
SSRC="${SRS_GB_SSRC:-47190001}"
TEST_DIR=$(mktemp -d "${TMPDIR:-/tmp}/srs-gb-tcp-disconnect.XXXXXX")
SRS_CONF="$TEST_DIR/srs.conf"
SRS_LOG="$TEST_DIR/srs.log"
SRS_PID_FILE="$TEST_DIR/srs.pid"
BUILD_LOG="$TEST_DIR/build.log"
SRS_PID=""
TEST_PASSED=0

cleanup() {
  echo ""
  echo "=== Cleaning up ==="
  if [[ -n "$SRS_PID" ]] && kill -0 "$SRS_PID" 2>/dev/null; then
    kill "$SRS_PID" 2>/dev/null || true
    for _ in {1..20}; do
      if ! kill -0 "$SRS_PID" 2>/dev/null; then
        break
      fi
      sleep 0.1
    done
  fi
  if [[ -n "$SRS_PID" ]] && kill -0 "$SRS_PID" 2>/dev/null; then
    kill -9 "$SRS_PID" 2>/dev/null || true
  fi
  if [[ -n "$SRS_PID" ]]; then
    wait "$SRS_PID" 2>/dev/null || true
  fi

  if [[ "$TEST_PASSED" != "1" && -f "$SRS_LOG" ]]; then
    echo "--- SRS log ---" >&2
    tail -80 "$SRS_LOG" >&2
  fi
  rm -rf "$TEST_DIR"
  echo "Cleanup done."
}
trap cleanup EXIT

echo "=== E2E GB28181 TCP Disconnect Cleanup Test ==="
echo "Workspace: $WORKSPACE"
echo "Session: id=$STREAM_ID, ssrc=$SSRC"
echo ""

for tool in curl python3 make; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "Error: $tool is required" >&2
    exit 1
  fi
done
if [[ ! -x "$GB_PUBLISH_STREAM" ]]; then
  echo "Error: GB stream publisher is not executable: $GB_PUBLISH_STREAM" >&2
  exit 1
fi

echo "=== Step 1: Building SRS with GB28181 ==="
if [[ "${SRS_GB_SKIP_BUILD:-0}" != "1" ]]; then
  (
    cd "$WORKSPACE/trunk"
    ./configure --gb28181=on >"$BUILD_LOG" 2>&1
    make -s >>"$BUILD_LOG" 2>&1
  )
elif [[ ! -x "$SRS_BINARY" ]]; then
  echo "Error: SRS_GB_SKIP_BUILD=1 but binary is missing: $SRS_BINARY" >&2
  exit 1
fi
echo "SRS built: $SRS_BINARY"

# No SIP listener is configured. SRS only exposes its HTTP publish API and the
# shared GB28181 media TCP listener; signaling belongs to an external service.
cat >"$SRS_CONF" <<EOF
listen $RTMP_PORT;
pid $SRS_PID_FILE;
max_connections 1000;
daemon off;
srs_log_tank console;

stream_caster {
  enabled on;
  caster gb28181;
  output rtmp://127.0.0.1:$RTMP_PORT/live/[stream];
  listen $MEDIA_PORT;
}

http_api {
  enabled on;
  listen $HTTP_API_PORT;
}

vhost __defaultVhost__ {
}
EOF

echo "=== Step 2: Starting SRS without an embedded SIP server ==="
(
  cd "$WORKSPACE/trunk"
  exec "$SRS_BINARY" -c "$SRS_CONF" >"$SRS_LOG" 2>&1
) &
SRS_PID=$!
echo "SRS PID: $SRS_PID"

READY=0
for _ in {1..50}; do
  if curl --silent --fail "http://127.0.0.1:$HTTP_API_PORT/api/v1/versions" >/dev/null; then
    READY=1
    break
  fi
  if ! kill -0 "$SRS_PID" 2>/dev/null; then
    break
  fi
  sleep 0.1
done
if [[ "$READY" != "1" ]]; then
  echo "FAIL: SRS did not start" >&2
  exit 1
fi
echo "SRS started: API :$HTTP_API_PORT, GB TCP :$MEDIA_PORT"

echo "=== Step 3: Creating and disconnecting a GB stream publisher ==="
"$GB_PUBLISH_STREAM" \
  --api-url "http://127.0.0.1:$HTTP_API_PORT" \
  --media-host 127.0.0.1 \
  --media-port "$MEDIA_PORT" \
  --id "$STREAM_ID" \
  --ssrc "$SSRC"

sleep 1

echo "=== Step 4: Republishing the same ID and SSRC ==="
SECOND_RESPONSE=$(curl --silent --show-error \
  --request POST \
  --header 'Content-Type: application/json' \
  --data "{\"id\":\"$STREAM_ID\",\"ssrc\":\"$SSRC\"}" \
  "http://127.0.0.1:$HTTP_API_PORT/gb/v1/publish/")
echo "Republish response: $SECOND_RESPONSE"

SECOND_CODE=$(printf '%s' "$SECOND_RESPONSE" | python3 -c 'import json, sys; print(json.load(sys.stdin).get("code", -1))')
if [[ "$SECOND_CODE" != "0" ]]; then
  echo "FAIL: expected reusable ID and SSRC, got code $SECOND_CODE" >&2
  exit 1
fi

TEST_PASSED=1
echo ""
echo "=== E2E GB28181 TCP Disconnect Cleanup Test PASSED ==="
