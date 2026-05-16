#!/bin/bash
# E2E test for WHIP proxy: starts proxy + SRS origin, publishes a WebRTC
# stream via WHIP through the proxy, then verifies playback through the proxy
# in every form the origin can transmux from WebRTC (rtc_to_rtmp + http_remux
# + hls):
#   - RTMP play (via rtc_to_rtmp on origin)
#   - HTTP-FLV (HTTP remux of the bridged RTMP)
#   - HLS (m3u8 + TS segments)
#   - WebRTC WHEP (placeholder only, not actually verified here)
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

# Proxy ports — same layout as proxy-e2e-srt-test.sh / proxy-e2e-transmux-test.sh.
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
# Randomize the stream name per run so each test starts from a clean origin
# state (HLS segments, RTMP source, proxy stream registry) and never shares
# state with sibling E2E tests that publish to "live/livestream".
STREAM_NAME="whip$(date +%s)"
STREAM_URL="live/$STREAM_NAME"

# WHIP endpoint exposed by the proxy. The proxy parses ?app=&stream= via
# utils.ConvertURLToStreamURL, then forwards the SDP exchange to the backend
# SRS origin. @see internal/proxy/api.go and internal/proxy/rtc.go.
WHIP_PUBLISH_URL="http://localhost:$PROXY_HTTP_API_PORT/rtc/v1/whip/?app=live&stream=$STREAM_NAME"

# Make the SRS origin advertise a host candidate that loops back through the
# proxy. The proxy rewrites only the port in the SDP answer (origin RTC port
# -> proxy WebRTC port), so the candidate IP must already be reachable for
# the publisher; 127.0.0.1 works for all-local E2E.
ORIGIN_CANDIDATE="127.0.0.1"

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
  output=$("$FFPROBE_BIN" -v error -show_streams "$url" 2>&1 || true)

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
  local deadline=60

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

first_hls_segment() {
  local url="$1"

  curl -fsS "$url" 2>/dev/null | awk '
    /^[[:space:]]*$/ { next }
    /^#/ { next }
    { print; exit }
  '
}

wait_for_hls_to_skip_first_segment() {
  local url="$1"
  local deadline=60
  local first_segment current_segment output

  first_segment="$(first_hls_segment "$url")"
  if [[ -z "$first_segment" ]]; then
    echo "FAIL: HLS playlist has no media segment: $url" >&2
    curl -fsS "$url" 2>&1 || true
    exit 1
  fi

  echo "Waiting for HLS to skip the first possibly incomplete segment (up to ${deadline}s): $first_segment"
  for ((i = 1; i <= deadline; i++)); do
    current_segment="$(first_hls_segment "$url")"
    if [[ -n "$current_segment" && "$current_segment" != "$first_segment" ]]; then
      output=$("$FFPROBE_BIN" -v error -show_streams "$url" 2>&1 || true)
      if echo "$output" | grep -q "codec_type=video" && echo "$output" | grep -q "codec_type=audio"; then
        echo "HLS first segment advanced and audio/video is ready: $current_segment"
        return
      fi
    fi
    sleep 1
  done

  echo "FAIL: HLS did not skip the first segment and expose audio/video in ${deadline}s." >&2
  echo "Last HLS response:" >&2
  curl -fsS "$url" 2>&1 || true
  echo "Last ffprobe output:" >&2
  echo "$output" >&2
  exit 1
}

echo "=== E2E WHIP Proxy Test ==="
echo "Workspace: $WORKSPACE"
echo "Stream:    $STREAM_URL"
echo ""

# --- Pre-checks ---
if [[ ! -f "$SOURCE_FLV" ]]; then
  echo "Error: test source not found: $SOURCE_FLV" >&2
  exit 1
fi
if ! command -v curl &>/dev/null; then
  echo "Error: curl not found in PATH" >&2
  exit 1
fi

