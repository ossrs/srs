#!/bin/bash
# E2E test for the Oryx "Live" scenario page (tab=live): query the publish
# secret the page uses, then publish through every protocol that page offers
# -- RTMP, SRT, and WHIP -- and for each one verify playback via RTMP,
# HTTP-FLV, and HLS, plus confirm the stream shows up in the SRS HTTP API.
#
# Starts Redis (if unreachable), local SRS, and the Oryx Go backend as
# needed (same self-start/cleanup-only-what-it-started behavior as
# oryx-api-smoke-test.sh), then publishes with ffmpeg and verifies with
# ffprobe/curl (same pattern as proxy-e2e-srt-test.sh / proxy-e2e-whip-test.sh).
#
# SRT publish requires an ffmpeg built with libsrt, which the default
# Homebrew formula does not include; this script builds one via
# setup-ffmpeg-with-whip.sh on first use (can take several minutes).

SCRIPT_DIR="$(cd -P "$(dirname "$0")" && pwd)"
# Walk up from SCRIPT_DIR looking for go.mod, the SRS repo root. This avoids
# brittle "../../../.." counting when the skills directory is reached via a
# symlink (which changes the symbolic vs. physical depth).
WORKSPACE="$SCRIPT_DIR"
while [[ "$WORKSPACE" != "/" && ! -f "$WORKSPACE/go.mod" ]]; do
  WORKSPACE="$(dirname "$WORKSPACE")"
done
if [[ ! -f "$WORKSPACE/go.mod" ]]; then
  echo "Error: go.mod not found walking up from: $SCRIPT_DIR" >&2
  exit 1
fi

ORYX_DIR="$WORKSPACE/oryx"
PLATFORM_DIR="$ORYX_DIR/platform"
SRS_BINARY="$WORKSPACE/trunk/objs/srs"
SRS_CONF="containers/conf/srs.release-local.conf"
SOURCE_FLV="$WORKSPACE/trunk/doc/source.flv"
ENDPOINT="${ORYX_ENDPOINT:-http://localhost:2022}"
SRS_API="${SRS_API_ENDPOINT:-http://localhost:1985}"
SRS_RTMP="${SRS_RTMP_ENDPOINT:-rtmp://localhost:1935}"
SRS_HTTP="${SRS_HTTP_ENDPOINT:-http://localhost:8080}"
SRS_SRT="${SRS_SRT_ENDPOINT:-srt://localhost:10080}"
ENV_FILE="$PLATFORM_DIR/containers/data/config/.env"
APP="live"

# PIDs of processes this script started; empty means "already running,
# leave it alone" and cleanup skips it. FFMPEG_PID is always ours to kill.
SRS_PID=""
BACKEND_PID=""
FFMPEG_PID=""

cleanup() {
  echo ""
  echo "=== Cleaning up ==="
  if [[ -n "$FFMPEG_PID" ]]; then
    kill -9 "$FFMPEG_PID" 2>/dev/null || true
  fi
  if [[ -n "$BACKEND_PID" ]]; then
    echo "Stopping Oryx backend (pid $BACKEND_PID)..."
    kill "$BACKEND_PID" 2>/dev/null || true
  fi
  if [[ -n "$SRS_PID" ]]; then
    echo "Stopping local SRS (pid $SRS_PID)..."
    kill "$SRS_PID" 2>/dev/null || true
  fi
  sleep 1
  if [[ -n "$BACKEND_PID" ]]; then
    kill -9 "$BACKEND_PID" 2>/dev/null || true
    # "go run" builds and execs a child process; the wrapper PID above may
    # not own it, so also reap anything still bound to the backend ports.
    for port in 2022 2024 2443; do
      lsof -ti :"$port" 2>/dev/null | xargs kill -9 2>/dev/null || true
    done
  fi
  if [[ -n "$SRS_PID" ]]; then
    kill -9 "$SRS_PID" 2>/dev/null || true
    for port in 1935 1985 8080 8000 10080; do
      lsof -ti :"$port" 2>/dev/null | xargs kill -9 2>/dev/null || true
    done
  fi
  echo "Cleanup done. Redis is left running (shared service)."
}
trap cleanup EXIT

wait_for_http() {
  local url="$1" max="$2" waited=0
  while ! curl -sS -m 2 -o /dev/null "$url" 2>/dev/null; do
    waited=$((waited + 1))
    if [[ "$waited" -ge "$max" ]]; then
      return 1
    fi
    sleep 1
  done
  return 0
}

wait_for_redis() {
  local max="$1" waited=0
  while [[ "$(redis-cli ping 2>/dev/null)" != "PONG" ]]; do
    waited=$((waited + 1))
    if [[ "$waited" -ge "$max" ]]; then
      return 1
    fi
    sleep 1
  done
  return 0
}

