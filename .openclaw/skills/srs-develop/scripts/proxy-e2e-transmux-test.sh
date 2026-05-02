#!/bin/bash
# E2E test for RTMP-to-multiple-protocol transmuxing through the proxy:
# starts one proxy with memory load balancer + one SRS origin, publishes one
# RTMP stream, then verifies RTMP, HTTP-FLV, HLS, and optional WebRTC WHEP
# playback through the proxy.
set -e

SCRIPT_DIR="$(cd -P "$(dirname "$0")" && pwd)"
# Navigate: scripts/ -> srs-develop/ -> skills/ -> .openclaw/ -> srs
WORKSPACE="$(cd -P "$SCRIPT_DIR/../../../.." && pwd)"

if [[ ! -f "$WORKSPACE/go.mod" ]]; then
  echo "Error: go.mod not found in WORKSPACE: $WORKSPACE" >&2
  exit 1
fi

# Ports — use the same high ports as proxy-e2e-test.sh.
# The proxy starts ALL servers, so we must assign unique ports for each.
PROXY_RTMP_PORT=11935
PROXY_HTTP_API_PORT=11985
PROXY_HTTP_SERVER_PORT=18080
PROXY_WEBRTC_PORT=18000
PROXY_SRT_PORT=20080
PROXY_SYSTEM_API_PORT=12025

# Origin ports (from origin1-for-proxy.conf).
ORIGIN_RTMP_PORT=19351
ORIGIN_HTTP_PORT=8081
ORIGIN_API_PORT=19851
ORIGIN_RTC_PORT=8001
ORIGIN_SRT_PORT=10081

SOURCE_FLV="$WORKSPACE/trunk/doc/source.flv"
SRS_BINARY="$WORKSPACE/trunk/objs/srs"
SRS_TEST_BINARY="$WORKSPACE/trunk/3rdparty/srs-bench/objs/srs_test"
STREAM_URL="live/livestream"

# WebRTC requires the srs-bench regression test binary. Keep it enabled by
# default because it exercises the proxy WHEP API path; set to "off" to skip.
PROXY_TRANSMUX_TEST_RTC="${PROXY_TRANSMUX_TEST_RTC:-on}"

# PIDs to clean up on exit.
PROXY_PID=""
ORIGIN_PID=""
FFMPEG_PID=""

cleanup() {
  echo ""
  echo "=== Cleaning up ==="
  for pid in $PROXY_PID $ORIGIN_PID $FFMPEG_PID; do
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
    fi
  done
  sleep 1
  for pid in $PROXY_PID $ORIGIN_PID $FFMPEG_PID; do
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
      kill -9 "$pid" 2>/dev/null || true
    fi
  done
  echo "Cleanup done."
}
trap cleanup EXIT

probe_has_audio_video() {
  local name="$1"
  local url="$2"

  echo "Verifying $name playback: $url"
  local output
  output=$(ffprobe -v error -show_streams "$url" 2>&1 || true)

  if echo "$output" | grep -q "codec_type=video"; then
    echo "PASS: $name video stream detected."
  else
    echo "FAIL: $name no video stream detected." >&2
    echo "ffprobe output:" >&2
    echo "$output" >&2
    exit 1
  fi

  if echo "$output" | grep -q "codec_type=audio"; then
    echo "PASS: $name audio stream detected."
  else
    echo "FAIL: $name no audio stream detected." >&2
    echo "ffprobe output:" >&2
    echo "$output" >&2
    exit 1
  fi
}

wait_for_hls_playlist() {
  local url="$1"
  local deadline=45

  echo "Waiting for HLS playlist to be generated (up to ${deadline}s): $url"
  for ((i = 1; i <= deadline; i++)); do
    if curl -fsS "$url" 2>/dev/null | grep -q "#EXTM3U"; then
      echo "HLS playlist is ready."
      return
    fi
    sleep 1
  done

  echo "FAIL: HLS playlist was not generated in ${deadline}s." >&2
  echo "Last HLS response:" >&2
  curl -v "$url" 2>&1 || true
  exit 1
}

echo "=== E2E RTMP Transmux Proxy Test ==="
echo "Workspace: $WORKSPACE"
echo "Stream: $STREAM_URL"
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
if ! command -v curl &>/dev/null; then
  echo "Error: curl not found in PATH" >&2
  exit 1
fi

# --- Step 0: Clean up stale state ---
rm -f "$WORKSPACE/trunk/objs/origin1.pid"
ALL_PORTS="$PROXY_RTMP_PORT $PROXY_HTTP_API_PORT $PROXY_HTTP_SERVER_PORT $PROXY_WEBRTC_PORT $PROXY_SRT_PORT $PROXY_SYSTEM_API_PORT $ORIGIN_RTMP_PORT $ORIGIN_HTTP_PORT $ORIGIN_API_PORT $ORIGIN_RTC_PORT $ORIGIN_SRT_PORT"
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

# --- Step 3: Build WebRTC regression tool (if enabled and needed) ---
if [[ "$PROXY_TRANSMUX_TEST_RTC" == "on" && ! -x "$SRS_TEST_BINARY" ]]; then
  echo "=== Step 3: Building WebRTC regression tool ==="
  cd "$WORKSPACE/trunk/3rdparty/srs-bench"
  make ./objs/srs_test
  echo "WebRTC regression tool built: $SRS_TEST_BINARY"