# WHIP needs an ffmpeg with the `whip` muxer (added in ffmpeg 7.1, requires
# --enable-openssl at build time for DTLS-SRTP). Neither vanilla brew nor the
# homebrew-ffmpeg tap enable it. Resolution order:
#   1. Use ffmpeg/ffprobe from PATH if they include the whip muxer.
#   2. Otherwise, use ~/.local/bin/ffmpeg/ffprobe if previously built there.
#   3. Otherwise, build from source via setup-ffmpeg-with-whip.sh (installs
#      into ~/.local/) and use the freshly built binaries.
ffmpeg_has_whip() {
  local bin="$1"
  [[ -x "$bin" ]] && "$bin" -hide_banner -muxers 2>/dev/null | grep -qw whip
}

resolve_ffmpeg() {
  local sys_ffmpeg sys_ffprobe local_ffmpeg local_ffprobe
  sys_ffmpeg="$(command -v ffmpeg || true)"
  sys_ffprobe="$(command -v ffprobe || true)"
  local_ffmpeg="$HOME/.local/bin/ffmpeg"
  local_ffprobe="$HOME/.local/bin/ffprobe"

  if [[ -n "$sys_ffprobe" ]] && ffmpeg_has_whip "$sys_ffmpeg"; then
    FFMPEG_BIN="$sys_ffmpeg"
    FFPROBE_BIN="$sys_ffprobe"
    return 0
  fi
  if [[ -x "$local_ffprobe" ]] && ffmpeg_has_whip "$local_ffmpeg"; then
    FFMPEG_BIN="$local_ffmpeg"
    FFPROBE_BIN="$local_ffprobe"
    return 0
  fi
  return 1
}

if ! resolve_ffmpeg; then
  echo "No ffmpeg with WHIP muxer found on PATH or in ~/.local/bin."
  echo "Building ffmpeg from source via setup-ffmpeg-with-whip.sh — this can take several minutes."
  bash "$SCRIPT_DIR/setup-ffmpeg-with-whip.sh"
  FFMPEG_BIN="$HOME/.local/bin/ffmpeg"
  FFPROBE_BIN="$HOME/.local/bin/ffprobe"
  if ! ffmpeg_has_whip "$FFMPEG_BIN"; then
    echo "Error: ffmpeg still lacks WHIP muxer after running setup-ffmpeg-with-whip.sh." >&2
    exit 1
  fi
  if [[ ! -x "$FFPROBE_BIN" ]]; then
    echo "Error: ffprobe missing at $FFPROBE_BIN after running setup-ffmpeg-with-whip.sh." >&2
    exit 1
  fi
fi
echo "ffmpeg : $FFMPEG_BIN"
echo "ffprobe: $FFPROBE_BIN"

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

# --- Step 3: Start proxy ---
echo "=== Step 3: Starting proxy (HTTP API :$PROXY_HTTP_API_PORT, WebRTC :$PROXY_WEBRTC_PORT) ==="
cd "$WORKSPACE"
env PROXY_RTMP_SERVER=$PROXY_RTMP_PORT \
    PROXY_HTTP_API=$PROXY_HTTP_API_PORT \
    PROXY_HTTP_SERVER=$PROXY_HTTP_SERVER_PORT \
    PROXY_WEBRTC_SERVER=$PROXY_WEBRTC_PORT \
    PROXY_SRT_SERVER=$PROXY_SRT_PORT \
    PROXY_SYSTEM_API=$PROXY_SYSTEM_API_PORT \
    PROXY_LOAD_BALANCER_TYPE=memory \
    ./bin/srs-proxy >/tmp/srs-proxy-whip-e2e.log 2>&1 &
PROXY_PID=$!
echo "Proxy PID: $PROXY_PID"
sleep 1

if ! kill -0 "$PROXY_PID" 2>/dev/null; then
  echo "Error: proxy failed to start. Logs:" >&2
  cat /tmp/srs-proxy-whip-e2e.log >&2
  exit 1
fi
echo "Proxy started."

