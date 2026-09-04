#!/bin/bash
# E2E test for the Oryx "Forward" scenario page (tab=forward, ScenarioForward.js):
# configure a self-loop forward. Publishes one source stream to this Oryx
# instance, creates a Forward "custom" platform config pointing back at this
# same instance, and verifies the forwarded copy actually shows up and plays.
#
# Protocol choice, worked out from oryx/platform/forward.go:
#   - The source's publish protocol is irrelevant to Forward: doForward()
#     always re-pulls the input over RTMP internally (selectActiveStream +
#     "rtmp://localhost/<app>/<stream>"), regardless of how the stream was
#     originally published. So this script publishes the source via plain
#     RTMP -- the simplest option, needing no extra ffmpeg build.
#   - The forward *target* protocol does change the code path: doForward()
#     passes "-f flv" for an rtmp(s):// target and "-pes_payload_size 0 -f
#     mpegts" for an srt:// target. WHIP is not a supported forward target
#     (no branch for it) -- ffmpeg's WHIP muxer needs a re-encode, and
#     doForward always uses "-c copy". So this script covers both an RTMP
#     forward target and an SRT forward target, and does not attempt WHIP.
#
# There is no "remove" action for /terraform/v1/ffmpeg/forward/secret (only
# "update"), matching the product's own design: forward platforms are meant
# to be toggled on/off, not deleted. So this script uses two fixed platform
# keys (forwarding-99-oryx-test-rtmp/-srt) instead of a timestamped one --
# every run updates those same two entries in place and disables them when
# done, instead of leaving a new permanent "Inactive" entry in Redis and the
# dashboard's Live Status list on every run.
#
# Requires the shared local stack (Redis, SRS, Oryx Go backend) to already be
# running -- start it once with oryx-stack-start.sh. This script only starts
# its own ffmpeg publisher and cleans that up; it does not manage server
# lifecycle, so it is safe to run concurrently with other oryx-*-test.sh
# scripts against that same shared stack. Because Forward can pick "the most
# recently active stream" when no stream filter is set, this script always
# pins config.stream to its own source stream name, so it never accidentally
# forwards a stream published by a sibling test running at the same time.

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
SRS_SRT="${SRS_SRT_ENDPOINT:-srt://localhost:10080}"
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
  echo "Forward task status:" >&2
  curl -sS -m 3 -X POST "$ENDPOINT/terraform/v1/ffmpeg/forward/streams" \
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

# Set (or update) one forward platform config and enable it.
set_forward_config() {
  local platform="$1" stream="$2" server="$3" secret="$4"
  curl -sS -m 5 -X POST "$ENDPOINT/terraform/v1/ffmpeg/forward/secret" \
    -H "Authorization: Bearer $BEARER" -H 'Content-Type: application/json' \
    -d "{\"action\":\"update\",\"platform\":\"$platform\",\"stream\":\"$stream\",\"server\":\"$server\",\"secret\":\"$secret\",\"enabled\":true,\"custom\":true,\"label\":\"oryx-forward-test\"}"
}

# Disable (not delete -- there is no delete action) a forward platform config.
disable_forward_config() {
  local platform="$1" stream="$2" server="$3" secret="$4"
  curl -sS -m 5 -X POST "$ENDPOINT/terraform/v1/ffmpeg/forward/secret" \
    -H "Authorization: Bearer $BEARER" -H 'Content-Type: application/json' \
    -d "{\"action\":\"update\",\"platform\":\"$platform\",\"stream\":\"$stream\",\"server\":\"$server\",\"secret\":\"$secret\",\"enabled\":false,\"custom\":true,\"label\":\"oryx-forward-test\"}" \
    >/dev/null 2>&1 || true
}

echo "=== Oryx Forward Test ==="
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

# --- Step 2: Publish the source stream via RTMP (see header for why RTMP) ---
echo "=== Step 1: Publish source stream via RTMP ==="
SOURCE_STREAM="fwdtest-src-$(date +%s)"
ffmpeg -re -stream_loop -1 -i "$SOURCE_FLV" -c copy -f flv \
  "$SRS_RTMP/$APP/$SOURCE_STREAM?secret=$PUBLISH_SECRET" >/tmp/oryx-forward-source.log 2>&1 &
