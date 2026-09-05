#!/bin/bash
# E2E test for the Oryx "Record" scenario page (tab=record, ScenarioRecord.js):
# publish a stream, enable recording, wait for it to actually record a
# segment, force-end the recording early, and verify the resulting MP4 is a
# real playable file.
#
# Recording only fires from SRS's on_hls webhook, gated by the *global*
# SRS_RECORD_PATTERNS "all" flag (see handleOnHls in srs-hooks.go) -- there is
# no per-stream "record this one" switch. Glob filters only narrow which
# streams get recorded *after* "all" is already true (see buildM3u8Object in
# dvr-local-disk.go); they cannot substitute for it. So this script must set
# "all":true to record anything, but immediately scopes it down with a glob
# matching only this script's own stream prefix, so it does not start
# recording streams published by sibling oryx-*-test.sh scripts running at
# the same time. It restores "all":false and clears the globs when done.
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
MP4_FILE=""

FFMPEG_PID=""
cleanup() {
  echo ""
  echo "=== Cleaning up ==="
  if [[ -n "$FFMPEG_PID" ]]; then
    kill -9 "$FFMPEG_PID" 2>/dev/null || true
  fi
  if [[ -n "$MP4_FILE" ]]; then
    rm -f "$MP4_FILE"
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

# Poll /terraform/v1/hooks/record/files for the entry matching $1 (stream
# name) whose "progress" boolean equals $2 ("true" or "false"), for up to
# $3 seconds. Echoes the entry's uuid and returns 0 on match, else fails.
find_record_entry() {
  local stream="$1" want_progress="$2" deadline="$3"
  local resp uuid progress
  # This function's stdout is captured via $(...) by callers to get the
  # uuid, so every diagnostic line here must go to stderr instead.
  echo "Waiting for record/files entry: stream=$stream progress=$want_progress (up to ${deadline}s)" >&2
  for ((i = 1; i <= deadline; i++)); do
    resp=$(curl -sS -m 5 -X POST "$ENDPOINT/terraform/v1/hooks/record/files" \
      -H "Authorization: Bearer $BEARER" -H 'Content-Type: application/json' -d '{}')
    uuid=$(echo "$resp" | jq -r --arg s "$stream" '[.data[] | select(.stream==$s)][0].uuid // empty')
    # Do NOT use "// empty" on progress: jq's // treats a literal `false`
    # boolean as falsy too, which would silently discard a real "not in
    # progress" result. Guard on uuid (a string) being non-empty instead.
    progress=""
    if [[ -n "$uuid" ]]; then
      progress=$(echo "$resp" | jq -r --arg s "$stream" '[.data[] | select(.stream==$s)][0].progress')
    fi
    if [[ -n "$uuid" && "$progress" == "$want_progress" ]]; then
      echo "$uuid"
      return 0
    fi
    sleep 1
  done
  echo "FAIL: no record/files entry for stream=$stream with progress=$want_progress within ${deadline}s." >&2
  echo "Last response: $resp" >&2
  return 1
}

echo "=== Oryx Record Test ==="
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
  echo "FAIL: jq not found in PATH (needed to pick this test's own entry out of record/files)." >&2
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

SECRET_RESP=$(curl -sS -m 5 -X POST "$ENDPOINT/terraform/v1/hooks/srs/secret/query" \
  -H "Authorization: Bearer $BEARER" -H 'Content-Type: application/json' -d '{}')
PUBLISH_SECRET=$(echo "$SECRET_RESP" | sed -n 's/.*"publish":"\([^"]*\)".*/\1/p')
if [[ -z "$PUBLISH_SECRET" ]]; then
  echo "FAIL: secret/query did not return a publish secret: $SECRET_RESP" >&2
  exit 1
fi

# --- Step 2: Enable recording, scoped to only this test's own stream ---
echo "=== Step 1: Enable recording (scoped to this test's stream) ==="
SOURCE_STREAM="rectest-$(date +%s)"
curl -sS -m 5 -X POST "$ENDPOINT/terraform/v1/hooks/record/apply" \
  -H "Authorization: Bearer $BEARER" -H 'Content-Type: application/json' \
  -d '{"all":true}' >/dev/null
curl -sS -m 5 -X POST "$ENDPOINT/terraform/v1/hooks/record/globs" \
  -H "Authorization: Bearer $BEARER" -H 'Content-Type: application/json' \
  -d "{\"globs\":[\"/$APP/rectest-*\"]}" >/dev/null
echo "PASS: recording enabled, scoped to /$APP/rectest-*"
echo ""

# From here on, always try to restore global record settings, even on failure.
restore_record_settings() {
  curl -sS -m 5 -X POST "$ENDPOINT/terraform/v1/hooks/record/apply" \
    -H "Authorization: Bearer $BEARER" -H 'Content-Type: application/json' \
    -d '{"all":false}' >/dev/null 2>&1 || true
  curl -sS -m 5 -X POST "$ENDPOINT/terraform/v1/hooks/record/globs" \
    -H "Authorization: Bearer $BEARER" -H 'Content-Type: application/json' \
    -d '{"globs":[]}' >/dev/null 2>&1 || true
}
trap 'restore_record_settings; cleanup' EXIT

# --- Step 3: Publish the source stream via RTMP long enough for one HLS segment ---
echo "=== Step 2: Publish source stream via RTMP ==="
ffmpeg -re -stream_loop -1 -i "$SOURCE_FLV" -c copy -f flv \
  "$SRS_RTMP/$APP/$SOURCE_STREAM?secret=$PUBLISH_SECRET" >/tmp/oryx-record-source.log 2>&1 &
FFMPEG_PID=$!
sleep 3
if ! kill -0 "$FFMPEG_PID" 2>/dev/null; then
  echo "FAIL: source publisher exited early. Logs:" >&2
  cat /tmp/oryx-record-source.log >&2
  exit 1
fi
check_srs_stream_published "$SOURCE_STREAM" 15
echo ""

# hls_fragment is 10s (see oryx/platform/containers/data/config/srs.vhost.conf);
# wait past one full fragment so at least one TS segment is generated and fed
# to the record worker via on_hls.
echo "=== Step 3: Wait for recording to pick up at least one segment ==="
sleep 13
RECORD_UUID=$(find_record_entry "$SOURCE_STREAM" "true" 30) || exit 1
echo "PASS: recording in progress, uuid=$RECORD_UUID"
echo ""

# --- Step 4: Stop the source, then force-end the recording ---
echo "=== Step 4: Stop source, end the recording ==="
kill -9 "$FFMPEG_PID" 2>/dev/null || true
wait "$FFMPEG_PID" 2>/dev/null || true
FFMPEG_PID=""

curl -sS -m 5 -X POST "$ENDPOINT/terraform/v1/hooks/record/end" \
  -H "Authorization: Bearer $BEARER" -H 'Content-Type: application/json' \
  -d "{\"uuid\":\"$RECORD_UUID\"}" >/dev/null
find_record_entry "$SOURCE_STREAM" "false" 20 >/dev/null || exit 1
echo "PASS: recording finalized."
echo ""

# --- Step 5: Verify the recorded MP4 is a real, playable file ---
echo "=== Step 5: Verify the recorded MP4 file ==="
MP4_FILE="/tmp/oryx-record-test-$RECORD_UUID.mp4"
if ! curl -sS -m 15 -o "$MP4_FILE" "$ENDPOINT/terraform/v1/hooks/record/hls/$RECORD_UUID/index.mp4"; then
  echo "FAIL: could not download the recorded MP4." >&2
  exit 1
fi
if [[ ! -s "$MP4_FILE" ]]; then
  echo "FAIL: recorded MP4 is empty or missing: $MP4_FILE" >&2
  exit 1
fi
PROBE_OUTPUT=$(ffprobe -v error -show_streams "$MP4_FILE" 2>&1 || true)
if echo "$PROBE_OUTPUT" | grep -q "codec_type=video"; then
  echo "PASS: recorded MP4 has a video stream."
else
  echo "FAIL: recorded MP4 has no video stream." >&2
  echo "$PROBE_OUTPUT" >&2
  exit 1
fi
if echo "$PROBE_OUTPUT" | grep -q "codec_type=audio"; then
  echo "PASS: recorded MP4 has an audio stream."
else
  echo "FAIL: recorded MP4 has no audio stream." >&2
  echo "$PROBE_OUTPUT" >&2
  exit 1
fi
echo ""

# --- Step 6: Remove the recording ---
echo "=== Step 6: POST /terraform/v1/hooks/record/remove ==="
curl -sS -m 5 -X POST "$ENDPOINT/terraform/v1/hooks/record/remove" \
  -H "Authorization: Bearer $BEARER" -H 'Content-Type: application/json' \
  -d "{\"uuid\":\"$RECORD_UUID\"}" >/dev/null
echo "PASS: recording removed."
echo ""

echo "=== Oryx Record Test PASSED ==="
