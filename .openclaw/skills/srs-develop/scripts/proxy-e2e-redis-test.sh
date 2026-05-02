#!/bin/bash
# E2E test for RTMP proxy Redis load balancer: starts two proxy instances with
# Redis-backed shared state + one SRS origin. The origin registers through proxy A,
# publishing goes through proxy A, and playback goes through proxy B. Playback must
# succeed because proxy B resolves the stream-to-origin mapping from Redis.
set -e

SCRIPT_DIR="$(cd -P "$(dirname "$0")" && pwd)"
# Navigate: scripts/ -> srs-develop/ -> skills/ -> .openclaw/ -> srs
WORKSPACE="$(cd -P "$SCRIPT_DIR/../../../.." && pwd)"

if [[ ! -f "$WORKSPACE/go.mod" ]]; then
  echo "Error: go.mod not found in WORKSPACE: $WORKSPACE" >&2
  exit 1
fi

# Ports — use high ports to avoid conflicts with running services.
# Each proxy starts ALL servers, so each proxy needs a unique full port set.
PROXY_A_RTMP_PORT=11935
PROXY_A_HTTP_API_PORT=11985
PROXY_A_HTTP_SERVER_PORT=18080
PROXY_A_WEBRTC_PORT=18000
PROXY_A_SRT_PORT=20080
PROXY_A_SYSTEM_API_PORT=12025

PROXY_B_RTMP_PORT=11936
PROXY_B_HTTP_API_PORT=11986
PROXY_B_HTTP_SERVER_PORT=18081
PROXY_B_WEBRTC_PORT=18001
PROXY_B_SRT_PORT=20081
PROXY_B_SYSTEM_API_PORT=12026

REDIS_HOST="${PROXY_REDIS_HOST:-127.0.0.1}"
REDIS_PORT="${PROXY_REDIS_PORT:-6379}"
REDIS_PASSWORD="${PROXY_REDIS_PASSWORD:-}"
REDIS_DB="${PROXY_REDIS_DB:-0}"
PYTHON_BIN="${PYTHON_BIN:-python3}"

SOURCE_FLV="$WORKSPACE/trunk/doc/source.flv"
SRS_BINARY="$WORKSPACE/trunk/objs/srs"
TEST_STREAM_URL="__defaultVhost__/live/livestream"

# PIDs to clean up on exit.
PROXY_A_PID=""
PROXY_B_PID=""
ORIGIN_PID=""
FFMPEG_PID=""

redis_cli() {
  if [[ -n "$REDIS_PASSWORD" ]]; then
    redis-cli -h "$REDIS_HOST" -p "$REDIS_PORT" -a "$REDIS_PASSWORD" -n "$REDIS_DB" "$@"
  else
    redis-cli -h "$REDIS_HOST" -p "$REDIS_PORT" -n "$REDIS_DB" "$@"
  fi
}

