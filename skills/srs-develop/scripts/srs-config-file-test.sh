#!/bin/bash
# E2E test for starting SRS from a config file, because the other scripts start
# SRS with environment variables only (-e). Writes a config with non-default
# ports, pid, log and HLS paths, clears every SRS_* variable, then:
#   - publishes RTMP and verifies the HTTP API, RTMP, HTTP-FLV, HLS and WebRTC
#     WHEP (rtmp_to_rtc) playback, and the pid, log and HLS files;
#   - publishes WebRTC over WHIP and verifies WHEP and RTMP (rtc_to_rtmp)
#     playback.
# WHEP is played with tools/pion-whep, because FFmpeg has no WHEP demuxer.
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

# Non-default ports, so a pass proves SRS read them from the config file.
RTMP_PORT=19370
HTTP_API_PORT=19870
HTTP_SERVER_PORT=18070
RTC_PORT=8070

SOURCE_FLV="$WORKSPACE/trunk/doc/source.flv"
SRS_BINARY="$WORKSPACE/trunk/objs/srs"
# Randomize per run so each invocation starts from clean state.
STREAM_NAME="config$(date +%s)"
STREAM_URL="live/$STREAM_NAME"
WHIP_STREAM_NAME="${STREAM_NAME}whip"
WHIP_STREAM_URL="live/$WHIP_STREAM_NAME"
API="http://127.0.0.1:$HTTP_API_PORT"

TEST_DIR=$(mktemp -d "${TMPDIR:-/tmp}/srs-config-file.XXXXXX")
SRS_CONF="$TEST_DIR/srs.conf"
SRS_PID_FILE="$TEST_DIR/srs.pid"
SRS_LOG_FILE="$TEST_DIR/srs.log"
SRS_STDOUT="$TEST_DIR/stdout.log"
HLS_DIR="$TEST_DIR/html"
FFMPEG_LOG="$TEST_DIR/ffmpeg.log"
WHIP_LOG="$TEST_DIR/ffmpeg-whip.log"

# PIDs to clean up on exit.
SRS_PID=""
FFMPEG_PID=""
WHIP_PID=""

cleanup() {
  echo ""
  echo "=== Cleaning up ==="
  for pid in $SRS_PID $FFMPEG_PID $WHIP_PID; do
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
    fi
  done
  sleep 1
  for pid in $SRS_PID $FFMPEG_PID $WHIP_PID; do
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
      kill -9 "$pid" 2>/dev/null || true
    fi
  done
  rm -rf "$TEST_DIR"
  echo "Cleanup done."
}
trap cleanup EXIT

fail() {
  echo "FAIL: $1" >&2
  echo "SRS log:" >&2
  tail -50 "$SRS_LOG_FILE" "$SRS_STDOUT" >&2 2>/dev/null || true
  exit 1
}

probe_has_audio_video() {
  local name="$1"
  local url="$2"

  echo "Verifying $name playback: $url"
  local output
  output=$("$FFPROBE_BIN" -v error -show_streams "$url" 2>&1 || true)

  if echo "$output" | grep -q "codec_type=video"; then
    echo "PASS: $name video stream detected."
  else
    echo "ffprobe output:" >&2
    echo "$output" >&2
    fail "$name no video stream detected."
  fi

  if echo "$output" | grep -q "codec_type=audio"; then
    echo "PASS: $name audio stream detected."
  else
    echo "ffprobe output:" >&2
    echo "$output" >&2
    fail "$name no audio stream detected."
  fi
}

# Play over WHEP with tools/pion-whep and require both video and audio RTP
# packets. The tool is rebuilt when its binary is missing or older than its
# sources.
verify_whep_playback() {
  local name="$1"
  local url="$2"
  local log="$3"
  local tool_dir="$WORKSPACE/tools/pion-whep"
  local tool_bin="$tool_dir/objs/pion-whep"
  local summary video audio

  if [[ ! -x "$tool_bin" ]] || [[ -n "$(find "$tool_dir" -maxdepth 1 \( -name '*.go' -o -name 'go.mod' -o -name 'go.sum' \) -newer "$tool_bin")" ]]; then
    echo "Building pion-whep: $tool_bin"
    (cd "$tool_dir" && mkdir -p objs && go build -o objs/pion-whep .)
  fi

  echo "Verifying $name playback: $url"
  if ! "$tool_bin" -hide_banner -f whep -i "$url" -t 5 -f null - >"$log" 2>&1; then
    cat "$log" >&2
    fail "$name playback failed."
  fi

  summary="$(grep -a "^Received video=" "$log" | tail -1)"
  video="$(echo "$summary" | sed -n 's/^Received video=\([0-9]*\),.*/\1/p')"
  audio="$(echo "$summary" | sed -n 's/^Received video=[0-9]*, audio=\([0-9]*\) .*/\1/p')"
  echo "pion-whep: $summary"

  if [[ -n "$video" && "$video" -gt 0 ]]; then
    echo "PASS: $name video packets received."
  else
    cat "$log" >&2
    fail "$name no video packets received."
  fi

  if [[ -n "$audio" && "$audio" -gt 0 ]]; then
    echo "PASS: $name audio packets received."
  else
    cat "$log" >&2
    fail "$name no audio packets received."
  fi
}

