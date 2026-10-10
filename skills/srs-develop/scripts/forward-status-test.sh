#!/bin/bash
# E2E test for the forward status API, GET /api/v1/forwards/. Starts two
# destination SRS and an origin whose forward.destination is both of them, from
# config files in /tmp/srs-forward-status, publishes doc/source.flv to the origin
# with FFmpeg, then checks on the origin:
#   - both forwards are listed under the stream, forwarding, with their urls;
#   - GET /api/v1/forwards/{id} returns one, an unknown id returns an error
#     code, and DELETE returns 405 and leaves the forward running;
#   - stopping a destination makes its forward retrying, the other one stays
#     forwarding;
#   - starting the destination again on the same port makes it forwarding again.
# forward.backend needs an HTTP server that answers POST; the srs-bench
# black-box cases cover it.
set -e

SCRIPT_DIR="$(cd -P "$(dirname "$0")" && pwd)"
# Walk up from SCRIPT_DIR looking for go.mod, because the skills directory may
# be reached via a symlink.
WORKSPACE="$SCRIPT_DIR"
while [[ "$WORKSPACE" != "/" && ! -f "$WORKSPACE/go.mod" ]]; do
  WORKSPACE="$(dirname "$WORKSPACE")"
done

if [[ ! -f "$WORKSPACE/go.mod" ]]; then
  echo "Error: go.mod not found walking up from: $SCRIPT_DIR" >&2
  exit 1
fi

# SRS_TEST_PORT_BASE, by default this script's slot in the Port Plan, moves every port,
# so the scripts can run in parallel.
PORT_BASE=${SRS_TEST_PORT_BASE:-12900}
ORIGIN_RTMP=$((PORT_BASE + 0))
ORIGIN_API=$((PORT_BASE + 1))
DST1_RTMP=$((PORT_BASE + 10))
DST1_API=$((PORT_BASE + 11))
DST2_RTMP=$((PORT_BASE + 20))
DST2_API=$((PORT_BASE + 21))

SOURCE_FLV="$WORKSPACE/trunk/doc/source.flv"
SRS_BINARY="$WORKSPACE/trunk/objs/srs"
TEST_DIR=/tmp/srs-forward-status
# Randomize per run so each invocation starts from clean state.
STREAM_NAME="forward$(date +%s)"
API="http://127.0.0.1:$ORIGIN_API"
DST1_URL="rtmp://127.0.0.1:$DST1_RTMP/live/$STREAM_NAME"
DST2_URL="rtmp://127.0.0.1:$DST2_RTMP/live/$STREAM_NAME"

# PIDs to clean up on exit.
ORIGIN_PID=""
DST1_PID=""
DST2_PID=""
FFMPEG_PID=""

cleanup() {
  echo ""
  echo "=== Cleaning up ==="
  for pid in $FFMPEG_PID $ORIGIN_PID $DST1_PID $DST2_PID; do
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
    fi
  done
  sleep 1
  for pid in $FFMPEG_PID $ORIGIN_PID $DST1_PID $DST2_PID; do
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
      kill -9 "$pid" 2>/dev/null || true
    fi
  done
  rm -rf /tmp/srs-forward-status
  echo "Cleanup done."
}
trap cleanup EXIT

fail() {
  echo "FAIL: $1" >&2
  for name in origin dst1 dst2; do
    echo "--- $name log:" >&2
    tail -20 "$TEST_DIR/$name/srs.log" "$TEST_DIR/$name/stdout.log" >&2 2>/dev/null || true
  done
  exit 1
}

# Write the config of one SRS: write_conf <name> <rtmp port> <api port> [vhost directive].
write_conf() {
  local name="$1"
  local rtmp="$2"
  local api="$3"
  local directive="$4"

  mkdir -p "$TEST_DIR/$name/objs/nginx/html"
  cat >"$TEST_DIR/$name/srs.conf" <<CONF
listen $rtmp;
pid $TEST_DIR/$name/srs.pid;
max_connections 1000;
daemon off;
srs_log_tank file;
srs_log_file $TEST_DIR/$name/srs.log;

http_api {
  enabled on;
  listen $api;
}

vhost __defaultVhost__ {
  $directive
}
CONF
}

# Start one SRS with its config file, and set STARTED_PID: start_srs <name> <api port>.
start_srs() {
  local name="$1"
  local api="$2"

  # Clear every SRS_* variable, so only the config file configures SRS.
  (
    for var in $(env | sed -n 's/^\(SRS_[A-Za-z0-9_]*\)=.*/\1/p'); do
      unset "$var"
    done
    cd "$TEST_DIR/$name"
    exec "$SRS_BINARY" -c "$TEST_DIR/$name/srs.conf" >"$TEST_DIR/$name/stdout.log" 2>&1
  ) &
  STARTED_PID=$!

  for ((i = 1; i <= 15; i++)); do
    if curl -fsS "http://127.0.0.1:$api/api/v1/versions" >/dev/null 2>&1; then
      echo "SRS $name is up, PID $STARTED_PID, API port $api."
      return 0
    fi
    if ! kill -0 "$STARTED_PID" 2>/dev/null; then
      fail "SRS $name exited during startup."
    fi
    sleep 1
  done
  fail "SRS $name HTTP API not ready on port $api."
}

