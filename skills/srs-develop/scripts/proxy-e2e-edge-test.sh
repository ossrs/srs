#!/bin/bash
# E2E test for proxy + edge + origin: starts proxy + one SRS edge (registered
# with proxy) + one SRS upstream origin (not registered). Publishes a single
# RTMP stream through proxy -> edge -> origin, then plays the stream twice
# concurrently from the proxy -> edge with a delay so the second player is a
# late joiner on an already-active edge-pull. Both players must succeed.
set -e

SCRIPT_DIR="$(cd -P "$(dirname "$0")" && pwd)"
# Walk up from SCRIPT_DIR looking for go.mod. This avoids brittle "../../../.."
# counting when the skills directory is reached via a symlink (which changes
# the symbolic vs. physical depth).
WORKSPACE="$SCRIPT_DIR"
while [[ "$WORKSPACE" != "/" && ! -f "$WORKSPACE/go.mod" ]]; do
  WORKSPACE="$(dirname "$WORKSPACE")"
done

if [[ ! -f "$WORKSPACE/go.mod" ]]; then
  echo "Error: go.mod not found walking up from: $SCRIPT_DIR" >&2
  exit 1
fi

# Proxy ports — high range, avoids the SRS port range.
PROXY_RTMP_PORT=11935
PROXY_HTTP_API_PORT=11985
PROXY_HTTP_SERVER_PORT=18080
PROXY_WEBRTC_PORT=18000
PROXY_SRT_PORT=20080
PROXY_SYSTEM_API_PORT=12025

# Origin ports (from origin-for-edge.conf) — upstream of the edge, NOT
# registered with the proxy. Distinct from origin1/2/3 to avoid collisions
# when running this test alongside the other proxy E2E tests.
ORIGIN_RTMP_PORT=19360
ORIGIN_API_PORT=19860

# Edge ports (from edge-for-proxy.conf) — what the proxy treats as its backend.
EDGE_RTMP_PORT=19361
EDGE_HTTP_PORT=8091
EDGE_API_PORT=19861

SOURCE_FLV="$WORKSPACE/trunk/doc/source.flv"
SRS_BINARY="$WORKSPACE/trunk/objs/srs"
ORIGIN_CONF="$WORKSPACE/trunk/conf/origin-for-edge.conf"
EDGE_CONF="$WORKSPACE/trunk/conf/edge-for-proxy.conf"
# Randomize per run so each invocation starts from clean state and never
# shares state with sibling E2E tests publishing to live/livestream.
STREAM_NAME="edge$(date +%s)"
STREAM_PATH="live/$STREAM_NAME"

# PIDs to clean up on exit.
PROXY_PID=""
ORIGIN_PID=""
EDGE_PID=""
PUBLISH_PID=""
PLAYER1_PID=""
PLAYER2_PID=""

cleanup() {
  echo ""
  echo "=== Cleaning up ==="
  for pid in $PUBLISH_PID $PLAYER1_PID $PLAYER2_PID $EDGE_PID $ORIGIN_PID $PROXY_PID; do
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
    fi
  done
  sleep 1
  for pid in $PUBLISH_PID $PLAYER1_PID $PLAYER2_PID $EDGE_PID $ORIGIN_PID $PROXY_PID; do
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
      kill -9 "$pid" 2>/dev/null || true
    fi
  done
  echo "Cleanup done."
}
trap cleanup EXIT

wait_for_http() {
  local url=$1
  local name=$2
  local i

  for i in $(seq 1 30); do
    if curl -fsS --max-time 2 "$url" >/dev/null 2>&1; then
      echo "$name is ready."
      return 0
    fi
    sleep 1
  done

  echo "Error: $name is not ready after 30s: $url" >&2
  return 1
}

api_has_stream() {
  local api_port=$1
  local stream=$2

  curl -fsS --max-time 3 "http://127.0.0.1:$api_port/api/v1/streams/" 2>/dev/null | grep -q "$stream"
}

verify_probe_has_av() {
  local url=$1
  local label=$2
  local probe_output

  probe_output=$(ffprobe -v error -rw_timeout 5000000 -show_streams "$url" 2>&1 || true)

  if ! echo "$probe_output" | grep -q "codec_type=video"; then
    echo "FAIL: No video stream detected for $label." >&2
    echo "ffprobe output:" >&2
    echo "$probe_output" >&2
    exit 1
  fi

  if ! echo "$probe_output" | grep -q "codec_type=audio"; then
    echo "FAIL: No audio stream detected for $label." >&2
    echo "ffprobe output:" >&2
    echo "$probe_output" >&2
    exit 1
  fi

  echo "PASS: Audio/video detected for $label."
}

echo "=== E2E Proxy + Edge + Origin Test ==="
echo "Workspace: $WORKSPACE"
echo "Stream:    $STREAM_PATH"
echo ""

# --- Pre-checks ---
if [[ ! -f "$SOURCE_FLV" ]]; then
  echo "Error: test source not found: $SOURCE_FLV" >&2
  exit 1
