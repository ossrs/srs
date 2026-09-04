#!/bin/bash
# E2E test for the Oryx "Stream" scenario page (tab=stream, ScenarioLiveRoom.js):
# the Live Room API lifecycle -- create, list, query, update (rename), and
# remove a room -- plus a basic check that a room's own publish secret and
# stream actually authenticate an RTMP publish (room secrets are verified
# through a different Redis key than the global publish secret, see
# GenerateRoomPublishKey in oryx/platform/live-room.go).
#
# Deliberately does NOT touch AI assistant settings or AI endpoints: not
# /terraform/v1/mgmt/openai/*, not /terraform/v1/ai/*, and no assertions on
# any of the room's aiXxx/assistant fields. The Live Room API responses
# include those fields (SrsAssistant is embedded in SrsLiveRoom), but this
# script only reads/writes uuid/title/stream/secret/roomToken/created_at.
#
# Requires the shared local stack (Redis, SRS, Oryx Go backend) to already be
# running -- start it once with oryx-stack-start.sh. This script only starts
# its own ffmpeg publisher and cleans that up; it does not manage server
# lifecycle, so it is safe to run concurrently with other oryx-*-test.sh
# scripts against that same shared stack.

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

# Confirm the stream is actually live per the SRS HTTP API.
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

echo "=== Oryx Live Room Test ==="
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
  echo "FAIL: ffmpeg/ffprobe not found in PATH (only RTMP is needed here)." >&2
  exit 1
fi

# --- Step 1: Login for the Bearer security key ---
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

# --- Step 2: POST /terraform/v1/live/room/create ---
echo "=== Step 1: POST /terraform/v1/live/room/create ==="
ROOM_TITLE="oryx-live-room-test-$(date +%s)"
CREATE_RESP=$(curl -sS -m 5 -X POST "$ENDPOINT/terraform/v1/live/room/create" \
  -H "Authorization: Bearer $BEARER" -H 'Content-Type: application/json' \
  -d "{\"title\":\"$ROOM_TITLE\"}")
ROOM_UUID=$(echo "$CREATE_RESP" | sed -n 's/.*"uuid":"\([^"]*\)".*/\1/p')
ROOM_STREAM=$(echo "$CREATE_RESP" | sed -n 's/.*"stream":"\([^"]*\)".*/\1/p')
ROOM_SECRET=$(echo "$CREATE_RESP" | sed -n 's/.*"secret":"\([^"]*\)".*/\1/p')
ROOM_TOKEN=$(echo "$CREATE_RESP" | sed -n 's/.*"roomToken":"\([^"]*\)".*/\1/p')
ROOM_CREATED=$(echo "$CREATE_RESP" | sed -n 's/.*"created_at":"\([^"]*\)".*/\1/p')
if [[ -z "$ROOM_UUID" || -z "$ROOM_STREAM" || -z "$ROOM_SECRET" || -z "$ROOM_TOKEN" ]]; then
  echo "FAIL: create did not return uuid/stream/secret/roomToken: $CREATE_RESP" >&2
  exit 1
fi
echo "PASS: room created, uuid=$ROOM_UUID stream=$ROOM_STREAM"
echo ""

# From here on, always try to remove the room we created, even on failure.
remove_room() {
  curl -sS -m 5 -X POST "$ENDPOINT/terraform/v1/live/room/remove" \
    -H "Authorization: Bearer $BEARER" -H 'Content-Type: application/json' \
    -d "{\"uuid\":\"$ROOM_UUID\"}" >/dev/null 2>&1 || true
}
trap 'remove_room; cleanup' EXIT

# --- Step 3: POST /terraform/v1/live/room/list ---
echo "=== Step 2: POST /terraform/v1/live/room/list ==="
LIST_RESP=$(curl -sS -m 5 -X POST "$ENDPOINT/terraform/v1/live/room/list" \
  -H "Authorization: Bearer $BEARER" -H 'Content-Type: application/json' -d '{}')
if ! echo "$LIST_RESP" | grep -q "\"uuid\":\"$ROOM_UUID\""; then
  echo "FAIL: list did not include the created room: $LIST_RESP" >&2
  exit 1
fi
echo "PASS: list includes the created room."
echo ""

# --- Step 4: POST /terraform/v1/live/room/query ---
echo "=== Step 3: POST /terraform/v1/live/room/query ==="
QUERY_RESP=$(curl -sS -m 5 -X POST "$ENDPOINT/terraform/v1/live/room/query" \
  -H "Authorization: Bearer $BEARER" -H 'Content-Type: application/json' \
  -d "{\"uuid\":\"$ROOM_UUID\"}")
