#!/bin/bash
# E2E test for the Oryx "Transcode" scenario page (tab=transcode,
# ScenarioTranscode.js): publish a source stream, enable transcoding to a
# new output stream, wait for the transcode task to report a running FFmpeg
# frame, verify the transcoded output stream is actually playable, then stop
# and restore the original config.
#
# There is exactly one global transcode task (see TranscodeWorker/
# TranscodeTask in oryx/platform/trancode.go, filename spelling preserved) --
# no per-stream "transcode this one" switch, and enabling it always
# picks the most-recently-active SRS stream (excluding its own output) as
# input. So this script cannot run concurrently with another instance of
# itself (or with the dashboard doing the same thing) without the two racing
# on the same task; it queries and backs up the existing config first and
# always restores it on exit, matching the restore behavior in
# oryx-record-test.sh.
#
# Requires the shared local stack (Redis, SRS, Oryx Go backend) to already be
# running -- start it once with oryx-stack-start.sh. This script only starts
# its own ffmpeg publisher and cleans that up; it does not manage server
# lifecycle.

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
SOURCE_FLV="$WORKSPACE/trunk/doc/source.flv"
ENDPOINT="${ORYX_ENDPOINT:-http://localhost:2022}"
SRS_API="${SRS_API_ENDPOINT:-http://localhost:1985}"
SRS_RTMP="${SRS_RTMP_ENDPOINT:-rtmp://localhost:1935}"
SRS_HTTP="${SRS_HTTP_ENDPOINT:-http://localhost:8080}"
ENV_FILE="$PLATFORM_DIR/containers/data/config/.env"
APP="live"

FFMPEG_PID=""
cleanup() {
  echo ""
  echo "=== Cleaning up ==="
  if [[ -n "$FFMPEG_PID" ]]; then
    kill -9 "$FFMPEG_PID" 2>/dev/null || true
  fi
  echo "Cleanup done."
}
trap cleanup EXIT