# SRT needs libsrt, WHIP needs the whip muxer -- the default Homebrew ffmpeg
# formula has neither reliably. Resolution order: PATH, then ~/.local/bin
# (built previously), then build from source via setup-ffmpeg-with-whip.sh.
ffmpeg_supports_srt_and_whip() {
  local bin="$1"
  [[ -x "$bin" ]] || return 1
  "$bin" -hide_banner -protocols 2>/dev/null | grep -qw srt || return 1
  "$bin" -hide_banner -muxers 2>/dev/null | grep -qw whip || return 1
  return 0
}

resolve_ffmpeg() {
  local sys_ffmpeg sys_ffprobe local_ffmpeg local_ffprobe
  sys_ffmpeg="$(command -v ffmpeg || true)"
  sys_ffprobe="$(command -v ffprobe || true)"
  local_ffmpeg="$HOME/.local/bin/ffmpeg"
  local_ffprobe="$HOME/.local/bin/ffprobe"

  if [[ -n "$sys_ffprobe" ]] && ffmpeg_supports_srt_and_whip "$sys_ffmpeg"; then
    FFMPEG_BIN="$sys_ffmpeg"
    FFPROBE_BIN="$sys_ffprobe"
    return 0
  fi
  if [[ -x "$local_ffprobe" ]] && ffmpeg_supports_srt_and_whip "$local_ffmpeg"; then
    FFMPEG_BIN="$local_ffmpeg"
    FFPROBE_BIN="$local_ffprobe"
    return 0
  fi
  return 1
}

probe_has_audio_video() {
  local name="$1" url="$2"
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
  local url="$1" deadline=60
  echo "Waiting for HLS playlist to be generated (up to ${deadline}s): $url"
  for ((i = 1; i <= deadline; i++)); do
    if curl -fsS "$url" 2>/dev/null | grep -q "#EXTM3U"; then
      echo "HLS playlist is ready."
      return
    fi
    sleep 1
  done
  echo "FAIL: HLS playlist was not generated in ${deadline}s." >&2
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
  local url="$1" deadline=60
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
  curl -fsS "$url" 2>&1 || true
  exit 1
}

# Confirm the stream is actually live per the SRS HTTP API, mirroring what a
# maintainer would check by hand: GET /api/v1/streams/ and look for the
# stream name with an active publisher.
check_srs_stream_published() {
  local stream="$1" deadline=15
  echo "Checking SRS HTTP API for published stream: $stream"
  for ((i = 1; i <= deadline; i++)); do
    local body
    body=$(curl -sS -m 3 "$SRS_API/api/v1/streams/" 2>/dev/null)
    if echo "$body" | grep -q "\"name\":\"$stream\"" && echo "$body" | grep -q '"publish":{"active":true'; then
      echo "PASS: SRS API reports $stream as actively published."
      return
    fi
    sleep 1
  done
  echo "FAIL: SRS API never reported $stream as actively published." >&2
  echo "Last response: $body" >&2
  exit 1
}

echo "=== Oryx Live Streaming Test ==="
echo "Endpoint: $ENDPOINT"
echo ""

# --- Step 0: Ensure Redis, SRS, and the Oryx Go backend are up ---
echo "=== Step 0: Ensure Redis, SRS, and the Oryx Go backend are running ==="

if [[ "$(redis-cli ping 2>/dev/null)" != "PONG" ]]; then
  echo "Redis not reachable, starting via 'brew services start redis'..."
  if ! command -v brew &>/dev/null; then
    echo "FAIL: redis is not running and 'brew' is not available to start it." >&2
    exit 1
  fi
  brew services start redis >/dev/null
  if ! wait_for_redis 15; then
    echo "FAIL: redis did not become ready within 15s." >&2
    exit 1
  fi
  echo "Redis: started."
else
  echo "Redis: already running, leaving it alone."
fi

if ! curl -sS -m 2 -o /dev/null "$SRS_API/api/v1/versions" 2>/dev/null; then
  if [[ ! -f "$SRS_BINARY" ]]; then
    echo "FAIL: SRS binary not found at $SRS_BINARY." >&2
    echo "Build it first: cd $WORKSPACE/trunk && ./configure && make" >&2
    exit 1
  fi
  echo "SRS not reachable, starting local SRS..."
  (cd "$PLATFORM_DIR" && exec "$SRS_BINARY" -c "$SRS_CONF") >/tmp/oryx-live-srs.log 2>&1 &
  SRS_PID=$!
  if ! wait_for_http "$SRS_API/api/v1/versions" 15; then
    echo "FAIL: SRS did not become ready within 15s. Logs:" >&2
    cat /tmp/oryx-live-srs.log >&2
    exit 1
  fi
  echo "SRS: started (pid $SRS_PID)."
