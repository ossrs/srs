#!/bin/bash
# E2E test for reloading the config of a running SRS, the path unit tests
# cannot reach: a real config file re-read from disk, the SIGHUP signal, the
# RAW API over HTTP, the server cycle that applies the reload, and live streams
# that must survive it. Starts SRS from a config file with a live RTMP
# publisher, then:
#   - checks the reload status before any reload (rpc=reload-fetch);
#   - reloads through the RAW API (rpc=reload) and through SIGHUP, and verifies
#     the new file was applied: rpc=raw reports the new value and the
#     chunk_size change reaches the subscribers;
#   - reloads a file that fails to parse and one that fails check_config, and
#     verifies each failure state, that the old config is kept, and that the
#     server and the stream survive;
#   - recovers with a valid file;
#   - reloads a file that turns allow_reload off, and verifies rpc=reload is
#     then refused, until SIGHUP reloads a file that turns it back on, and
#     the same for raw_api.enabled and rpc=reload-fetch;
#   - reloads an SRS started with -e, which has no file to re-read.
# Every reload is awaited by polling rpc=reload-fetch until its id changes,
# because the RAW API only signals the server, which reloads in its next cycle.
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

RTMP_PORT=19371
HTTP_API_PORT=19871
HTTP_SERVER_PORT=18071

SOURCE_FLV="$WORKSPACE/trunk/doc/source.flv"
SRS_BINARY="$WORKSPACE/trunk/objs/srs"
# Randomize per run so each invocation starts from clean state.
STREAM_NAME="reload$(date +%s)"
STREAM_URL="live/$STREAM_NAME"
API="http://127.0.0.1:$HTTP_API_PORT"
RAW="$API/api/v1/raw"

TEST_DIR=$(mktemp -d "${TMPDIR:-/tmp}/srs-reload.XXXXXX")
SRS_CONF="$TEST_DIR/srs.conf"
SRS_PID_FILE="$TEST_DIR/srs.pid"
SRS_LOG_FILE="$TEST_DIR/srs.log"
SRS_STDOUT="$TEST_DIR/stdout.log"
ENV_STDOUT="$TEST_DIR/stdout-env.log"
FFMPEG_LOG="$TEST_DIR/ffmpeg.log"

# PIDs to clean up on exit.
SRS_PID=""
FFMPEG_PID=""

cleanup() {
  echo ""
  echo "=== Cleaning up ==="
  for pid in $SRS_PID $FFMPEG_PID; do
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
    fi
  done
  sleep 1
  for pid in $SRS_PID $FFMPEG_PID; do
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
  tail -50 "$SRS_LOG_FILE" "$SRS_STDOUT" "$ENV_STDOUT" >&2 2>/dev/null || true
  exit 1
}

probe_has_audio_video() {
  local name="$1"
  local url="$2"

  echo "Verifying $name playback: $url"
  local output
  output=$(ffprobe -v error -show_streams "$url" 2>&1 || true)

  if echo "$output" | grep -q "codec_type=video" && echo "$output" | grep -q "codec_type=audio"; then
    echo "PASS: $name audio and video streams detected."
  else
    echo "ffprobe output:" >&2
    echo "$output" >&2
    fail "$name playback has no audio and video."
  fi
}

# Write the config file. $1 is http_api.crossdomain, which rpc=raw reports from
# the live config, so a changed value proves a reload applied the new file.
# $2 is the vhost chunk_size, whose change is notified to the subscribers.
# $3 is appended verbatim, to break the file on purpose. $4 is
# raw_api.allow_reload and $5 raw_api.enabled, both on by default.
write_conf() {
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
  crossdomain $1;
  raw_api {
    enabled ${5:-on};
    allow_reload ${4:-on};
  }
}

http_server {
  enabled on;
  listen $HTTP_SERVER_PORT;
  dir $TEST_DIR;
}

vhost __defaultVhost__ {
  chunk_size $2;
  http_remux {
    enabled on;
    mount [vhost]/[app]/[stream].flv;
  }
}
$3
CONF
}