# Stop one SRS and wait until it quits: stop_srs <pid>.
stop_srs() {
  local pid="$1"
  kill "$pid" 2>/dev/null || true
  for ((i = 1; i <= 10; i++)); do
    if ! kill -0 "$pid" 2>/dev/null; then
      return 0
    fi
    sleep 1
  done
  kill -9 "$pid" 2>/dev/null || true
}

# The forwards of the stream on the origin, as JSON.
list_forwards() {
  curl -fsS "$API/api/v1/forwards/?stream=$STREAM_ID" 2>/dev/null || true
}

# The state of the forward to the url, or empty.
forward_state() {
  list_forwards | jq -r --arg url "$1" '.forwards[]? | select(.url == $url) | .state' 2>/dev/null || true
}

# Wait until the forward to the url is in the state: wait_state <url> <state> <seconds>.
wait_state() {
  local url="$1"
  local state="$2"
  local timeout="$3"
  for ((i = 1; i <= timeout * 5; i++)); do
    if [[ "$(forward_state "$url")" == "$state" ]]; then
      echo "PASS: forward to $url is $state."
      return 0
    fi
    sleep 0.2
  done
  echo "Forwards: $(list_forwards)" >&2
  fail "forward to $url is not $state in ${timeout}s."
}

echo "=== E2E SRS Forward Status Test ==="
echo "Workspace: $WORKSPACE"
echo "Stream: live/$STREAM_NAME, origin RTMP $ORIGIN_RTMP, destinations RTMP $DST1_RTMP and $DST2_RTMP"
echo "Test dir: $TEST_DIR"
echo ""

# --- Pre-checks ---
if [[ ! -f "$SOURCE_FLV" ]]; then
  echo "Error: test source not found: $SOURCE_FLV" >&2
  exit 1
fi
if [[ ! -x "$SRS_BINARY" ]]; then
  echo "Error: no $SRS_BINARY, build it first." >&2
  exit 1
fi
for cmd in curl jq ffmpeg lsof; do
  if ! command -v "$cmd" &>/dev/null; then
    echo "Error: $cmd not found in PATH" >&2
    exit 1
  fi
done

# --- Step 0: Clean up stale state ---
for port in $ORIGIN_RTMP $ORIGIN_API $DST1_RTMP $DST1_API $DST2_RTMP $DST2_API; do
  # Kill only listeners: a client of the port is not ours.
  lsof -ti TCP:"$port" -sTCP:LISTEN 2>/dev/null | xargs kill 2>/dev/null || true
done
rm -rf /tmp/srs-forward-status
sleep 1

# --- Step 1: Start the destinations and the origin from config files ---
echo "=== Step 1: Starting two destinations and the origin ==="
ulimit -n 10000 2>/dev/null || true
write_conf dst1 "$DST1_RTMP" "$DST1_API"
write_conf dst2 "$DST2_RTMP" "$DST2_API"
write_conf origin "$ORIGIN_RTMP" "$ORIGIN_API" \
  "forward { enabled on; destination 127.0.0.1:$DST1_RTMP 127.0.0.1:$DST2_RTMP; }"
start_srs dst1 "$DST1_API"
DST1_PID=$STARTED_PID
start_srs dst2 "$DST2_API"
DST2_PID=$STARTED_PID
start_srs origin "$ORIGIN_API"
ORIGIN_PID=$STARTED_PID

# --- Step 2: Publish to the origin ---
echo "=== Step 2: Publishing live/$STREAM_NAME to the origin ==="
ffmpeg -stream_loop -1 -re -i "$SOURCE_FLV" -c copy -f flv \
  "rtmp://127.0.0.1:$ORIGIN_RTMP/live/$STREAM_NAME" >"$TEST_DIR/ffmpeg.log" 2>&1 &
FFMPEG_PID=$!
echo "FFmpeg publisher PID: $FFMPEG_PID"

STREAM_ID=""
for ((i = 1; i <= 15; i++)); do
  STREAM_ID=$(curl -fsS "$API/api/v1/streams/?count=100" 2>/dev/null |
    jq -r --arg name "$STREAM_NAME" '.streams[]? | select(.name == $name and .publish.active == true) | .id' 2>/dev/null || true)
  if [[ -n "$STREAM_ID" ]]; then
    break
  fi
  if ! kill -0 "$FFMPEG_PID" 2>/dev/null; then
    cat "$TEST_DIR/ffmpeg.log" >&2
    fail "FFmpeg publisher failed."
  fi
  sleep 1