FFMPEG_PID=$!
sleep 3
if ! kill -0 "$FFMPEG_PID" 2>/dev/null; then
  echo "FAIL: source publisher exited early. Logs:" >&2
  cat /tmp/oryx-forward-source.log >&2
  exit 1
fi
check_srs_stream_published "$SOURCE_STREAM" 15
echo ""

RUN_ID="$(date +%s)"

# Fixed platform keys, reused across runs: there is no delete action for
# forward/secret (only "update"), so a timestamped platform key here would
# accumulate a permanent, growing "Inactive" entry in Redis and the
# dashboard's Live Status list every time this script runs. Reusing the same
# two keys means re-runs update these two entries in place instead. Verified
# safe for repeated sequential runs (one full run, then another). The
# tradeoff: two OVERLAPPING invocations of this script would race on these
# same keys -- the second one's config update triggers Restart() on the
# backend's shared in-memory task, cancelling the first one's in-flight
# ffmpeg process. Do not run this script concurrently with itself.
PLATFORM_RTMP="forwarding-99-oryx-test-rtmp"
PLATFORM_SRT="forwarding-99-oryx-test-srt"

# --- Step 3: Forward to an RTMP target (back at this same Oryx instance) ---
echo "=== Step 2: Forward source to an RTMP target ==="
TARGET_STREAM_RTMP="fwdtest-dst-rtmp-$RUN_ID"
set_forward_config "$PLATFORM_RTMP" "$SOURCE_STREAM" "$SRS_RTMP/$APP" \
  "$TARGET_STREAM_RTMP?secret=$PUBLISH_SECRET" >/dev/null
echo "Forward config set: $PLATFORM_RTMP -> $SRS_RTMP/$APP/$TARGET_STREAM_RTMP"
check_srs_stream_published "$TARGET_STREAM_RTMP" 30
probe_has_audio_video "RTMP forward target" "$SRS_RTMP/$APP/$TARGET_STREAM_RTMP"
echo ""

# --- Step 4: Forward to an SRT target (back at this same Oryx instance) ---
echo "=== Step 3: Forward source to an SRT target ==="
TARGET_STREAM_SRT="fwdtest-dst-srt-$RUN_ID"
set_forward_config "$PLATFORM_SRT" "$SOURCE_STREAM" "$SRS_SRT?streamid=#!::r=$APP/" \
  "$TARGET_STREAM_SRT?secret=$PUBLISH_SECRET,m=publish" >/dev/null
echo "Forward config set: $PLATFORM_SRT -> $SRS_SRT (target stream $TARGET_STREAM_SRT)"
check_srs_stream_published "$TARGET_STREAM_SRT" 30
# The target was published over SRT by the backend's own ffmpeg; SRS still
# serves it over RTMP too (srt_to_rtmp), so plain RTMP playback confirms it.
probe_has_audio_video "SRT forward target (via RTMP playback)" "$SRS_RTMP/$APP/$TARGET_STREAM_SRT"
echo ""

# --- Step 5: Disable the forward configs and stop the source publisher ---
echo "=== Step 4: Disable forward configs ==="
disable_forward_config "$PLATFORM_RTMP" "$SOURCE_STREAM" "$SRS_RTMP/$APP" \
  "$TARGET_STREAM_RTMP?secret=$PUBLISH_SECRET"
disable_forward_config "$PLATFORM_SRT" "$SOURCE_STREAM" "$SRS_SRT?streamid=#!::r=$APP/" \
  "$TARGET_STREAM_SRT?secret=$PUBLISH_SECRET,m=publish"
echo "PASS: forward configs disabled (no delete API; this is the product's own lifecycle)."

kill -9 "$FFMPEG_PID" 2>/dev/null || true
wait "$FFMPEG_PID" 2>/dev/null || true
FFMPEG_PID=""
echo ""

echo "=== Oryx Forward Test PASSED ==="