cleanup_redis_state() {
  # Remove only the Redis records created by this E2E test. Never flush the DB,
  # and never delete every srs-proxy-* key because the same Redis DB may be used
  # by another proxy/origin-cluster test or by a developer's local proxy.
  if ! command -v redis-cli &>/dev/null; then
    return
  fi
  if ! command -v "$PYTHON_BIN" &>/dev/null; then
    echo "Skip Redis cleanup: $PYTHON_BIN is not available"
    return
  fi
  if ! redis_cli ping 2>/dev/null | grep -q "PONG"; then
    echo "Skip Redis cleanup: Redis is not available at $REDIS_HOST:$REDIS_PORT db=$REDIS_DB"
    return
  fi

  local count=0
  local stream_key="srs-proxy-url:$TEST_STREAM_URL"
  if [[ "$(redis_cli exists "$stream_key" 2>/dev/null || echo 0)" != "0" ]]; then
    redis_cli del "$stream_key" >/dev/null 2>&1 || true
    count=$((count + 1))
  fi

  # The origin server generates its server key from runtime IDs, so discover only
  # server records that match this test origin's identity and configured ports.
  local server_keys=()
  local key value
  while IFS= read -r key; do
    [[ -z "$key" ]] && continue
    value="$(redis_cli get "$key" 2>/dev/null || true)"
    if [[ "$value" == *'"device_id":"origin1"'* && \
          "$value" == *'"rtmp":["19351"]'* && \
          "$value" == *'"http":["8081"]'* && \
          "$value" == *'"api":["19851"]'* ]]; then
      server_keys+=("$key")
      redis_cli del "$key" >/dev/null 2>&1 || true
      count=$((count + 1))
    fi
  done < <(redis_cli --scan --pattern 'srs-proxy-server:*' 2>/dev/null || true)

  # Keep the shared server index, but remove only the test origin server keys.
  if [[ ${#server_keys[@]} -gt 0 ]]; then
    local servers_json updated_json
    servers_json="$(redis_cli get srs-proxy-all-servers 2>/dev/null || true)"
    if [[ -n "$servers_json" ]]; then
      updated_json="$($PYTHON_BIN - "$servers_json" "${server_keys[@]}" <<'PY'
import json, sys
servers = json.loads(sys.argv[1]) if sys.argv[1] else []
remove = set(sys.argv[2:])
servers = [server for server in servers if server not in remove]
print(json.dumps(servers, separators=(",", ":")))
PY
)"
      if [[ "$updated_json" == "[]" ]]; then
        redis_cli del srs-proxy-all-servers >/dev/null 2>&1 || true
      else
        redis_cli set srs-proxy-all-servers "$updated_json" >/dev/null 2>&1 || true
      fi
    fi
  fi

  echo "Cleaned $count Redis proxy test key(s)."
}
cleanup() {
  echo ""
  echo "=== Cleaning up ==="
  for pid in $PROXY_A_PID $PROXY_B_PID $ORIGIN_PID $FFMPEG_PID; do
    if kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
    fi
  done
  sleep 1
  for pid in $PROXY_A_PID $PROXY_B_PID $ORIGIN_PID $FFMPEG_PID; do
    if kill -0 "$pid" 2>/dev/null; then
      kill -9 "$pid" 2>/dev/null || true
    fi
  done
  cleanup_redis_state
  echo "Cleanup done."
}
trap cleanup EXIT

echo "=== E2E RTMP Proxy Redis Load Balancer Test ==="
echo "Workspace: $WORKSPACE"
echo "Redis: $REDIS_HOST:$REDIS_PORT db=$REDIS_DB"
echo ""

# --- Pre-checks ---
if [[ ! -f "$SOURCE_FLV" ]]; then
  echo "Error: test source not found: $SOURCE_FLV" >&2
  exit 1
fi
if ! command -v ffmpeg &>/dev/null; then
  echo "Error: ffmpeg not found in PATH" >&2
  exit 1
fi
if ! command -v ffprobe &>/dev/null; then
  echo "Error: ffprobe not found in PATH" >&2
  exit 1
fi
if ! command -v redis-cli &>/dev/null; then
  echo "Error: redis-cli not found in PATH" >&2
  echo "Install Redis on macOS with: brew install redis" >&2
  exit 1
fi
if ! redis_cli ping 2>/dev/null | grep -q "PONG"; then
  echo "Error: Redis is not available at $REDIS_HOST:$REDIS_PORT db=$REDIS_DB" >&2
  echo "Start Redis on macOS with: brew services start redis" >&2
  echo "Or run a foreground Redis with: redis-server" >&2
  exit 1
fi

# Origin ports (from origin1-for-proxy.conf).
ORIGIN_RTMP_PORT=19351
ORIGIN_HTTP_PORT=8081
ORIGIN_API_PORT=19851
ORIGIN_RTC_PORT=8001
ORIGIN_SRT_PORT=10081

# --- Step 0: Clean up stale state ---
# Remove stale SRS PID file that prevents restart.
rm -f "$WORKSPACE/trunk/objs/origin1.pid"
cleanup_redis_state
# Kill any leftover processes on our ports (proxy A + proxy B + origin).
ALL_PORTS="$PROXY_A_RTMP_PORT $PROXY_A_HTTP_API_PORT $PROXY_A_HTTP_SERVER_PORT $PROXY_A_WEBRTC_PORT $PROXY_A_SRT_PORT $PROXY_A_SYSTEM_API_PORT $PROXY_B_RTMP_PORT $PROXY_B_HTTP_API_PORT $PROXY_B_HTTP_SERVER_PORT $PROXY_B_WEBRTC_PORT $PROXY_B_SRT_PORT $PROXY_B_SYSTEM_API_PORT $ORIGIN_RTMP_PORT $ORIGIN_HTTP_PORT $ORIGIN_API_PORT $ORIGIN_RTC_PORT $ORIGIN_SRT_PORT"
for port in $ALL_PORTS; do
  lsof -ti :"$port" 2>/dev/null | xargs kill 2>/dev/null || true
done
sleep 1

# --- Step 1: Build proxy ---
echo "=== Step 1: Building proxy ==="
cd "$WORKSPACE"
make -s 2>&1
echo "Proxy built: $WORKSPACE/bin/srs-proxy"

# --- Step 2: Build SRS origin (if not already built) ---
if [[ ! -f "$SRS_BINARY" ]]; then
  echo "=== Step 2: Building SRS origin ==="
  cd "$WORKSPACE/trunk"
  ./configure && make 2>&1 | tail -3
  echo "SRS origin built: $SRS_BINARY"
else
  echo "=== Step 2: SRS origin already built ==="
fi

# --- Step 3: Start proxy A ---
echo "=== Step 3: Starting proxy A (RTMP :$PROXY_A_RTMP_PORT, System API :$PROXY_A_SYSTEM_API_PORT) ==="
cd "$WORKSPACE"
env PROXY_RTMP_SERVER=$PROXY_A_RTMP_PORT \
    PROXY_HTTP_API=$PROXY_A_HTTP_API_PORT \
    PROXY_HTTP_SERVER=$PROXY_A_HTTP_SERVER_PORT \
    PROXY_WEBRTC_SERVER=$PROXY_A_WEBRTC_PORT \
    PROXY_SRT_SERVER=$PROXY_A_SRT_PORT \
    PROXY_SYSTEM_API=$PROXY_A_SYSTEM_API_PORT \
    PROXY_LOAD_BALANCER_TYPE=redis \
    PROXY_REDIS_HOST="$REDIS_HOST" \
    PROXY_REDIS_PORT="$REDIS_PORT" \
    PROXY_REDIS_PASSWORD="$REDIS_PASSWORD" \
    PROXY_REDIS_DB="$REDIS_DB" \
    ./bin/srs-proxy >/tmp/srs-proxy-redis-a-e2e.log 2>&1 &
PROXY_A_PID=$!
echo "Proxy A PID: $PROXY_A_PID"
sleep 1

if ! kill -0 "$PROXY_A_PID" 2>/dev/null; then
  echo "Error: proxy A failed to start. Logs:" >&2
  cat /tmp/srs-proxy-redis-a-e2e.log >&2
  exit 1
fi
echo "Proxy A started."

# --- Step 4: Start proxy B ---
echo "=== Step 4: Starting proxy B (RTMP :$PROXY_B_RTMP_PORT, System API :$PROXY_B_SYSTEM_API_PORT) ==="
cd "$WORKSPACE"
env PROXY_RTMP_SERVER=$PROXY_B_RTMP_PORT \
    PROXY_HTTP_API=$PROXY_B_HTTP_API_PORT \
    PROXY_HTTP_SERVER=$PROXY_B_HTTP_SERVER_PORT \
    PROXY_WEBRTC_SERVER=$PROXY_B_WEBRTC_PORT \
    PROXY_SRT_SERVER=$PROXY_B_SRT_PORT \
    PROXY_SYSTEM_API=$PROXY_B_SYSTEM_API_PORT \
    PROXY_LOAD_BALANCER_TYPE=redis \
    PROXY_REDIS_HOST="$REDIS_HOST" \
    PROXY_REDIS_PORT="$REDIS_PORT" \
    PROXY_REDIS_PASSWORD="$REDIS_PASSWORD" \
    PROXY_REDIS_DB="$REDIS_DB" \
    ./bin/srs-proxy >/tmp/srs-proxy-redis-b-e2e.log 2>&1 &
PROXY_B_PID=$!
echo "Proxy B PID: $PROXY_B_PID"
sleep 1

if ! kill -0 "$PROXY_B_PID" 2>/dev/null; then
  echo "Error: proxy B failed to start. Logs:" >&2
  cat /tmp/srs-proxy-redis-b-e2e.log >&2
  exit 1
fi
echo "Proxy B started."

# --- Step 5: Start SRS origin ---
echo "=== Step 5: Starting SRS origin ==="
ulimit -n 10000 2>/dev/null || true
cd "$WORKSPACE/trunk"
./objs/srs -c conf/origin1-for-proxy.conf >/tmp/srs-origin-redis-e2e.log 2>&1 &
ORIGIN_PID=$!
echo "SRS origin PID: $ORIGIN_PID"

# Wait for SRS to start and register with proxy A (heartbeat interval is 9s).
echo "Waiting for SRS origin to register with proxy A and Redis (up to 15s)..."
sleep 12

if ! kill -0 "$ORIGIN_PID" 2>/dev/null; then
  echo "Error: SRS origin failed to start. Logs:" >&2
  cat /tmp/srs-origin-redis-e2e.log >&2
  exit 1
fi
if ! redis_cli --scan --pattern 'srs-proxy-server:*' | grep -q 'srs-proxy-server:'; then
  echo "Error: SRS origin did not register in Redis. Proxy A logs:" >&2
  cat /tmp/srs-proxy-redis-a-e2e.log >&2
  exit 1
fi
echo "SRS origin started and registered in Redis."

# --- Step 6: Publish RTMP stream to proxy A ---
echo "=== Step 6: Publishing RTMP stream to proxy A ==="
ffmpeg -stream_loop -1 -re -i "$SOURCE_FLV" -c copy -f flv \
  "rtmp://localhost:$PROXY_A_RTMP_PORT/live/livestream" >/tmp/srs-ffmpeg-redis-e2e.log 2>&1 &
FFMPEG_PID=$!
echo "FFmpeg publisher PID: $FFMPEG_PID"

# Wait for stream to stabilize.
sleep 5

if ! kill -0 "$FFMPEG_PID" 2>/dev/null; then
  echo "Error: FFmpeg publisher failed. Logs:" >&2
  cat /tmp/srs-ffmpeg-redis-e2e.log >&2
  exit 1
fi
echo "Stream publishing through proxy A."

# --- Step 7: Verify RTMP playback through proxy B ---
echo "=== Step 7: Verifying RTMP playback through proxy B ==="
PROBE_OUTPUT=$(ffprobe -v error -show_streams \
  "rtmp://localhost:$PROXY_B_RTMP_PORT/live/livestream" 2>&1 || true)

if echo "$PROBE_OUTPUT" | grep -q "codec_type=video"; then
  echo "PASS: Video stream detected through proxy B."
else
  echo "FAIL: No video stream detected through proxy B." >&2
  echo "ffprobe output:" >&2
  echo "$PROBE_OUTPUT" >&2
  echo "Proxy B logs:" >&2
  cat /tmp/srs-proxy-redis-b-e2e.log >&2
  exit 1
fi

if echo "$PROBE_OUTPUT" | grep -q "codec_type=audio"; then
  echo "PASS: Audio stream detected through proxy B."
else
  echo "FAIL: No audio stream detected through proxy B." >&2
  echo "ffprobe output:" >&2
  echo "$PROBE_OUTPUT" >&2
  echo "Proxy B logs:" >&2
  cat /tmp/srs-proxy-redis-b-e2e.log >&2
  exit 1
fi

echo ""
echo "=== E2E RTMP Proxy Redis Load Balancer Test PASSED ==="
