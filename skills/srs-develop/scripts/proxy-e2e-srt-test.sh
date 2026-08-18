#!/bin/bash
# E2E test for SRT proxy: starts proxy + SRS origin, publishes an SRT stream
# through the proxy, then verifies playback through the proxy in every form
# the origin can transmux from SRT (srt_to_rtmp + rtmp_to_rtc):
#   - SRT play (passthrough)
#   - RTMP play (via srt_to_rtmp on origin)
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

# Proxy ports — same layout as proxy-e2e-test.sh / proxy-e2e-transmux-test.sh.
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
# Randomize per run so each invocation starts from clean origin state (HLS
# segments, RTMP source, proxy stream registry) and never shares state with
# sibling E2E tests that publish to "live/livestream".
STREAM_URL="live/srt$(date +%s)"

# SRT streamid format used by SRS: "#!::r=<app>/<stream>,m=publish|request".
# @see skills/internal-docs-for-srs/references/cpp-docs/doc/srt.md and internal/proxy/srt.go.
SRT_PUBLISH_URL="srt://localhost:$PROXY_SRT_PORT?streamid=#!::r=$STREAM_URL,m=publish"
SRT_PLAY_URL="srt://localhost:$PROXY_SRT_PORT?streamid=#!::r=$STREAM_URL,m=request"

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

echo "=== E2E SRT Proxy Test ==="
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

# SRT URLs need libsrt compiled into ffmpeg/ffprobe. The default Homebrew
# ffmpeg formula does NOT include libsrt. Resolution order:
#   1. Use ffmpeg/ffprobe from PATH if they support SRT.
#   2. Otherwise, use ~/.local/bin/ffmpeg/ffprobe if previously built there.
#   3. Otherwise, build from source via setup-ffmpeg-with-whip.sh (installs
#      into ~/.local/) and use the freshly built binaries.
ffmpeg_has_srt() {
  local bin="$1"
  [[ -x "$bin" ]] && "$bin" -hide_banner -protocols 2>/dev/null | grep -qw srt
}

resolve_ffmpeg() {
  local sys_ffmpeg sys_ffprobe local_ffmpeg local_ffprobe
  sys_ffmpeg="$(command -v ffmpeg || true)"
  sys_ffprobe="$(command -v ffprobe || true)"
  local_ffmpeg="$HOME/.local/bin/ffmpeg"
  local_ffprobe="$HOME/.local/bin/ffprobe"

  if [[ -n "$sys_ffprobe" ]] && ffmpeg_has_srt "$sys_ffmpeg"; then
    FFMPEG_BIN="$sys_ffmpeg"
    FFPROBE_BIN="$sys_ffprobe"
    return 0
  fi
  if [[ -x "$local_ffprobe" ]] && ffmpeg_has_srt "$local_ffmpeg"; then
    FFMPEG_BIN="$local_ffmpeg"
    FFPROBE_BIN="$local_ffprobe"
    return 0
  fi
  return 1
}

if ! resolve_ffmpeg; then
  echo "No ffmpeg with SRT support found on PATH or in ~/.local/bin."
  echo "Building ffmpeg from source via setup-ffmpeg-with-whip.sh — this can take several minutes."
  bash "$SCRIPT_DIR/setup-ffmpeg-with-whip.sh"
  FFMPEG_BIN="$HOME/.local/bin/ffmpeg"
  FFPROBE_BIN="$HOME/.local/bin/ffprobe"
  if ! ffmpeg_has_srt "$FFMPEG_BIN"; then
    echo "Error: ffmpeg still lacks SRT support after running setup-ffmpeg-with-whip.sh." >&2
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
echo "=== Step 3: Starting proxy (SRT :$PROXY_SRT_PORT, System API :$PROXY_SYSTEM_API_PORT) ==="
cd "$WORKSPACE"
env PROXY_RTMP_SERVER=$PROXY_RTMP_PORT \
    PROXY_HTTP_API=$PROXY_HTTP_API_PORT \
    PROXY_HTTP_SERVER=$PROXY_HTTP_SERVER_PORT \
    PROXY_WEBRTC_SERVER=$PROXY_WEBRTC_PORT \
    PROXY_SRT_SERVER=$PROXY_SRT_PORT \
    PROXY_SYSTEM_API=$PROXY_SYSTEM_API_PORT \
    PROXY_LOAD_BALANCER_TYPE=memory \
    ./bin/srs-proxy >/tmp/srs-proxy-srt-e2e.log 2>&1 &
PROXY_PID=$!
echo "Proxy PID: $PROXY_PID"
sleep 1

if ! kill -0 "$PROXY_PID" 2>/dev/null; then
  echo "Error: proxy failed to start. Logs:" >&2
  cat /tmp/srs-proxy-srt-e2e.log >&2
  exit 1
fi
echo "Proxy started."

# --- Step 4: Start SRS origin ---
echo "=== Step 4: Starting SRS origin ==="
ulimit -n 10000 2>/dev/null || true
cd "$WORKSPACE/trunk"
./objs/srs -c conf/origin1-for-proxy.conf >/tmp/srs-origin-srt-e2e.log 2>&1 &
ORIGIN_PID=$!
echo "SRS origin PID: $ORIGIN_PID"

# Wait for SRS to start and register with proxy (heartbeat interval is 9s).
echo "Waiting for SRS origin to register with proxy (up to 15s)..."
sleep 12

if ! kill -0 "$ORIGIN_PID" 2>/dev/null; then
  echo "Error: SRS origin failed to start. Logs:" >&2
  cat /tmp/srs-origin-srt-e2e.log >&2
  exit 1
fi
echo "SRS origin started and registered."

# --- Step 5: Publish SRT stream ---
echo "=== Step 5: Publishing SRT stream to proxy ==="
echo "Publish URL: $SRT_PUBLISH_URL"
"$FFMPEG_BIN" -stream_loop -1 -re -i "$SOURCE_FLV" -c copy -f mpegts \
  "$SRT_PUBLISH_URL" >/tmp/srs-ffmpeg-srt-e2e.log 2>&1 &
FFMPEG_PID=$!
echo "FFmpeg publisher PID: $FFMPEG_PID"

# Wait for the SRT handshake, the proxy<->backend bridge, and the
# origin's srt_to_rtmp pipeline to spin up the derived streams.
sleep 5

if ! kill -0 "$FFMPEG_PID" 2>/dev/null; then
  echo "Error: FFmpeg publisher failed. Logs:" >&2
  cat /tmp/srs-ffmpeg-srt-e2e.log >&2
  exit 1
fi
echo "Stream publishing."

# --- Step 6: Verify SRT playback (passthrough) ---
echo "=== Step 6: Verifying SRT playback via proxy ==="
probe_has_audio_video "SRT" "$SRT_PLAY_URL"

# --- Step 7: Verify RTMP playback (srt_to_rtmp) ---
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

# --- Step 10: WebRTC WHEP playback (placeholder) ---
echo "=== Step 10: WebRTC WHEP playback (placeholder) ==="
echo "SKIP: WebRTC WHEP playback is not verified by this script."
echo "      The origin has rtmp_to_rtc enabled, so SRT->RTMP->RTC should work end-to-end,"
echo "      but actual playback verification is intentionally left as a TODO here."

echo ""
echo "=== E2E SRT Proxy Test PASSED ==="
