#!/bin/bash
# E2E test for RTMP proxy: starts proxy + SRS origin, publishes an RTMP stream,
# verifies playback via ffprobe, then cleans up all processes.
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

# Ports — use high ports to avoid conflicts with running services.
# The proxy starts ALL servers, so we must assign unique ports for each.
# SRS_TEST_PORT_BASE, by default this script's slot in the Port Plan, moves every port,
# so the scripts can run in parallel.
PORT_BASE=${SRS_TEST_PORT_BASE:-11000}
PROXY_RTMP_PORT=$((PORT_BASE + 0))
PROXY_HTTP_API_PORT=$((PORT_BASE + 1))
PROXY_HTTP_SERVER_PORT=$((PORT_BASE + 2))
PROXY_WEBRTC_PORT=$((PORT_BASE + 3))
PROXY_SRT_PORT=$((PORT_BASE + 4))
PROXY_SYSTEM_API_PORT=$((PORT_BASE + 5))

SOURCE_FLV="$WORKSPACE/trunk/doc/source.flv"
SRS_BINARY="$WORKSPACE/trunk/objs/srs"
source "$SCRIPT_DIR/proxy-e2e-origin.sh"
# Randomize per run so each invocation starts from clean origin state (HLS
# segments, RTMP source, proxy stream registry) and never shares state with
# sibling E2E tests that publish to "live/livestream".
STREAM_URL="live/rtmp$(date +%s)"

# PIDs to clean up on exit.
PROXY_PID=""
ORIGIN_PID=""
FFMPEG_PID=""

cleanup() {
  echo ""
  echo "=== Cleaning up ==="
  for pid in $PROXY_PID $ORIGIN_PID $FFMPEG_PID; do
    if kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
    fi
  done
  sleep 1
  for pid in $PROXY_PID $ORIGIN_PID $FFMPEG_PID; do
    if kill -0 "$pid" 2>/dev/null; then
      kill -9 "$pid" 2>/dev/null || true
    fi
  done
  echo "Cleanup done."
}
trap cleanup EXIT

echo "=== E2E RTMP Proxy Test ==="
echo "Workspace: $WORKSPACE"
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

# Origin ports (from srs_proxy_origin 1 in proxy-e2e-origin.sh).
ORIGIN_RTMP_PORT=$((PORT_BASE + 20))
ORIGIN_HTTP_PORT=$((PORT_BASE + 21))
ORIGIN_API_PORT=$((PORT_BASE + 22))
ORIGIN_RTC_PORT=$((PORT_BASE + 23))
ORIGIN_SRT_PORT=$((PORT_BASE + 24))

# --- Step 0: Clean up stale state ---
# Kill any leftover processes on our ports (proxy + origin).
ALL_PORTS="$PROXY_RTMP_PORT $PROXY_HTTP_API_PORT $PROXY_HTTP_SERVER_PORT $PROXY_WEBRTC_PORT $PROXY_SRT_PORT $PROXY_SYSTEM_API_PORT $ORIGIN_RTMP_PORT $ORIGIN_HTTP_PORT $ORIGIN_API_PORT $ORIGIN_RTC_PORT $ORIGIN_SRT_PORT"
for port in $ALL_PORTS; do
  # Kill only listeners: a client of the port, such as another test's player, is not ours.
  lsof -ti TCP:"$port" -sTCP:LISTEN 2>/dev/null | xargs kill 2>/dev/null || true
  lsof -nP -iUDP:"$port" 2>/dev/null | awk -v p=":$port" 'NR > 1 && $9 !~ /->/ && $9 ~ p "$" {print $2}' | xargs kill 2>/dev/null || true
done
sleep 1

# --- Step 1: Build proxy ---
echo "=== Step 1: Building proxy ==="
cd "$WORKSPACE"
make -s 2>&1
echo "Proxy built: $WORKSPACE/srs-proxy"

