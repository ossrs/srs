#!/bin/bash
# E2E test for RTMP-to-multiple-protocol transmuxing through the proxy:
# starts one proxy with memory load balancer + one SRS origin, publishes one
# RTMP stream, then verifies RTMP, HTTP-FLV, HLS, and WebRTC WHEP playback
# through the proxy. WHEP is played with tools/pion-whep.
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

# Ports — use the same high ports as proxy-e2e-test.sh.
# The proxy starts ALL servers, so we must assign unique ports for each.
PROXY_RTMP_PORT=11935
PROXY_HTTP_API_PORT=11985
PROXY_HTTP_SERVER_PORT=18080
PROXY_WEBRTC_PORT=18000
PROXY_SRT_PORT=20080
PROXY_SYSTEM_API_PORT=12025

# Origin ports (from srs_proxy_origin 1 in proxy-e2e-origin.sh).
ORIGIN_RTMP_PORT=19351
ORIGIN_HTTP_PORT=8081
ORIGIN_API_PORT=19851
ORIGIN_RTC_PORT=8001
ORIGIN_SRT_PORT=10081

SOURCE_FLV="$WORKSPACE/trunk/doc/source.flv"
SRS_BINARY="$WORKSPACE/trunk/objs/srs"
source "$SCRIPT_DIR/proxy-e2e-origin.sh"
# Randomize per run so each invocation starts from clean origin state (HLS
# segments, RTMP source, proxy stream registry) and never shares state with
# sibling E2E tests that publish to "live/livestream".
STREAM_URL="live/transmux$(date +%s)"

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

# Play over WHEP with tools/pion-whep, because FFmpeg has no WHEP demuxer, and
# require both video and audio RTP packets. The tool is rebuilt when its binary
# is missing or older than its sources.
verify_whep_playback() {
  local url="$1"
  local log="$2"
  local tool_dir="$WORKSPACE/tools/pion-whep"
  local tool_bin="$tool_dir/objs/pion-whep"
  local summary video audio

  if [[ ! -x "$tool_bin" ]] || [[ -n "$(find "$tool_dir" -maxdepth 1 \( -name '*.go' -o -name 'go.mod' -o -name 'go.sum' \) -newer "$tool_bin")" ]]; then
    echo "Building pion-whep: $tool_bin"
    (cd "$tool_dir" && mkdir -p objs && go build -o objs/pion-whep .)
  fi

  echo "Verifying WHEP playback: $url"
  if ! "$tool_bin" -hide_banner -f whep -i "$url" -t 5 -f null - >"$log" 2>&1; then
    echo "FAIL: WHEP playback failed. pion-whep log:" >&2
    cat "$log" >&2
    exit 1
  fi

  summary="$(grep -a "^Received video=" "$log" | tail -1)"
  video="$(echo "$summary" | sed -n 's/^Received video=\([0-9]*\),.*/\1/p')"
  audio="$(echo "$summary" | sed -n 's/^Received video=[0-9]*, audio=\([0-9]*\) .*/\1/p')"
  echo "pion-whep: $summary"

  if [[ -n "$video" && "$video" -gt 0 ]]; then
    echo "PASS: WHEP video packets received."
  else
    echo "FAIL: WHEP no video packets received." >&2
    cat "$log" >&2
    exit 1
  fi

  if [[ -n "$audio" && "$audio" -gt 0 ]]; then
    echo "PASS: WHEP audio packets received."
  else
    echo "FAIL: WHEP no audio packets received." >&2
    cat "$log" >&2
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
if ! command -v go &>/dev/null; then
  echo "Error: go not found in PATH" >&2
  exit 1
fi

# --- Step 0: Clean up stale state ---
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
  ./configure --simulator=on && make 2>&1 | tail -3
  echo "SRS origin built: $SRS_BINARY"
else
  echo "=== Step 2: SRS origin already built ==="
fi

# --- Step 3: Start proxy ---
echo "=== Step 3: Starting proxy (memory LB) ==="
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

# --- Step 4: Start SRS origin ---
echo "=== Step 4: Starting SRS origin ==="
ulimit -n 10000 2>/dev/null || true
cd "$WORKSPACE/trunk"
# The proxy rewrites only the port of the WebRTC candidate, so the origin must
# advertise an IP the WHEP player can reach.
srs_proxy_origin 1 SRS_RTC_SERVER_CANDIDATE=127.0.0.1 >/tmp/srs-origin-transmux-e2e.log 2>&1 &
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

# --- Step 5: Publish RTMP stream ---
echo "=== Step 5: Publishing RTMP stream to proxy ==="
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

# --- Step 6: Verify RTMP playback ---
echo "=== Step 6: Verifying RTMP playback via proxy ==="
probe_has_audio_video "RTMP" "rtmp://localhost:$PROXY_RTMP_PORT/$STREAM_URL"

# --- Step 7: Verify HTTP-FLV playback ---
echo "=== Step 7: Verifying HTTP-FLV playback via proxy ==="
probe_has_audio_video "HTTP-FLV" "http://localhost:$PROXY_HTTP_SERVER_PORT/$STREAM_URL.flv"

# --- Step 8: Verify HLS playback ---
echo "=== Step 8: Verifying HLS playback via proxy ==="
HLS_URL="http://localhost:$PROXY_HTTP_SERVER_PORT/$STREAM_URL.m3u8"
wait_for_hls_playlist "$HLS_URL"
probe_has_audio_video "HLS" "$HLS_URL"

# --- Step 9: Verify WebRTC WHEP playback (rtmp_to_rtc) ---
echo "=== Step 9: Verifying WebRTC WHEP playback via proxy ==="
verify_whep_playback "http://localhost:$PROXY_HTTP_API_PORT/rtc/v1/whep/?app=live&stream=${STREAM_URL#live/}" \
  /tmp/srs-pion-whep-transmux-e2e.log

echo ""
echo "NOTE: RTSP is not tested here because the Go proxy currently has no RTSP listener."
echo "=== E2E RTMP Transmux Proxy Test PASSED ==="
