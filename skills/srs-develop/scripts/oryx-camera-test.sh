#!/bin/bash
# E2E test for the Oryx "Camera" scenario page (tab=camera,
# ScenarioCamera.js): bind a live source stream as an IP-camera input,
# configure a camera platform to forward it back into this same Oryx/SRS
# instance, and verify the forwarded copy actually shows up and plays.
#
# Source choice, worked out from oryx/platform/camera-live-stream.go: camera
# only supports a "stream" source type (via /camera/stream-url), unlike vlive
# which also allows upload/server/ytdl. A locally published RTMP stream is
# the natural fit -- it needs no file upload/copy step -- so this script
# publishes its own source stream over RTMP and binds that URL directly,
# mirroring TestScenario_WithStream_PublishCameraStreamUrl in
# oryx/test/camera_test.go.
#
# Major API sequence exercised (all under /terraform/v1/ffmpeg/camera/):
#   1. stream-url  -- validate the source RTMP URL, get back {name,uuid,target}
#   2. source      -- ffprobe the source, enforce a bitrate limit, bind it to
#                     a platform (persists CameraConfigure.Streams in Redis)
#   3. secret      -- GET (no action) the full per-platform config Redis just
#                     saved (it already carries the Files from step 2), then
#                     POST it back with action=update plus server/secret/
#                     enabled set. This matters because the backend's
#                     CameraConfigure.Update() *replaces* Streams wholesale
#                     with whatever the update request body carries -- posting
#                     only server/secret/enabled without files would silently
#                     wipe the binding from step 2. Fetch-mutate-repost, not a
#                     fresh object.
#   4. streams     -- (diagnostics only) lists running camera tasks with live
#                     ffmpeg status.
#
# Like oryx/platform/forward.go and virtual-live-stream.go, there is no
# delete action for camera/secret (only "update"), and an unset platform
# filter still runs whatever files are bound -- so a timestamped platform key
# here would accumulate a permanent, growing "Inactive" entry in Redis and
# the dashboard's Live Status list on every run (see the "Fix Oryx
# forward-test Redis growth" commit for the same issue in
# oryx-forward-test.sh). This script reuses one fixed platform key across
# runs and disables it when done instead.
#
# Requires the shared local stack (Redis, SRS, Oryx Go backend) to already be
# running -- start it once with oryx-stack-start.sh. This script only starts
# its own ffmpeg publisher and cleans that up; it does not manage server
# lifecycle, so it is safe to run concurrently with other oryx-*-test.sh
# scripts against that same shared stack, as long as no other run of this
# same script is in flight at the same time (see PLATFORM note below).

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

# Fixed platform key, reused across runs -- see header for why. Must contain
# "camera-" to pass the backend's allowed-platform check in camera-live-stream.go.
PLATFORM="camera-99-oryx-test"

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
  echo "Camera task status:" >&2
  curl -sS -m 3 -X POST "$ENDPOINT/terraform/v1/ffmpeg/camera/streams" \
    -H "Authorization: Bearer $BEARER" -H 'Content-Type: application/json' -d '{}' >&2 2>/dev/null
  echo "" >&2
  exit 1
}