echo "=== E2E SRS Config File Test ==="
echo "Workspace: $WORKSPACE"
echo "Streams: $STREAM_URL (RTMP), $WHIP_STREAM_URL (WHIP)"
echo "Test dir: $TEST_DIR"
echo ""

# --- Pre-checks ---
if [[ ! -f "$SOURCE_FLV" ]]; then
  echo "Error: test source not found: $SOURCE_FLV" >&2
  exit 1
fi
for cmd in curl jq go; do
  if ! command -v "$cmd" &>/dev/null; then
    echo "Error: $cmd not found in PATH" >&2
    exit 1
  fi
done

# WHIP needs an ffmpeg with the `whip` muxer (added in ffmpeg 7.1, requires
# --enable-openssl at build time for DTLS-SRTP). Resolution order:
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
for port in $RTMP_PORT $HTTP_API_PORT $HTTP_SERVER_PORT $RTC_PORT; do
  lsof -ti :"$port" 2>/dev/null | xargs kill 2>/dev/null || true
done
sleep 1

# --- Step 1: Build SRS (if not already built) ---
if [[ ! -f "$SRS_BINARY" ]]; then
  echo "=== Step 1: Building SRS ==="
  cd "$WORKSPACE/trunk"
  ./configure --simulator=on && make 2>&1 | tail -3
  echo "SRS built: $SRS_BINARY"
else
  echo "=== Step 1: SRS already built ==="
fi

# --- Step 2: Write the config file ---
echo "=== Step 2: Writing config file: $SRS_CONF ==="
mkdir -p "$HLS_DIR"
cat >"$SRS_CONF" <<CONF
listen $RTMP_PORT;
pid $SRS_PID_FILE;
max_connections 1000;
daemon off;
srs_log_tank file;
srs_log_file $SRS_LOG_FILE;

http_api {
  enabled on;
  listen $HTTP_API_PORT;
}

http_server {
  enabled on;
  listen $HTTP_SERVER_PORT;
  dir $HLS_DIR;
}

rtc_server {
  enabled on;
  listen $RTC_PORT;
  candidate 127.0.0.1;
}

vhost __defaultVhost__ {
  hls {
    enabled on;
    hls_path $HLS_DIR;
    hls_fragment 2;
    hls_window 10;
  }
  http_remux {
    enabled on;
    mount [vhost]/[app]/[stream].flv;
  }
  rtc {
    enabled on;
    rtmp_to_rtc on;
    rtc_to_rtmp on;
  }
}
CONF

# --- Step 3: Start SRS with the config file ---
echo "=== Step 3: Starting SRS with -c $SRS_CONF ==="
ulimit -n 10000 2>/dev/null || true
# Clear every SRS_* variable, so only the config file configures SRS.
(
  for name in $(env | sed -n 's/^\(SRS_[A-Za-z0-9_]*\)=.*/\1/p'); do
    unset "$name"
  done
  cd "$WORKSPACE/trunk"
  exec "$SRS_BINARY" -c "$SRS_CONF" >"$SRS_STDOUT" 2>&1
) &
SRS_PID=$!
echo "SRS PID: $SRS_PID"

echo "Waiting for the HTTP API (up to 15s)..."
for ((i = 1; i <= 15; i++)); do
  if curl -fsS "$API/api/v1/versions" >/dev/null 2>&1; then
    break
  fi
  if ! kill -0 "$SRS_PID" 2>/dev/null; then
    fail "SRS exited during startup."
  fi
  sleep 1
done
curl -fsS "$API/api/v1/versions" >/dev/null 2>&1 || fail "HTTP API not ready on port $HTTP_API_PORT."
echo "PASS: HTTP API is up on the config port $HTTP_API_PORT."

# --- Step 4: Verify the pid and log files from the config ---
echo "=== Step 4: Verifying pid and log files ==="
if [[ "$(cat "$SRS_PID_FILE" 2>/dev/null)" == "$SRS_PID" ]]; then
  echo "PASS: pid file $SRS_PID_FILE holds $SRS_PID."
else
  fail "pid file $SRS_PID_FILE holds '$(cat "$SRS_PID_FILE" 2>/dev/null)', want $SRS_PID."
fi
if [[ -s "$SRS_LOG_FILE" ]]; then
  echo "PASS: log file $SRS_LOG_FILE is written."
else
  fail "log file $SRS_LOG_FILE is empty or missing."
fi