fetch() {
  curl -fsS "$RAW?rpc=reload-fetch" || fail "rpc=reload-fetch request failed."
}

fetch_field() {
  fetch | jq -r ".data.$1"
}

# Wait until the reload id differs from $1, then print the reload status.
wait_reload() {
  local old="$1" rid=""
  for ((i = 1; i <= 20; i++)); do
    rid=$(fetch_field rid)
    if [[ -n "$rid" && "$rid" != "$old" ]]; then
      fetch
      return 0
    fi
    sleep 0.5
  done
  fail "no reload happened in 10s; rid is still $old."
}

# Check a reload status JSON: $1 status, $2 name, $3 want state, $4 want err.
expect_status() {
  local status="$1" name="$2" state err rid
  state=$(echo "$status" | jq -r .data.state)
  err=$(echo "$status" | jq -r .data.err)
  rid=$(echo "$status" | jq -r .data.rid)
  if [[ "$state" == "$3" && "$err" == "$4" && ${#rid} -eq 7 ]]; then
    echo "PASS: $name: state=$state err=$err rid=$rid."
  else
    echo "Status: $status" >&2
    fail "$name: want state=$3 err=$4 and a 7-character rid."
  fi
}

expect_msg() {
  local status="$1" name="$2" want="$3"
  if echo "$status" | jq -r .data.msg | grep -q "$want"; then
    echo "PASS: $name: msg contains '$want'."
  else
    echo "Status: $status" >&2
    fail "$name: msg lacks '$want'."
  fi
}

expect_crossdomain() {
  local name="$1" want="$2" got
  got=$(curl -fsS "$RAW?rpc=raw" | jq -r .http_api.crossdomain)
  if [[ "$got" == "$want" ]]; then
    echo "PASS: $name: rpc=raw reports crossdomain=$got."
  else
    fail "$name: rpc=raw reports crossdomain=$got, want $want."
  fi
}

# The publisher must still be the same client, so the reload did not drop it.
expect_publisher_kept() {
  local name="$1" cid
  cid=$(curl -fsS "$API/api/v1/streams/" | jq -r --arg name "$STREAM_NAME" \
    '.streams[] | select(.name == $name and .publish.active == true) | .publish.cid')
  if [[ -n "$cid" && "$cid" == "$PUBLISHER_CID" ]]; then
    echo "PASS: $name: publisher $cid still active."
  else
    fail "$name: publisher is '$cid', want $PUBLISHER_CID still active."
  fi
  if ! kill -0 "$FFMPEG_PID" 2>/dev/null; then
    cat "$FFMPEG_LOG" >&2
    fail "$name: FFmpeg publisher exited."
  fi
}

wait_api() {
  local pid="$1"
  for ((i = 1; i <= 15; i++)); do
    if curl -fsS "$API/api/v1/versions" >/dev/null 2>&1; then
      return 0
    fi
    if ! kill -0 "$pid" 2>/dev/null; then
      fail "SRS exited during startup."
    fi
    sleep 1
  done
  fail "HTTP API not ready on port $HTTP_API_PORT."
}

# Stop SRS with SIGTERM and require a clean exit with no sanitizer report.
stop_srs() {
  local name="$1" out="$2"
  kill "$SRS_PID" 2>/dev/null || true
  for ((i = 1; i <= 15; i++)); do
    if ! kill -0 "$SRS_PID" 2>/dev/null; then
      break
    fi
    sleep 1
  done
  if kill -0 "$SRS_PID" 2>/dev/null; then
    fail "$name: SRS did not exit on SIGTERM."
  fi
  if grep -q "AddressSanitizer" "$out"; then
    fail "$name: sanitizer report in $out."
  fi
  echo "PASS: $name: SRS exited cleanly."
  SRS_PID=""
}

echo "=== E2E SRS Reload Test ==="
echo "Workspace: $WORKSPACE"
echo "Stream: $STREAM_URL"
echo "Test dir: $TEST_DIR"
echo ""

# --- Pre-checks ---
if [[ ! -f "$SOURCE_FLV" ]]; then
  echo "Error: test source not found: $SOURCE_FLV" >&2
  exit 1
fi
for cmd in curl jq ffmpeg ffprobe; do
  if ! command -v "$cmd" &>/dev/null; then
    echo "Error: $cmd not found in PATH" >&2
    exit 1
  fi
done

# --- Step 0: Clean up stale state ---
for port in $RTMP_PORT $HTTP_API_PORT $HTTP_SERVER_PORT; do
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

# --- Step 2: Start SRS with a config file ---
echo "=== Step 2: Starting SRS with -c $SRS_CONF ==="
write_conf on 60000 ""
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
wait_api "$SRS_PID"
echo "PASS: HTTP API is up."

# --- Step 3: The reload status before any reload ---
echo "=== Step 3: Reload status before any reload ==="
STATUS=$(fetch)
expect_status "$STATUS" "before reload" 0 0
expect_msg "$STATUS" "before reload" "Success"
expect_crossdomain "before reload" true

# --- Step 4: Publish RTMP stream ---
echo "=== Step 4: Publishing RTMP stream ==="
ffmpeg -stream_loop -1 -re -i "$SOURCE_FLV" -c copy -f flv \
  "rtmp://127.0.0.1:$RTMP_PORT/$STREAM_URL" >"$FFMPEG_LOG" 2>&1 &
FFMPEG_PID=$!
echo "FFmpeg publisher PID: $FFMPEG_PID"
sleep 5
if ! kill -0 "$FFMPEG_PID" 2>/dev/null; then
  cat "$FFMPEG_LOG" >&2
  fail "FFmpeg publisher failed."
fi
PUBLISHER_CID=$(curl -fsS "$API/api/v1/streams/" | jq -r --arg name "$STREAM_NAME" \
  '.streams[] | select(.name == $name and .publish.active == true) | .publish.cid')
[[ -n "$PUBLISHER_CID" ]] || fail "HTTP API does not list the active stream $STREAM_NAME."
echo "PASS: publisher $PUBLISHER_CID active."

# --- Step 5: Reload through the RAW API ---
echo "=== Step 5: Reload through the RAW API ==="
OLD_RID=$(fetch_field rid)
write_conf off 4096 ""
RESPONSE=$(curl -fsS "$RAW?rpc=reload")
[[ "$(echo "$RESPONSE" | jq -r .code)" == "0" ]] || fail "rpc=reload answered $RESPONSE."
STATUS=$(wait_reload "$OLD_RID")
expect_status "$STATUS" "API reload" 90 0
expect_msg "$STATUS" "API reload" "Success"
expect_crossdomain "API reload" false
if grep -q "vhost __defaultVhost__ reload chunk_size success" "$SRS_LOG_FILE"; then
  echo "PASS: API reload: chunk_size change notified to the subscribers."
else
  fail "API reload: no chunk_size reload in the log."
fi
expect_publisher_kept "API reload"
probe_has_audio_video "RTMP after API reload" "rtmp://127.0.0.1:$RTMP_PORT/$STREAM_URL"
probe_has_audio_video "HTTP-FLV after API reload" "http://127.0.0.1:$HTTP_SERVER_PORT/$STREAM_URL.flv"

# --- Step 6: Reload through SIGHUP ---
echo "=== Step 6: Reload through SIGHUP ==="
OLD_RID=$(fetch_field rid)
write_conf on 4096 ""
kill -HUP "$SRS_PID"
STATUS=$(wait_reload "$OLD_RID")
expect_status "$STATUS" "SIGHUP reload" 90 0
expect_crossdomain "SIGHUP reload" true
expect_publisher_kept "SIGHUP reload"

# --- Step 7: A file that fails to parse ---
echo "=== Step 7: Reload a file that fails to parse ==="
OLD_RID=$(fetch_field rid)
write_conf off 4096 "this is { broken"
curl -fsS "$RAW?rpc=reload" >/dev/null
STATUS=$(wait_reload "$OLD_RID")
# The parsing state (10) fails with ERROR_SYSTEM_CONFIG_INVALID (1023).
expect_status "$STATUS" "parse failure" 10 1023
expect_msg "$STATUS" "parse failure" "parse file"
expect_crossdomain "parse failure keeps the old config" true
expect_publisher_kept "parse failure"

# --- Step 8: A file that fails check_config ---
echo "=== Step 8: Reload a file that fails check_config ==="
OLD_RID=$(fetch_field rid)
write_conf off 4096 "reload_test_unknown_directive on;"
curl -fsS "$RAW?rpc=reload" >/dev/null
STATUS=$(wait_reload "$OLD_RID")
# check_config runs in the transforming state (20).
expect_status "$STATUS" "check_config failure" 20 1023
expect_msg "$STATUS" "check_config failure" "illegal directive"
expect_crossdomain "check_config failure keeps the old config" true
expect_publisher_kept "check_config failure"

# --- Step 9: Recover with a valid file ---
echo "=== Step 9: Reload a valid file again ==="
OLD_RID=$(fetch_field rid)
write_conf off 60000 ""
curl -fsS "$RAW?rpc=reload" >/dev/null
STATUS=$(wait_reload "$OLD_RID")
expect_status "$STATUS" "recovery" 90 0
expect_msg "$STATUS" "recovery" "Success"
expect_crossdomain "recovery" false
expect_publisher_kept "recovery"
probe_has_audio_video "RTMP after recovery" "rtmp://127.0.0.1:$RTMP_PORT/$STREAM_URL"

# --- Step 10: A file that turns allow_reload off ---
echo "=== Step 10: Reload a file that turns allow_reload off ==="
OLD_RID=$(fetch_field rid)
write_conf off 60000 "" off
curl -fsS "$RAW?rpc=reload" >/dev/null
STATUS=$(wait_reload "$OLD_RID")
expect_status "$STATUS" "allow_reload off" 90 0
if [[ "$(curl -fsS "$RAW?rpc=raw" | jq -r .http_api.raw_api.allow_reload)" == "false" ]]; then
  echo "PASS: allow_reload off: rpc=raw reports allow_reload=false."
else
  fail "allow_reload off: rpc=raw does not report allow_reload=false."
fi
# The running config now forbids rpc=reload: it is refused with
# ERROR_SYSTEM_CONFIG_RAW_DISABLED (1061), and no reload follows.
OLD_RID=$(fetch_field rid)
RESPONSE=$(curl -fsS "$RAW?rpc=reload")
if [[ "$(echo "$RESPONSE" | jq -r .code)" == "1061" ]]; then
  echo "PASS: allow_reload off: rpc=reload refused with 1061."
else
  fail "allow_reload off: rpc=reload answered $RESPONSE, want code 1061."
fi
sleep 3
if [[ "$(fetch_field rid)" == "$OLD_RID" ]]; then
  echo "PASS: allow_reload off: no reload happened."
else
  fail "allow_reload off: a reload happened; rid changed from $OLD_RID."
fi
# SIGHUP still reloads, and a file that turns allow_reload on restores rpc=reload.
write_conf off 60000 "" on
kill -HUP "$SRS_PID"
STATUS=$(wait_reload "$OLD_RID")
expect_status "$STATUS" "allow_reload on by SIGHUP" 90 0
OLD_RID=$(fetch_field rid)
RESPONSE=$(curl -fsS "$RAW?rpc=reload")
[[ "$(echo "$RESPONSE" | jq -r .code)" == "0" ]] || fail "allow_reload on: rpc=reload answered $RESPONSE."
STATUS=$(wait_reload "$OLD_RID")
expect_status "$STATUS" "allow_reload on: rpc=reload" 90 0
expect_publisher_kept "allow_reload toggled"

# A file that turns raw_api off: rpc=reload-fetch is then refused with 1061.
# The reload is awaited through rpc=raw, which is served even when it is off.
OLD_RID=$(fetch_field rid)
write_conf off 60000 "" on off
curl -fsS "$RAW?rpc=reload" >/dev/null
for ((i = 1; i <= 20; i++)); do
  if [[ "$(curl -fsS "$RAW?rpc=raw" | jq -r .http_api.raw_api.enabled)" == "false" ]]; then
    break
  fi
  sleep 0.5
done
[[ "$(curl -fsS "$RAW?rpc=raw" | jq -r .http_api.raw_api.enabled)" == "false" ]] ||
  fail "raw_api off: rpc=raw does not report raw_api.enabled=false in 10s."
RESPONSE=$(curl -fsS "$RAW?rpc=reload-fetch")
if [[ "$(echo "$RESPONSE" | jq -r .code)" == "1061" ]]; then
  echo "PASS: raw_api off: rpc=reload-fetch refused with 1061."
else
  fail "raw_api off: rpc=reload-fetch answered $RESPONSE, want code 1061."
fi
# SIGHUP reloads a file that turns it back on, and rpc=reload-fetch answers again.
write_conf off 60000 "" on on
kill -HUP "$SRS_PID"
STATUS=""
for ((i = 1; i <= 20; i++)); do
  RESPONSE=$(curl -fsS "$RAW?rpc=reload-fetch")
  if [[ "$(echo "$RESPONSE" | jq -r .code)" == "0" ]]; then
    STATUS="$RESPONSE"
    break
  fi
  sleep 0.5
done
[[ -n "$STATUS" ]] || fail "raw_api on by SIGHUP: rpc=reload-fetch still refused after 10s."
expect_status "$STATUS" "raw_api on by SIGHUP" 90 0
[[ "$(echo "$STATUS" | jq -r .data.rid)" != "$OLD_RID" ]] || fail "raw_api on by SIGHUP: rid is still $OLD_RID."
expect_publisher_kept "raw_api toggled"

kill "$FFMPEG_PID" 2>/dev/null || true
FFMPEG_PID=""
stop_srs "config file" "$SRS_STDOUT"

# --- Step 11: Reload an SRS started with -e ---
# With -e there is no file to re-read, so a reload fails in the parsing state
# and the server keeps its config. This passes from the start and locks in
# accepted behavior.
echo "=== Step 11: Reload an SRS started with -e ==="
(
  for name in $(env | sed -n 's/^\(SRS_[A-Za-z0-9_]*\)=.*/\1/p'); do
    unset "$name"
  done
  cd "$WORKSPACE/trunk"
  export SRS_RTMP_LISTEN=$RTMP_PORT SRS_DAEMON=off SRS_PID="$TEST_DIR/srs-env.pid"
  export SRS_SRS_LOG_TANK=console SRS_HTTP_SERVER_ENABLED=off
  export SRS_HTTP_API_ENABLED=on SRS_HTTP_API_LISTEN=$HTTP_API_PORT
  export SRS_HTTP_API_RAW_API_ENABLED=on SRS_HTTP_API_RAW_API_ALLOW_RELOAD=on
  exec "$SRS_BINARY" -e >"$ENV_STDOUT" 2>&1
) &
SRS_PID=$!
echo "SRS PID: $SRS_PID"
wait_api "$SRS_PID"
OLD_RID=$(fetch_field rid)
curl -fsS "$RAW?rpc=reload" >/dev/null
STATUS=$(wait_reload "$OLD_RID")
expect_status "$STATUS" "-e reload" 10 1023
expect_msg "$STATUS" "-e reload" "empty config"
curl -fsS "$API/api/v1/versions" >/dev/null 2>&1 || fail "-e reload: HTTP API gone."
echo "PASS: -e reload: server still serves the API."
stop_srs "-e" "$ENV_STDOUT"

echo ""
echo "=== E2E SRS Reload Test PASSED ==="