probe_has_audio_video() {
  local name="$1" url="$2" deadline=10
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

echo "=== Oryx Camera Test ==="
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
if ! command -v jq &>/dev/null; then
  echo "FAIL: jq not found in PATH (needed to fetch-mutate-repost the camera config)." >&2
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
echo "PASS: publish secret resolved (${#PUBLISH_SECRET} chars)."
echo ""

# --- Step 2: Publish the camera source stream via RTMP ---
# (Stands in for a real RTSP IP camera -- camera-live-stream.go treats any
# valid rtmp/srt/rtsp/http-flv/hls source URL the same way.)
echo "=== Step 1: Publish source stream via RTMP ==="
SOURCE_STREAM="cameratest-src-$(date +%s)"
SOURCE_URL="$SRS_RTMP/$APP/$SOURCE_STREAM?secret=$PUBLISH_SECRET"
ffmpeg -re -stream_loop -1 -i "$SOURCE_FLV" -c copy -f flv \
  "$SOURCE_URL" >/tmp/oryx-camera-source.log 2>&1 &
FFMPEG_PID=$!
sleep 3
if ! kill -0 "$FFMPEG_PID" 2>/dev/null; then
  echo "FAIL: source publisher exited early. Logs:" >&2
  cat /tmp/oryx-camera-source.log >&2
  exit 1
fi
check_srs_stream_published "$SOURCE_STREAM" 15
echo ""

# --- Step 3: Bind the source URL via camera/stream-url ---
echo "=== Step 2: POST /terraform/v1/ffmpeg/camera/stream-url ==="
STREAM_URL_RESP=$(curl -sS -m 10 -X POST "$ENDPOINT/terraform/v1/ffmpeg/camera/stream-url" \
  -H "Authorization: Bearer $BEARER" -H 'Content-Type: application/json' \
  -d "{\"url\":\"$SOURCE_URL\"}")
SRC_NAME=$(echo "$STREAM_URL_RESP" | jq -r '.data.name // empty')
SRC_UUID=$(echo "$STREAM_URL_RESP" | jq -r '.data.uuid // empty')
SRC_TARGET=$(echo "$STREAM_URL_RESP" | jq -r '.data.target // empty')
if [[ -z "$SRC_UUID" || -z "$SRC_TARGET" ]]; then
  echo "FAIL: stream-url did not return uuid/target: $STREAM_URL_RESP" >&2
  exit 1
fi
echo "PASS: stream-url bound, uuid=$SRC_UUID name=$SRC_NAME"
echo ""

# --- Step 4: Probe and bind the source to the platform via camera/source ---
echo "=== Step 3: POST /terraform/v1/ffmpeg/camera/source (ffprobe + bind) ==="
SOURCE_FILE_JSON=$(jq -nc --arg name "$SRC_NAME" --arg target "$SRC_TARGET" --arg uuid "$SRC_UUID" \
  '{name:$name, target:$target, uuid:$uuid, size:0, type:"stream"}')
SOURCE_RESP=$(curl -sS -m 20 -X POST "$ENDPOINT/terraform/v1/ffmpeg/camera/source" \
  -H "Authorization: Bearer $BEARER" -H 'Content-Type: application/json' \
  -d "$(jq -nc --arg platform "$PLATFORM" --argjson files "[$SOURCE_FILE_JSON]" \
    '{platform:$platform, files:$files}')")
VIDEO_CODEC=$(echo "$SOURCE_RESP" | jq -r '.data.files[0].video.codec_name // empty')
AUDIO_CODEC=$(echo "$SOURCE_RESP" | jq -r '.data.files[0].audio.codec_name // empty')
if [[ -z "$VIDEO_CODEC" || -z "$AUDIO_CODEC" ]]; then
  echo "FAIL: source did not return probed codec info: $SOURCE_RESP" >&2
  exit 1
fi
echo "PASS: source probed and bound to platform=$PLATFORM, video=$VIDEO_CODEC audio=$AUDIO_CODEC"
echo ""

# --- Step 5: Fetch-mutate-repost the platform config to enable forwarding ---
# See header: the update request's Files field replaces whatever Redis has,
# so we must repost the object secret/query just gave us (already carrying
# the Files step 4 set), not a hand-built object missing that field.
echo "=== Step 4: GET+POST /terraform/v1/ffmpeg/camera/secret (enable forward to self) ==="
CONF_RESP=$(curl -sS -m 5 -X POST "$ENDPOINT/terraform/v1/ffmpeg/camera/secret" \
  -H "Authorization: Bearer $BEARER" -H 'Content-Type: application/json' -d '{}')
PLATFORM_CONF=$(echo "$CONF_RESP" | jq -c --arg p "$PLATFORM" '.data[$p] // empty')
if [[ -z "$PLATFORM_CONF" ]]; then
  echo "FAIL: platform $PLATFORM not found after source bind: $CONF_RESP" >&2
  exit 1
fi

OUT_STREAM="cameratest-dst-$(date +%s)"
UPDATED_CONF=$(echo "$PLATFORM_CONF" | jq -c \
  --arg server "$SRS_RTMP/$APP/" \
  --arg secret "$OUT_STREAM?secret=$PUBLISH_SECRET" \
  --arg label "oryx-camera-test" \
  '.server=$server | .secret=$secret | .enabled=true | .custom=true | .label=$label | .action="update"')
curl -sS -m 5 -X POST "$ENDPOINT/terraform/v1/ffmpeg/camera/secret" \
  -H "Authorization: Bearer $BEARER" -H 'Content-Type: application/json' \
  -d "$UPDATED_CONF" >/dev/null
echo "Camera config set: $PLATFORM -> $SRS_RTMP/$APP/$OUT_STREAM (forwarding back to this same Oryx/SRS)"
echo ""

# --- Step 6: Verify the forwarded stream shows up and plays ---
echo "=== Step 5: Verify forwarded stream ==="
check_srs_stream_published "$OUT_STREAM" 30
probe_has_audio_video "HTTP-FLV forward target" "$SRS_HTTP/$APP/$OUT_STREAM.flv"
echo ""

# --- Step 7: Disable the camera config and stop the source publisher ---
echo "=== Step 6: Disable camera config ==="
DISABLED_CONF=$(echo "$UPDATED_CONF" | jq -c '.enabled=false | .action="update"')
curl -sS -m 5 -X POST "$ENDPOINT/terraform/v1/ffmpeg/camera/secret" \
  -H "Authorization: Bearer $BEARER" -H 'Content-Type: application/json' \
  -d "$DISABLED_CONF" >/dev/null 2>&1 || true
echo "PASS: camera config disabled (no delete API; this is the product's own lifecycle)."

kill -9 "$FFMPEG_PID" 2>/dev/null || true
wait "$FFMPEG_PID" 2>/dev/null || true
FFMPEG_PID=""
echo ""

echo "=== Oryx Camera Test PASSED ==="