fi
if [[ ! -f "$ORIGIN_CONF" ]]; then
  echo "Error: origin conf not found: $ORIGIN_CONF" >&2
  exit 1
fi
if [[ ! -f "$EDGE_CONF" ]]; then
  echo "Error: edge conf not found: $EDGE_CONF" >&2
  exit 1
fi
for tool in ffmpeg ffprobe curl; do
  if ! command -v "$tool" &>/dev/null; then
    echo "Error: $tool not found in PATH" >&2
    exit 1
  fi
done

# --- Step 0: Clean up stale state ---
rm -f "$WORKSPACE/trunk/objs/origin-for-edge.pid" "$WORKSPACE/trunk/objs/edge-for-proxy.pid"
ALL_PORTS="$PROXY_RTMP_PORT $PROXY_HTTP_API_PORT $PROXY_HTTP_SERVER_PORT $PROXY_WEBRTC_PORT $PROXY_SRT_PORT $PROXY_SYSTEM_API_PORT"
ALL_PORTS="$ALL_PORTS $ORIGIN_RTMP_PORT $ORIGIN_API_PORT $EDGE_RTMP_PORT $EDGE_HTTP_PORT $EDGE_API_PORT"
for port in $ALL_PORTS; do
  lsof -ti :"$port" 2>/dev/null | xargs kill 2>/dev/null || true
done
sleep 1

# --- Step 1: Build proxy ---
echo "=== Step 1: Building proxy ==="
cd "$WORKSPACE"
make -s 2>&1
echo "Proxy built: $WORKSPACE/bin/srs-proxy"

# --- Step 2: Build SRS (if not already built) ---
if [[ ! -f "$SRS_BINARY" ]]; then
  echo "=== Step 2: Building SRS ==="
  cd "$WORKSPACE/trunk"
  ./configure && make 2>&1 | tail -3
  echo "SRS built: $SRS_BINARY"
else
  echo "=== Step 2: SRS already built ==="
fi

# --- Step 3: Start proxy ---
echo "=== Step 3: Starting proxy (RTMP :$PROXY_RTMP_PORT, System API :$PROXY_SYSTEM_API_PORT) ==="
cd "$WORKSPACE"
env PROXY_RTMP_SERVER=$PROXY_RTMP_PORT \
    PROXY_HTTP_API=$PROXY_HTTP_API_PORT \
    PROXY_HTTP_SERVER=$PROXY_HTTP_SERVER_PORT \
    PROXY_WEBRTC_SERVER=$PROXY_WEBRTC_PORT \
    PROXY_SRT_SERVER=$PROXY_SRT_PORT \
    PROXY_SYSTEM_API=$PROXY_SYSTEM_API_PORT \
    PROXY_LOAD_BALANCER_TYPE=memory \
    ./bin/srs-proxy >/tmp/srs-proxy-edge-e2e.log 2>&1 &
PROXY_PID=$!
echo "Proxy PID: $PROXY_PID"

wait_for_http "http://127.0.0.1:$PROXY_SYSTEM_API_PORT/api/v1/versions" "Proxy System API"

if ! kill -0 "$PROXY_PID" 2>/dev/null; then
  echo "Error: proxy failed to start. Logs:" >&2
  cat /tmp/srs-proxy-edge-e2e.log >&2
  exit 1
fi
echo "Proxy started."

# --- Step 4: Start upstream origin (no proxy heartbeat) ---
echo "=== Step 4: Starting upstream SRS origin (RTMP :$ORIGIN_RTMP_PORT) ==="
ulimit -n 10000 2>/dev/null || true
cd "$WORKSPACE/trunk"
./objs/srs -c conf/origin-for-edge.conf >/tmp/srs-origin-edge-e2e.log 2>&1 &
ORIGIN_PID=$!
echo "Origin PID: $ORIGIN_PID"

wait_for_http "http://127.0.0.1:$ORIGIN_API_PORT/api/v1/versions" "Origin HTTP API"

if ! kill -0 "$ORIGIN_PID" 2>/dev/null; then
  echo "Error: origin failed to start. Logs:" >&2
  cat /tmp/srs-origin-edge-e2e.log >&2
  exit 1
fi
echo "Origin started."

# --- Step 5: Start edge (mode remote, registered with proxy) ---
echo "=== Step 5: Starting SRS edge (RTMP :$EDGE_RTMP_PORT, upstream :$ORIGIN_RTMP_PORT) ==="
./objs/srs -c conf/edge-for-proxy.conf >/tmp/srs-edge-e2e.log 2>&1 &
EDGE_PID=$!
echo "Edge PID: $EDGE_PID"

wait_for_http "http://127.0.0.1:$EDGE_API_PORT/api/v1/versions" "Edge HTTP API"

# Wait for the edge to register with the proxy (heartbeat interval is 9s).
echo "Waiting for edge to register with proxy (up to 20s)..."
for i in $(seq 1 20); do
  if grep -q "Register SRS media server" /tmp/srs-proxy-edge-e2e.log 2>/dev/null; then
    echo "Edge registered with proxy."
    break
  fi
  sleep 1
done