# --- Step 5: Publish RTMP stream ---
echo "=== Step 5: Publishing RTMP stream ==="
"$FFMPEG_BIN" -stream_loop -1 -re -i "$SOURCE_FLV" -c copy -f flv \
  "rtmp://127.0.0.1:$RTMP_PORT/$STREAM_URL" >"$FFMPEG_LOG" 2>&1 &
FFMPEG_PID=$!
echo "FFmpeg publisher PID: $FFMPEG_PID"
sleep 5

if ! kill -0 "$FFMPEG_PID" 2>/dev/null; then
  echo "FFmpeg log:" >&2
  cat "$FFMPEG_LOG" >&2
  fail "FFmpeg publisher failed."
fi
echo "Stream publishing."

# --- Step 6: Verify the stream in the HTTP API ---
echo "=== Step 6: Verifying the stream in the HTTP API ==="
STREAMS=$(curl -fsS "$API/api/v1/streams/" || true)
if echo "$STREAMS" | jq -e --arg name "$STREAM_NAME" '.streams[] | select(.name == $name and .publish.active == true)' >/dev/null 2>&1; then
  echo "PASS: HTTP API lists the active stream $STREAM_NAME."
else
  echo "Streams: $STREAMS" >&2
  fail "HTTP API does not list the active stream $STREAM_NAME."
fi

# --- Step 7: Verify RTMP and HTTP-FLV playback ---
echo "=== Step 7: Verifying RTMP and HTTP-FLV playback ==="
probe_has_audio_video "RTMP" "rtmp://127.0.0.1:$RTMP_PORT/$STREAM_URL"
probe_has_audio_video "HTTP-FLV" "http://127.0.0.1:$HTTP_SERVER_PORT/$STREAM_URL.flv"

# --- Step 8: Verify HLS files and playback ---
echo "=== Step 8: Verifying HLS in the config hls_path ==="
HLS_URL="http://127.0.0.1:$HTTP_SERVER_PORT/$STREAM_URL.m3u8"
echo "Waiting for HLS playlist (up to 45s): $HLS_URL"
for ((i = 1; i <= 45; i++)); do
  if curl -fsS "$HLS_URL" 2>/dev/null | grep -q "#EXTM3U"; then
    break
  fi
  sleep 1
done
curl -fsS "$HLS_URL" 2>/dev/null | grep -q "#EXTM3U" || fail "HLS playlist was not generated in 45s."
if [[ -f "$HLS_DIR/$STREAM_URL.m3u8" ]]; then
  echo "PASS: HLS playlist written to the config hls_path $HLS_DIR."
else
  fail "HLS playlist not found at $HLS_DIR/$STREAM_URL.m3u8."
fi
probe_has_audio_video "HLS" "$HLS_URL"

# --- Step 9: Verify WHEP playback of the RTMP stream (rtmp_to_rtc) ---
echo "=== Step 9: Verifying WHEP playback of the RTMP stream ==="
verify_whep_playback "WHEP (rtmp_to_rtc)" "$API/rtc/v1/whep/?app=live&stream=$STREAM_NAME" \
  "$TEST_DIR/pion-whep-rtmp.log"

# --- Step 10: Publish WHIP stream ---
# WebRTC requires H.264 (baseline-friendly) + Opus, so transcode source.flv.
echo "=== Step 10: Publishing WHIP stream ==="
"$FFMPEG_BIN" -stream_loop -1 -re -i "$SOURCE_FLV" \
  -c:v libx264 -profile:v baseline -level 3.1 -pix_fmt yuv420p \
  -tune zerolatency -preset ultrafast \
  -c:a libopus -ar 48000 -ac 2 \
  -f whip "$API/rtc/v1/whip/?app=live&stream=$WHIP_STREAM_NAME" >"$WHIP_LOG" 2>&1 &
WHIP_PID=$!
echo "FFmpeg WHIP publisher PID: $WHIP_PID"
# Wait for the SDP exchange, DTLS-SRTP handshake and the rtc_to_rtmp bridge.
sleep 8

if ! kill -0 "$WHIP_PID" 2>/dev/null; then
  echo "FFmpeg WHIP log:" >&2
  cat "$WHIP_LOG" >&2
  fail "FFmpeg WHIP publisher failed."
fi
echo "WHIP stream publishing."

# --- Step 11: Verify WHEP and RTMP playback of the WHIP stream ---
echo "=== Step 11: Verifying WHEP and RTMP (rtc_to_rtmp) playback of the WHIP stream ==="
verify_whep_playback "WHEP (WHIP)" "$API/rtc/v1/whep/?app=live&stream=$WHIP_STREAM_NAME" \
  "$TEST_DIR/pion-whep-whip.log"
probe_has_audio_video "RTMP (rtc_to_rtmp)" "rtmp://127.0.0.1:$RTMP_PORT/$WHIP_STREAM_URL"

echo ""
echo "=== E2E SRS Config File Test PASSED ==="