done
[[ -n "$STREAM_ID" ]] || fail "the origin does not publish live/$STREAM_NAME."
echo "PASS: the origin publishes live/$STREAM_NAME as stream $STREAM_ID."

# --- Step 3: Both forwards are listed, forwarding, with their urls ---
echo "=== Step 3: Verifying both forwards are listed and forwarding ==="
wait_state "$DST1_URL" forwarding 15
wait_state "$DST2_URL" forwarding 15

FORWARDS=$(list_forwards)
if [[ "$(echo "$FORWARDS" | jq '.code')" == "0" && "$(echo "$FORWARDS" | jq '.forwards | length')" == "2" ]]; then
  echo "PASS: the stream has exactly two forwards."
else
  echo "Forwards: $FORWARDS" >&2
  fail "the stream does not have exactly two forwards."
fi
KEYS=$(echo "$FORWARDS" | jq -c '[.forwards[] | keys] | unique')
if [[ "$KEYS" == '[["app","id","name","state","state_at","stream","url","vhost"]]' ]]; then
  echo "PASS: each forward has exactly id, stream, vhost, app, name, url, state and state_at."
else
  fail "forward fields are $KEYS."
fi
if echo "$FORWARDS" | jq -e --arg sid "$STREAM_ID" --arg name "$STREAM_NAME" \
  'all(.forwards[]; .stream == $sid and .app == "live" and .name == $name and .vhost != "" and .state_at > 0)' >/dev/null; then
  echo "PASS: both forwards belong to stream $STREAM_ID, live/$STREAM_NAME."
else
  echo "Forwards: $FORWARDS" >&2
  fail "the forwards do not belong to stream $STREAM_ID."
fi

# --- Step 4: One forward by id, an unknown id, and DELETE ---
echo "=== Step 4: Verifying /api/v1/forwards/{id} ==="
ID1=$(echo "$FORWARDS" | jq -r --arg url "$DST1_URL" '.forwards[] | select(.url == $url) | .id')
ONE=$(curl -fsS "$API/api/v1/forwards/$ID1" || true)
if [[ -n "$ID1" ]] && echo "$ONE" | jq -e --arg id "$ID1" --arg url "$DST1_URL" \
  '.code == 0 and .forward.id == $id and .forward.url == $url and .forward.state == "forwarding"' >/dev/null; then
  echo "PASS: /api/v1/forwards/$ID1 returns the forward to $DST1_URL."
else
  echo "Answer: $ONE" >&2
  fail "/api/v1/forwards/$ID1 does not return the forward to $DST1_URL."
fi

UNKNOWN=$(curl -sS "$API/api/v1/forwards/fwd-unknown" || true)
if echo "$UNKNOWN" | jq -e '.code == 2057' >/dev/null 2>&1; then
  echo "PASS: an unknown id returns code 2057 (forward not found)."
else
  fail "an unknown id returns $UNKNOWN, want code 2057."
fi

STATUS=$(curl -sS -o /dev/null -w '%{http_code}' -X DELETE "$API/api/v1/forwards/$ID1" || true)
if [[ "$STATUS" == "405" ]]; then
  echo "PASS: DELETE /api/v1/forwards/$ID1 returns 405."
else
  fail "DELETE /api/v1/forwards/$ID1 returns $STATUS, want 405."
fi
wait_state "$DST1_URL" forwarding 1

# --- Step 5: Stop a destination ---
echo "=== Step 5: Stopping destination dst1 ==="
stop_srs "$DST1_PID"
DST1_PID=""
wait_state "$DST1_URL" retrying 15
if [[ "$(forward_state "$DST2_URL")" == "forwarding" ]]; then
  echo "PASS: the forward to $DST2_URL stays forwarding."
else
  fail "the forward to $DST2_URL is $(forward_state "$DST2_URL"), want forwarding."
fi

# --- Step 6: Start the destination again on the same port ---
echo "=== Step 6: Starting destination dst1 again ==="
start_srs dst1 "$DST1_API"
DST1_PID=$STARTED_PID
wait_state "$DST1_URL" forwarding 15
ID1_AGAIN=$(list_forwards | jq -r --arg url "$DST1_URL" '.forwards[] | select(.url == $url) | .id')
if [[ "$ID1_AGAIN" == "$ID1" ]]; then
  echo "PASS: the forward keeps its id $ID1."
else
  fail "the forward id is $ID1_AGAIN, want $ID1."
fi

echo ""
echo "=== E2E SRS Forward Status Test PASSED ==="