# --- Step 2: Build SRS origin (if not already built) ---
if [[ ! -f "$SRS_BINARY" ]]; then
  echo "=== Step 2: Building SRS origin ==="
  cd "$WORKSPACE/trunk"
  ./configure --simulator=on && make 2>&1 | tail -3
  echo "SRS origin built: $SRS_BINARY"
else
  echo "=== Step 2: SRS origin already built ==="
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
    ./bin/srs-proxy >/tmp/srs-proxy-e2e.log 2>&1 &
PROXY_PID=$!
echo "Proxy PID: $PROXY_PID"
sleep 1

if ! kill -0 "$PROXY_PID" 2>/dev/null; then
  echo "Error: proxy failed to start. Logs:" >&2
  cat /tmp/srs-proxy-e2e.log >&2
  exit 1
fi
echo "Proxy started."

# --- Step 4: Start SRS origin ---
echo "=== Step 4: Starting SRS origin ==="
ulimit -n 10000 2>/dev/null || true
cd "$WORKSPACE/trunk"
srs_proxy_origin 1 >/tmp/srs-origin-e2e.log 2>&1 &
ORIGIN_PID=$!
echo "SRS origin PID: $ORIGIN_PID"

# Wait for SRS to register with proxy: the first heartbeat is sent at startup, then every 9s.
echo "Waiting for SRS origin to register with proxy (up to 15s)..."
for i in $(seq 1 15); do
  if grep -q "Register SRS media server" /tmp/srs-proxy-e2e.log 2>/dev/null; then
    break
  fi
  sleep 1
done

if ! grep -q "Register SRS media server" /tmp/srs-proxy-e2e.log 2>/dev/null; then
  echo "Error: SRS origin did not register with proxy after 15s. Proxy logs:" >&2
  cat /tmp/srs-proxy-e2e.log >&2
  exit 1
fi

if ! kill -0 "$ORIGIN_PID" 2>/dev/null; then
  echo "Error: SRS origin failed to start. Logs:" >&2
  cat /tmp/srs-origin-e2e.log >&2
  exit 1
fi
echo "SRS origin started and registered."

# --- Step 5: Publish RTMP stream ---
echo "=== Step 5: Publishing RTMP stream to proxy ==="
ffmpeg -stream_loop -1 -re -i "$SOURCE_FLV" -c copy -f flv \
  "rtmp://localhost:$PROXY_RTMP_PORT/$STREAM_URL" >/tmp/srs-ffmpeg-e2e.log 2>&1 &
FFMPEG_PID=$!
echo "FFmpeg publisher PID: $FFMPEG_PID"

# Wait for stream to stabilize.
sleep 5

if ! kill -0 "$FFMPEG_PID" 2>/dev/null; then
  echo "Error: FFmpeg publisher failed. Logs:" >&2
  cat /tmp/srs-ffmpeg-e2e.log >&2
  exit 1
fi
echo "Stream publishing."

# --- Step 6: Verify RTMP playback ---
echo "=== Step 6: Verifying RTMP playback via proxy ==="
PROBE_OUTPUT=$(ffprobe -v error -show_streams \
  "rtmp://localhost:$PROXY_RTMP_PORT/$STREAM_URL" 2>&1 || true)

if echo "$PROBE_OUTPUT" | grep -q "codec_type=video"; then
  echo "PASS: Video stream detected."
else
  echo "FAIL: No video stream detected." >&2
  echo "ffprobe output:" >&2
  echo "$PROBE_OUTPUT" >&2
  exit 1
fi

if echo "$PROBE_OUTPUT" | grep -q "codec_type=audio"; then
  echo "PASS: Audio stream detected."
else
  echo "FAIL: No audio stream detected." >&2
  echo "ffprobe output:" >&2
  echo "$PROBE_OUTPUT" >&2
  exit 1
fi

echo ""
echo "=== E2E RTMP Proxy Test PASSED ==="