else
  echo "SRS: already running, leaving it alone."
fi

if ! curl -sS -m 2 -o /dev/null "$ENDPOINT/terraform/v1/mgmt/versions" 2>/dev/null; then
  echo "Oryx backend not reachable, starting 'go run .'..."
  (cd "$PLATFORM_DIR" && exec env AUTO_SELF_SIGNED_CERTIFICATE=off go run .) >/tmp/oryx-live-backend.log 2>&1 &
  BACKEND_PID=$!
  if ! wait_for_http "$ENDPOINT/terraform/v1/mgmt/versions" 60; then
    echo "FAIL: Oryx backend did not become ready within 60s. Logs:" >&2
    cat /tmp/oryx-live-backend.log >&2
    exit 1
  fi
  echo "Oryx backend: started (pid $BACKEND_PID)."
else
  echo "Oryx backend: already running, leaving it alone."
fi
echo ""

# --- Step 1: Resolve ffmpeg/ffprobe with SRT + WHIP support ---
echo "=== Step 1: Resolve ffmpeg with SRT and WHIP support ==="
if ! resolve_ffmpeg; then
  echo "No ffmpeg with both SRT and WHIP support found on PATH or in ~/.local/bin."
  echo "Building ffmpeg from source via setup-ffmpeg-with-whip.sh -- this can take several minutes."
  bash "$SCRIPT_DIR/setup-ffmpeg-with-whip.sh"
  FFMPEG_BIN="$HOME/.local/bin/ffmpeg"
  FFPROBE_BIN="$HOME/.local/bin/ffprobe"
  if ! ffmpeg_supports_srt_and_whip "$FFMPEG_BIN"; then
    echo "Error: ffmpeg still lacks SRT/WHIP support after running setup-ffmpeg-with-whip.sh." >&2
    exit 1
  fi
fi
echo "ffmpeg : $FFMPEG_BIN"
echo "ffprobe: $FFPROBE_BIN"
if [[ ! -f "$SOURCE_FLV" ]]; then
  echo "FAIL: test source not found: $SOURCE_FLV" >&2
  exit 1
fi
echo ""

# --- Step 2: Login and fetch the publish secret the Live page uses ---
echo "=== Step 2: Login and query the publish secret ==="
PASSWORD="${MGMT_PASSWORD:-}"
if [[ -z "$PASSWORD" && -f "$ENV_FILE" ]]; then
  # godotenv quotes values, e.g. MGMT_PASSWORD="abc123" -- strip the quotes.
  PASSWORD=$(sed -n 's/^MGMT_PASSWORD="\(.*\)"$/\1/p' "$ENV_FILE")
fi
if [[ -z "$PASSWORD" ]]; then
  echo "FAIL: no MGMT_PASSWORD found (env var or $ENV_FILE)." >&2
  exit 1
fi

LOGIN_RESP=$(curl -sS -m 5 -X POST "$ENDPOINT/terraform/v1/mgmt/login" \
  -H 'Content-Type: application/json' \
  -d "{\"password\":\"$PASSWORD\"}")
BEARER=$(echo "$LOGIN_RESP" | sed -n 's/.*"bearer":"\([^"]*\)".*/\1/p')
if [[ -z "$BEARER" ]]; then
  echo "FAIL: login did not return a security key: $LOGIN_RESP" >&2
  exit 1
fi

SECRET_RESP=$(curl -sS -m 5 -X POST "$ENDPOINT/terraform/v1/hooks/srs/secret/query" \
  -H "Authorization: Bearer $BEARER" \
  -H 'Content-Type: application/json' \
  -d '{}')
PUBLISH_SECRET=$(echo "$SECRET_RESP" | sed -n 's/.*"publish":"\([^"]*\)".*/\1/p')
if [[ -z "$PUBLISH_SECRET" ]]; then
  echo "FAIL: secret/query did not return a publish secret: $SECRET_RESP" >&2
  exit 1
fi
echo "PASS: publish secret resolved (${#PUBLISH_SECRET} chars)."
echo ""

# --- Step 3: RTMP publish, verify RTMP/HTTP-FLV/HLS playback ---
echo "=== Step 3: Publish via RTMP, verify RTMP/HTTP-FLV/HLS playback ==="
STREAM_RTMP="livetest-rtmp-$(date +%s)"
"$FFMPEG_BIN" -re -stream_loop -1 -i "$SOURCE_FLV" -c copy -f flv \
  "$SRS_RTMP/$APP/$STREAM_RTMP?secret=$PUBLISH_SECRET" >/tmp/oryx-live-ffmpeg-rtmp.log 2>&1 &