QUERIED_STREAM=$(echo "$QUERY_RESP" | sed -n 's/.*"stream":"\([^"]*\)".*/\1/p')
QUERIED_SECRET=$(echo "$QUERY_RESP" | sed -n 's/.*"secret":"\([^"]*\)".*/\1/p')
if [[ "$QUERIED_STREAM" != "$ROOM_STREAM" || "$QUERIED_SECRET" != "$ROOM_SECRET" ]]; then
  echo "FAIL: query returned different stream/secret than create: $QUERY_RESP" >&2
  exit 1
fi
echo "PASS: query returns the same stream/secret as create."
echo ""

# --- Step 5: POST /terraform/v1/live/room/update (rename) ---
echo "=== Step 4: POST /terraform/v1/live/room/update ==="
NEW_TITLE="oryx-live-room-test-renamed-$(date +%s)"
# Update replaces the whole room record (see the TODO in live-room.go), so
# pass through the identifying fields we already have. Deliberately omit
# every aiXxx/assistant field -- we are not testing or asserting on them.
UPDATE_RESP=$(curl -sS -m 5 -X POST "$ENDPOINT/terraform/v1/live/room/update" \
  -H "Authorization: Bearer $BEARER" -H 'Content-Type: application/json' \
  -d "{\"uuid\":\"$ROOM_UUID\",\"title\":\"$NEW_TITLE\",\"stream\":\"$ROOM_STREAM\",\"secret\":\"$ROOM_SECRET\",\"roomToken\":\"$ROOM_TOKEN\",\"created_at\":\"$ROOM_CREATED\"}")
if ! echo "$UPDATE_RESP" | grep -q "\"title\":\"$NEW_TITLE\""; then
  echo "FAIL: update did not return the new title: $UPDATE_RESP" >&2
  exit 1
fi
REQUERY_RESP=$(curl -sS -m 5 -X POST "$ENDPOINT/terraform/v1/live/room/query" \
  -H "Authorization: Bearer $BEARER" -H 'Content-Type: application/json' \
  -d "{\"uuid\":\"$ROOM_UUID\"}")
if ! echo "$REQUERY_RESP" | grep -q "\"title\":\"$NEW_TITLE\""; then
  echo "FAIL: re-query did not reflect the renamed title: $REQUERY_RESP" >&2
  exit 1
fi
echo "PASS: room renamed and re-query reflects it."
echo ""

# --- Step 6: Publish with the room's own stream + secret ---
echo "=== Step 5: Publish via RTMP using the room's stream and secret ==="
ffmpeg -re -stream_loop -1 -i "$SOURCE_FLV" -c copy -f flv \
  "$SRS_RTMP/$APP/$ROOM_STREAM?secret=$ROOM_SECRET" >/tmp/oryx-live-room-ffmpeg.log 2>&1 &
FFMPEG_PID=$!
sleep 3
if ! kill -0 "$FFMPEG_PID" 2>/dev/null; then
  echo "FAIL: RTMP publisher exited early. Logs:" >&2
  cat /tmp/oryx-live-room-ffmpeg.log >&2
  exit 1
fi
check_srs_stream_published "$ROOM_STREAM"
probe_has_audio_video "RTMP" "$SRS_RTMP/$APP/$ROOM_STREAM"
kill -9 "$FFMPEG_PID" 2>/dev/null || true
wait "$FFMPEG_PID" 2>/dev/null || true
FFMPEG_PID=""
echo ""

# --- Step 7: POST /terraform/v1/live/room/remove ---
echo "=== Step 6: POST /terraform/v1/live/room/remove ==="
REMOVE_RESP=$(curl -sS -m 5 -X POST "$ENDPOINT/terraform/v1/live/room/remove" \
  -H "Authorization: Bearer $BEARER" -H 'Content-Type: application/json' \
  -d "{\"uuid\":\"$ROOM_UUID\"}")
if ! echo "$REMOVE_RESP" | grep -q '"code":0'; then
  echo "FAIL: remove did not return success: $REMOVE_RESP" >&2
  exit 1
fi
GONE_RESP=$(curl -sS -m 5 -X POST "$ENDPOINT/terraform/v1/live/room/query" \
  -H "Authorization: Bearer $BEARER" -H 'Content-Type: application/json' \
  -d "{\"uuid\":\"$ROOM_UUID\"}")
if echo "$GONE_RESP" | grep -q '"code":0'; then
  echo "FAIL: room still queryable after remove: $GONE_RESP" >&2
  exit 1
fi
echo "PASS: room removed and no longer queryable."
echo ""

echo "=== Oryx Live Room Test PASSED ==="