else
  echo "=== Step 3: WebRTC regression tool build skipped ==="
fi

# --- Step 4: Start proxy ---
echo "=== Step 4: Starting proxy (memory LB) ==="
cd "$WORKSPACE"
env PROXY_RTMP_SERVER=$PROXY_RTMP_PORT \
    PROXY_HTTP_API=$PROXY_HTTP_API_PORT \
    PROXY_HTTP_SERVER=$PROXY_HTTP_SERVER_PORT \
    PROXY_WEBRTC_SERVER=$PROXY_WEBRTC_PORT \
    PROXY_SRT_SERVER=$PROXY_SRT_PORT \
    PROXY_SYSTEM_API=$PROXY_SYSTEM_API_PORT \
    PROXY_LOAD_BALANCER_TYPE=memory \
    ./bin/srs-proxy >/tmp/srs-proxy-transmux-e2e.log 2>&1 &
PROXY_PID=$!
echo "Proxy PID: $PROXY_PID"
sleep 1

if ! kill -0 "$PROXY_PID" 2>/dev/null; then
  echo "Error: proxy failed to start. Logs:" >&2
  cat /tmp/srs-proxy-transmux-e2e.log >&2
  exit 1
fi
echo "Proxy started."

# --- Step 5: Start SRS origin ---
echo "=== Step 5: Starting SRS origin ==="
ulimit -n 10000 2>/dev/null || true
cd "$WORKSPACE/trunk"
./objs/srs -c conf/origin1-for-proxy.conf >/tmp/srs-origin-transmux-e2e.log 2>&1 &
ORIGIN_PID=$!
echo "SRS origin PID: $ORIGIN_PID"

# Wait for SRS to start and register with proxy (heartbeat interval is 9s).
echo "Waiting for SRS origin to register with proxy (up to 15s)..."
sleep 12

if ! kill -0 "$ORIGIN_PID" 2>/dev/null; then
  echo "Error: SRS origin failed to start. Logs:" >&2
  cat /tmp/srs-origin-transmux-e2e.log >&2
  exit 1
fi
echo "SRS origin started and registered."

# --- Step 6: Publish RTMP stream ---
echo "=== Step 6: Publishing RTMP stream to proxy ==="
ffmpeg -stream_loop -1 -re -i "$SOURCE_FLV" -c copy -f flv \
  "rtmp://localhost:$PROXY_RTMP_PORT/$STREAM_URL" >/tmp/srs-ffmpeg-transmux-e2e.log 2>&1 &
FFMPEG_PID=$!
echo "FFmpeg publisher PID: $FFMPEG_PID"

# Wait for stream to stabilize and for the origin to start muxing HTTP/HLS.
sleep 5

if ! kill -0 "$FFMPEG_PID" 2>/dev/null; then
  echo "Error: FFmpeg publisher failed. Logs:" >&2
  cat /tmp/srs-ffmpeg-transmux-e2e.log >&2
  exit 1
fi
echo "Stream publishing."

# --- Step 7: Verify RTMP playback ---
echo "=== Step 7: Verifying RTMP playback via proxy ==="
probe_has_audio_video "RTMP" "rtmp://localhost:$PROXY_RTMP_PORT/$STREAM_URL"

# --- Step 8: Verify HTTP-FLV playback ---
echo "=== Step 8: Verifying HTTP-FLV playback via proxy ==="
probe_has_audio_video "HTTP-FLV" "http://localhost:$PROXY_HTTP_SERVER_PORT/$STREAM_URL.flv"

# --- Step 9: Verify HLS playback ---
echo "=== Step 9: Verifying HLS playback via proxy ==="
HLS_URL="http://localhost:$PROXY_HTTP_SERVER_PORT/$STREAM_URL.m3u8"
wait_for_hls_playlist "$HLS_URL"
probe_has_audio_video "HLS" "$HLS_URL"

# --- Step 10: Verify WebRTC WHEP signaling via proxy ---
echo "=== Step 10: Verifying WebRTC WHEP signaling via proxy ==="
if [[ "$PROXY_TRANSMUX_TEST_RTC" == "on" ]]; then
  if [[ ! -x "$SRS_TEST_BINARY" ]]; then
    echo "FAIL: WebRTC regression tool not found: $SRS_TEST_BINARY" >&2
    exit 1
  fi

  cd "$WORKSPACE/trunk/3rdparty/srs-bench"
  "$SRS_TEST_BINARY" \
    -test.run '^TestBugfix2371_RTMP2RTC_PlayWithNack$' \
    -srs-server "127.0.0.1:$PROXY_HTTP_API_PORT" \
    -srs-stream "/$STREAM_URL" \
    -srs-timeout 10000
  echo "PASS: WebRTC WHEP signaling succeeded."
else
  echo "SKIP: WebRTC WHEP test disabled by PROXY_TRANSMUX_TEST_RTC=$PROXY_TRANSMUX_TEST_RTC."
fi

echo ""
echo "NOTE: RTSP is not tested here because the Go proxy currently has no RTSP listener."
echo "=== E2E RTMP Transmux Proxy Test PASSED ==="
