#!/bin/bash
# E2E test for RTMP proxy: starts proxy + SRS origin, publishes an RTMP stream,
# verifies playback via ffprobe, then cleans up all processes.
set -e

SCRIPT_DIR="$(cd -P "$(dirname "$0")" && pwd)"
# Navigate: scripts/ -> srs-develop/ -> skills/ -> .openclaw/ -> srs
WORKSPACE="$(cd -P "$SCRIPT_DIR/../../../.." && pwd)"

if [[ ! -f "$WORKSPACE/go.mod" ]]; then
  echo "Error: go.mod not found in WORKSPACE: $WORKSPACE" >&2
  exit 1
fi

# Ports — use high ports to avoid conflicts with running services.
# The proxy starts ALL servers, so we must assign unique ports for each.
PROXY_RTMP_PORT=11935
PROXY_HTTP_API_PORT=11985
PROXY_HTTP_SERVER_PORT=18080
PROXY_WEBRTC_PORT=18000
PROXY_SRT_PORT=20080
PROXY_SYSTEM_API_PORT=12025

SOURCE_FLV="$WORKSPACE/trunk/doc/source.flv"
SRS_BINARY="$WORKSPACE/trunk/objs/srs"
ORIGIN_CONF="$WORKSPACE/trunk/conf/origin1-for-proxy.conf"

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

# Origin ports (from origin1-for-proxy.conf).
ORIGIN_RTMP_PORT=19351
ORIGIN_HTTP_PORT=8081
ORIGIN_API_PORT=19851
ORIGIN_RTC_PORT=8001
ORIGIN_SRT_PORT=10081

# --- Step 0: Clean up stale state ---
# Remove stale SRS PID file that prevents restart.
rm -f "$WORKSPACE/trunk/objs/origin1.pid"
# Kill any leftover processes on our ports (proxy + origin).
ALL_PORTS="$PROXY_RTMP_PORT $PROXY_HTTP_API_PORT $PROXY_HTTP_SERVER_PORT $PROXY_WEBRTC_PORT $PROXY_SRT_PORT $PROXY_SYSTEM_API_PORT $ORIGIN_RTMP_PORT $ORIGIN_HTTP_PORT $ORIGIN_API_PORT $ORIGIN_RTC_PORT $ORIGIN_SRT_PORT"
for port in $ALL_PORTS; do
  lsof -ti :"$port" 2>/dev/null | xargs kill 2>/dev/null || true
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
  ./configure && make 2>&1 | tail -3
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
    ./srs-proxy >/tmp/srs-proxy-e2e.log 2>&1 &
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
./objs/srs -c conf/origin1-for-proxy.conf >/tmp/srs-origin-e2e.log 2>&1 &
ORIGIN_PID=$!
echo "SRS origin PID: $ORIGIN_PID"

# Wait for SRS to start and register with proxy (heartbeat interval is 9s).
echo "Waiting for SRS origin to register with proxy (up to 15s)..."
sleep 12

if ! kill -0 "$ORIGIN_PID" 2>/dev/null; then
  echo "Error: SRS origin failed to start. Logs:" >&2
  cat /tmp/srs-origin-e2e.log >&2
  exit 1
fi
echo "SRS origin started and registered."

# --- Step 5: Publish RTMP stream ---
echo "=== Step 5: Publishing RTMP stream to proxy ==="
ffmpeg -stream_loop -1 -re -i "$SOURCE_FLV" -c copy -f flv \
  "rtmp://localhost:$PROXY_RTMP_PORT/live/livestream" >/tmp/srs-ffmpeg-e2e.log 2>&1 &
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
  "rtmp://localhost:$PROXY_RTMP_PORT/live/livestream" 2>&1 || true)

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