# --- Step 4: Start SRS origin (with CANDIDATE=$ORIGIN_CANDIDATE for WebRTC) ---
echo "=== Step 4: Starting SRS origin (CANDIDATE=$ORIGIN_CANDIDATE) ==="
ulimit -n 10000 2>/dev/null || true
cd "$WORKSPACE/trunk"
env CANDIDATE="$ORIGIN_CANDIDATE" \
    ./objs/srs -c conf/origin1-for-proxy.conf >/tmp/srs-origin-whip-e2e.log 2>&1 &
ORIGIN_PID=$!
echo "SRS origin PID: $ORIGIN_PID"

# Wait for SRS to start and register with proxy (heartbeat interval is 9s).
echo "Waiting for SRS origin to register with proxy (up to 15s)..."
sleep 12

if ! kill -0 "$ORIGIN_PID" 2>/dev/null; then
  echo "Error: SRS origin failed to start. Logs:" >&2
  cat /tmp/srs-origin-whip-e2e.log >&2
  exit 1
fi
echo "SRS origin started and registered."

# --- Step 5: Publish WHIP stream ---
# WebRTC requires H.264 (baseline-friendly) + Opus. source.flv is H.264 High
# profile + AAC, so transcode video to baseline and audio to Opus. Use
# zerolatency/ultrafast so the encoder keeps up with -re.
echo "=== Step 5: Publishing WHIP stream to proxy ==="
echo "Publish URL: $WHIP_PUBLISH_URL"
"$FFMPEG_BIN" -stream_loop -1 -re -i "$SOURCE_FLV" \
  -c:v libx264 -profile:v baseline -level 3.1 -pix_fmt yuv420p \
  -tune zerolatency -preset ultrafast \
  -c:a libopus -ar 48000 -ac 2 \
  -f whip "$WHIP_PUBLISH_URL" >/tmp/srs-ffmpeg-whip-e2e.log 2>&1 &
FFMPEG_PID=$!
echo "FFmpeg publisher PID: $FFMPEG_PID"

# Wait for WHIP SDP exchange + DTLS-SRTP handshake + the origin's
# rtc_to_rtmp pipeline to spin up the bridged RTMP stream.
sleep 8

if ! kill -0 "$FFMPEG_PID" 2>/dev/null; then
  echo "Error: FFmpeg WHIP publisher failed. Logs:" >&2
  cat /tmp/srs-ffmpeg-whip-e2e.log >&2
  exit 1
fi
echo "Stream publishing."

# --- Step 6: Verify RTMP playback (rtc_to_rtmp) ---
echo "=== Step 6: Verifying RTMP playback via proxy ==="
probe_has_audio_video "RTMP" "rtmp://localhost:$PROXY_RTMP_PORT/$STREAM_URL"

# --- Step 7: Verify HTTP-FLV playback ---
echo "=== Step 7: Verifying HTTP-FLV playback via proxy ==="
probe_has_audio_video "HTTP-FLV" "http://localhost:$PROXY_HTTP_SERVER_PORT/$STREAM_URL.flv"

# --- Step 8: Verify HLS playback ---
echo "=== Step 8: Verifying HLS playback via proxy ==="
HLS_URL="http://localhost:$PROXY_HTTP_SERVER_PORT/$STREAM_URL.m3u8"
wait_for_hls_playlist "$HLS_URL"
wait_for_hls_to_skip_first_segment "$HLS_URL"
probe_has_audio_video "HLS" "$HLS_URL"

# --- Step 9: WebRTC WHEP playback (placeholder) ---
echo "=== Step 9: WebRTC WHEP playback (placeholder) ==="
echo "SKIP: WebRTC WHEP playback is not verified by this script."
echo "      The origin has rtmp_to_rtc enabled, so WHIP->RTMP->RTC should work end-to-end,"
echo "      but actual playback verification is intentionally left as a TODO here."

echo ""
echo "=== E2E WHIP Proxy Test PASSED ==="