FFMPEG_PID=$!
sleep 3
if ! kill -0 "$FFMPEG_PID" 2>/dev/null; then
  echo "FAIL: RTMP publisher exited early. Logs:" >&2
  cat /tmp/oryx-live-ffmpeg-rtmp.log >&2
  exit 1
fi
check_srs_stream_published "$STREAM_RTMP"
probe_has_audio_video "RTMP" "$SRS_RTMP/$APP/$STREAM_RTMP"
probe_has_audio_video "HTTP-FLV" "$SRS_HTTP/$APP/$STREAM_RTMP.flv"
HLS_URL="$SRS_HTTP/$APP/$STREAM_RTMP.m3u8"
wait_for_hls_playlist "$HLS_URL"
wait_for_hls_to_skip_first_segment "$HLS_URL"
probe_has_audio_video "HLS" "$HLS_URL"
kill -9 "$FFMPEG_PID" 2>/dev/null || true
wait "$FFMPEG_PID" 2>/dev/null || true
FFMPEG_PID=""
echo ""

# --- Step 4: SRT publish, verify RTMP/HTTP-FLV/HLS playback (srt_to_rtmp) ---
echo "=== Step 4: Publish via SRT, verify RTMP/HTTP-FLV/HLS playback ==="
STREAM_SRT="livetest-srt-$(date +%s)"
"$FFMPEG_BIN" -re -stream_loop -1 -i "$SOURCE_FLV" -c copy -f mpegts \
  "$SRS_SRT?streamid=#!::r=$APP/$STREAM_SRT?secret=$PUBLISH_SECRET,m=publish" >/tmp/oryx-live-ffmpeg-srt.log 2>&1 &
FFMPEG_PID=$!
sleep 3
if ! kill -0 "$FFMPEG_PID" 2>/dev/null; then
  echo "FAIL: SRT publisher exited early. Logs:" >&2
  cat /tmp/oryx-live-ffmpeg-srt.log >&2
  exit 1
fi
check_srs_stream_published "$STREAM_SRT"
probe_has_audio_video "RTMP" "$SRS_RTMP/$APP/$STREAM_SRT"
probe_has_audio_video "HTTP-FLV" "$SRS_HTTP/$APP/$STREAM_SRT.flv"
HLS_URL="$SRS_HTTP/$APP/$STREAM_SRT.m3u8"
wait_for_hls_playlist "$HLS_URL"
wait_for_hls_to_skip_first_segment "$HLS_URL"
probe_has_audio_video "HLS" "$HLS_URL"
kill -9 "$FFMPEG_PID" 2>/dev/null || true
wait "$FFMPEG_PID" 2>/dev/null || true
FFMPEG_PID=""
echo ""

# --- Step 5: WHIP publish, verify RTMP/HTTP-FLV/HLS playback (rtc_to_rtmp) ---
echo "=== Step 5: Publish via WHIP, verify RTMP/HTTP-FLV/HLS playback ==="
STREAM_WHIP="livetest-whip-$(date +%s)"
WHIP_URL="$ENDPOINT/rtc/v1/whip/?app=$APP&stream=$STREAM_WHIP&secret=$PUBLISH_SECRET"
echo "Publish URL: $WHIP_URL"
# WebRTC requires H.264 (baseline-friendly) + Opus. source.flv is H.264 High
# profile + AAC, so transcode video to baseline and audio to Opus.
"$FFMPEG_BIN" -stream_loop -1 -re -i "$SOURCE_FLV" \
  -c:v libx264 -profile:v baseline -level 3.1 -pix_fmt yuv420p \
  -tune zerolatency -preset ultrafast -g 25 \
  -c:a libopus -ar 48000 -ac 2 \
  -f whip "$WHIP_URL" >/tmp/oryx-live-ffmpeg-whip.log 2>&1 &
FFMPEG_PID=$!
sleep 10
if ! kill -0 "$FFMPEG_PID" 2>/dev/null; then
  echo "FAIL: WHIP publisher exited early. Logs:" >&2
  cat /tmp/oryx-live-ffmpeg-whip.log >&2
  exit 1
fi
check_srs_stream_published "$STREAM_WHIP"
probe_has_audio_video "RTMP" "$SRS_RTMP/$APP/$STREAM_WHIP"
probe_has_audio_video "HTTP-FLV" "$SRS_HTTP/$APP/$STREAM_WHIP.flv"
HLS_URL="$SRS_HTTP/$APP/$STREAM_WHIP.m3u8"
wait_for_hls_playlist "$HLS_URL"
wait_for_hls_to_skip_first_segment "$HLS_URL"
probe_has_audio_video "HLS" "$HLS_URL"
kill -9 "$FFMPEG_PID" 2>/dev/null || true
wait "$FFMPEG_PID" 2>/dev/null || true
FFMPEG_PID=""
echo ""

echo "=== Oryx Live Streaming Test PASSED ==="