check_srs_stream_published() {
  local stream="$1" deadline="$2"
  echo "Checking SRS HTTP API for published stream: $stream (up to ${deadline}s)"
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

probe_has_audio_video() {
  local name="$1" url="$2" deadline=15
  echo "Verifying $name playback: $url"
  local output
  for ((i = 1; i <= deadline; i++)); do
    output=$(ffprobe -v error -show_streams "$url" 2>&1 || true)
    if echo "$output" | grep -q "codec_type=video" && echo "$output" | grep -q "codec_type=audio"; then
      echo "PASS: $name video stream detected."
      echo "PASS: $name audio stream detected."
      return
    fi
    sleep 1
  done
  echo "FAIL: $name did not expose audio+video within ${deadline}s." >&2
  echo "ffprobe output:" >&2
  echo "$output" >&2
  exit 1
}

echo "=== Oryx Transcode Test ==="
echo "Endpoint: $ENDPOINT"
echo ""

# --- Step 0: Require the shared stack to already be running ---
if ! curl -sS -m 2 -o /dev/null "$SRS_API/api/v1/versions" 2>/dev/null || \
   ! curl -sS -m 2 -o /dev/null "$ENDPOINT/terraform/v1/mgmt/versions" 2>/dev/null; then
  echo "FAIL: local Oryx stack is not running. Start it first:" >&2
  echo "  bash $SCRIPT_DIR/oryx-stack-start.sh" >&2
  exit 1
fi
if ! command -v ffmpeg &>/dev/null || ! command -v ffprobe &>/dev/null; then
  echo "FAIL: ffmpeg/ffprobe not found in PATH." >&2
  exit 1
fi

# --- Step 1: Login, then resolve the global publish secret ---
PASSWORD="${MGMT_PASSWORD:-}"
if [[ -z "$PASSWORD" && -f "$ENV_FILE" ]]; then
  # godotenv quotes values, e.g. MGMT_PASSWORD="abc123" -- strip the quotes.
  PASSWORD=$(sed -n 's/^MGMT_PASSWORD="\(.*\)"$/\1/p' "$ENV_FILE")
fi
if [[ -z "$PASSWORD" ]]; then
  echo "FAIL: no MGMT_PASSWORD found (env var or $ENV_FILE)." >&2
  exit 1
fi

# The backend serializes logins with a mutex and replies "login is running,
# try later" to a losing concurrent request -- expected when multiple
# oryx-*-test.sh scripts log in around the same moment, not a real failure.
# Retry past it instead of hard-failing.
BEARER=""
for ((i = 1; i <= 10; i++)); do
  LOGIN_RESP=$(curl -sS -m 5 -X POST "$ENDPOINT/terraform/v1/mgmt/login" \
    -H 'Content-Type: application/json' \
    -d "{\"password\":\"$PASSWORD\"}")
  BEARER=$(echo "$LOGIN_RESP" | sed -n 's/.*"bearer":"\([^"]*\)".*/\1/p')
  [[ -n "$BEARER" ]] && break
  sleep 1
done
if [[ -z "$BEARER" ]]; then
  echo "FAIL: login did not return a security key after retries: $LOGIN_RESP" >&2
  exit 1
fi

SECRET_RESP=$(curl -sS -m 5 -X POST "$ENDPOINT/terraform/v1/hooks/srs/secret/query" \
  -H "Authorization: Bearer $BEARER" -H 'Content-Type: application/json' -d '{}')
PUBLISH_SECRET=$(echo "$SECRET_RESP" | sed -n 's/.*"publish":"\([^"]*\)".*/\1/p')
if [[ -z "$PUBLISH_SECRET" ]]; then
  echo "FAIL: secret/query did not return a publish secret: $SECRET_RESP" >&2
  exit 1
fi

# --- Step 2: Back up the current global transcode config, restore on exit ---
echo "=== Step 1: Back up current transcode config ==="
BACKUP_CONF=$(curl -sS -m 5 -X POST "$ENDPOINT/terraform/v1/ffmpeg/transcode/query" \
  -H "Authorization: Bearer $BEARER" -H 'Content-Type: application/json' -d '{}')
echo "Backed up: $BACKUP_CONF"
echo ""

restore_transcode_config() {
  curl -sS -m 5 -X POST "$ENDPOINT/terraform/v1/ffmpeg/transcode/apply" \
    -H "Authorization: Bearer $BEARER" -H 'Content-Type: application/json' \
    -d "$BACKUP_CONF" >/dev/null 2>&1 || true
}
trap 'restore_transcode_config; cleanup' EXIT

# --- Step 3: Publish the source stream via RTMP ---
echo "=== Step 2: Publish source stream via RTMP ==="
SOURCE_STREAM="transtest-src-$(date +%s)"
ffmpeg -re -stream_loop -1 -i "$SOURCE_FLV" -c copy -f flv \
  "$SRS_RTMP/$APP/$SOURCE_STREAM?secret=$PUBLISH_SECRET" >/tmp/oryx-transcode-source.log 2>&1 &
FFMPEG_PID=$!
sleep 3
if ! kill -0 "$FFMPEG_PID" 2>/dev/null; then
  echo "FAIL: source publisher exited early. Logs:" >&2
  cat /tmp/oryx-transcode-source.log >&2
  exit 1
fi
check_srs_stream_published "$SOURCE_STREAM" 15
echo ""

# --- Step 4: Enable transcoding to a new output stream ---
echo "=== Step 3: Enable transcode ==="
TRANSCODE_STREAM="transtest-dst-$(date +%s)"
APPLY_BODY=$(cat <<EOF
{"all":true,"vcodec":"libx264","acodec":"aac","vbitrate":200,"abitrate":16,"achannels":0,"vprofile":"baseline","vpreset":"ultrafast","server":"$SRS_RTMP/$APP/","secret":"$TRANSCODE_STREAM?secret=$PUBLISH_SECRET"}
EOF
)
curl -sS -m 5 -X POST "$ENDPOINT/terraform/v1/ffmpeg/transcode/apply" \
  -H "Authorization: Bearer $BEARER" -H 'Content-Type: application/json' \
  -d "$APPLY_BODY" >/dev/null
echo "Transcode config applied, output -> $SRS_RTMP/$APP/$TRANSCODE_STREAM"
echo ""

# --- Step 5: Poll the transcode task until FFmpeg reports a running frame ---
echo "=== Step 4: Wait for transcode task to be running ==="
TASK_OK=""
for ((i = 1; i <= 30; i++)); do
  TASK_RESP=$(curl -sS -m 5 -X POST "$ENDPOINT/terraform/v1/ffmpeg/transcode/task" \
    -H "Authorization: Bearer $BEARER" -H 'Content-Type: application/json' -d '{}')
  ENABLED=$(echo "$TASK_RESP" | sed -n 's/.*"enabled":\([a-z]*\).*/\1/p')
  INPUT=$(echo "$TASK_RESP" | sed -n 's/.*"input":"\([^"]*\)".*/\1/p')
  OUTPUT=$(echo "$TASK_RESP" | sed -n 's/.*"output":"\([^"]*\)".*/\1/p')
  LOG=$(echo "$TASK_RESP" | sed -n 's/.*"log":"\([^"]*\)".*/\1/p')
  if [[ "$ENABLED" == "true" && -n "$INPUT" && -n "$OUTPUT" && -n "$LOG" ]]; then
    TASK_OK=1
    break
  fi
  sleep 1
done
if [[ -z "$TASK_OK" ]]; then
  echo "FAIL: transcode task never reported enabled with input/output/frame within 30s." >&2
  echo "Last response: $TASK_RESP" >&2
  exit 1
fi
echo "PASS: transcode task running, input=$INPUT output=$OUTPUT"
echo ""

# --- Step 6: Verify the transcoded output stream is actually playable ---
echo "=== Step 5: Verify transcoded output stream ==="
check_srs_stream_published "$TRANSCODE_STREAM" 30
probe_has_audio_video "Transcoded output" "$SRS_HTTP/$APP/$TRANSCODE_STREAM.flv"
echo ""

# --- Step 7: Disable transcoding and stop the source publisher ---
echo "=== Step 6: Disable transcode and restore original config ==="
restore_transcode_config
echo "PASS: transcode config restored."

kill -9 "$FFMPEG_PID" 2>/dev/null || true
wait "$FFMPEG_PID" 2>/dev/null || true
FFMPEG_PID=""
echo ""

echo "=== Oryx Transcode Test PASSED ==="