if ! grep -q "Register SRS media server" /tmp/srs-proxy-edge-e2e.log 2>/dev/null; then
  echo "Error: edge did not register with proxy after 20s. Proxy logs:" >&2
  cat /tmp/srs-proxy-edge-e2e.log >&2
  exit 1
fi

if ! kill -0 "$EDGE_PID" 2>/dev/null; then
  echo "Error: edge failed to start. Logs:" >&2
  cat /tmp/srs-edge-e2e.log >&2
  exit 1
fi
echo "Edge started and registered."

# --- Step 6: Publish RTMP stream to proxy ---
# Path: ffmpeg -> proxy (:$PROXY_RTMP_PORT) -> edge (:$EDGE_RTMP_PORT, mode remote forwards
# publish) -> origin (:$ORIGIN_RTMP_PORT). Verify the publish reached the
# upstream origin via the origin's HTTP API.
echo "=== Step 6: Publishing $STREAM_PATH through proxy -> edge -> origin ==="
ffmpeg -stream_loop -1 -re -i "$SOURCE_FLV" -c copy -f flv \
  "rtmp://localhost:$PROXY_RTMP_PORT/$STREAM_PATH" >/tmp/srs-ffmpeg-edge-e2e.log 2>&1 &
PUBLISH_PID=$!
echo "Publisher PID: $PUBLISH_PID"

echo "Waiting for stream to reach origin (up to 15s)..."
reached_origin=0
for i in $(seq 1 15); do
  if api_has_stream "$ORIGIN_API_PORT" "$STREAM_NAME"; then
    reached_origin=1
    echo "Stream visible on origin after ${i}s."
    break
  fi
  sleep 1
done

if [[ $reached_origin -ne 1 ]]; then
  echo "FAIL: stream did not reach upstream origin via edge." >&2
  echo "Publisher logs:" >&2
  cat /tmp/srs-ffmpeg-edge-e2e.log >&2
  echo "Edge logs:" >&2
  cat /tmp/srs-edge-e2e.log >&2
  exit 1
fi

if ! kill -0 "$PUBLISH_PID" 2>/dev/null; then
  echo "Error: publisher exited unexpectedly. Logs:" >&2
  cat /tmp/srs-ffmpeg-edge-e2e.log >&2
  exit 1
fi
echo "PASS: publish path proxy -> edge -> origin works."

# --- Step 7: Two concurrent players on the same stream ---
# Player 1 attaches first and triggers the edge-pull from origin. Player 2
# joins a few seconds later as a late joiner on the already-active edge-pull.
# Both must succeed — this is the proxy-side analogue of the C++ edge late-
# join fix.
echo "=== Step 7: Two concurrent RTMP players via proxy ==="
PLAY_DURATION=8
PLAYER_URL="rtmp://localhost:$PROXY_RTMP_PORT/$STREAM_PATH"

echo "Starting player 1 (immediate)..."
ffmpeg -rw_timeout 5000000 -i "$PLAYER_URL" -t $PLAY_DURATION -c copy -f flv -y /dev/null \
  >/tmp/srs-player1-edge-e2e.log 2>&1 &
PLAYER1_PID=$!

sleep 3

echo "Starting player 2 (late joiner, +3s)..."
ffmpeg -rw_timeout 5000000 -i "$PLAYER_URL" -t $PLAY_DURATION -c copy -f flv -y /dev/null \
  >/tmp/srs-player2-edge-e2e.log 2>&1 &
PLAYER2_PID=$!

# Wait for both players to finish.
player1_rc=0
player2_rc=0
wait "$PLAYER1_PID" || player1_rc=$?
wait "$PLAYER2_PID" || player2_rc=$?
# Clear PIDs so cleanup() doesn't try to re-kill exited processes.
PLAYER1_PID=""
PLAYER2_PID=""

check_player() {
  local label=$1
  local rc=$2
  local log=$3

  if [[ $rc -ne 0 ]]; then
    echo "FAIL: $label exited with code $rc. Logs:" >&2
    cat "$log" >&2
    exit 1
  fi
  # Decoded-frames check — ffmpeg's progress lines contain `frame=` once it has
  # successfully started decoding. Catches "dimensions not set"-style failures
  # where ffmpeg returns 0 but never produced output.
  if ! grep -qE 'frame= *[1-9]' "$log"; then
    echo "FAIL: $label produced no frames. Logs:" >&2
    cat "$log" >&2
    exit 1
  fi
  echo "PASS: $label played successfully."
}

check_player "player 1" "$player1_rc" /tmp/srs-player1-edge-e2e.log
check_player "player 2 (late joiner)" "$player2_rc" /tmp/srs-player2-edge-e2e.log

# --- Step 8: Final probe via proxy confirms A/V is still queryable ---
echo "=== Step 8: Final ffprobe verification via proxy ==="
verify_probe_has_av "rtmp://localhost:$PROXY_RTMP_PORT/$STREAM_PATH" "proxy $STREAM_PATH"

echo ""
echo "=== E2E Proxy + Edge + Origin Test PASSED ==="
