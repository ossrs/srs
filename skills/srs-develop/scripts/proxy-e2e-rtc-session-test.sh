#!/bin/bash
# E2E test for the lifetime of the proxy's WebRTC sessions: starts proxy + SRS
# origin, and checks from outside the proxy that every session it relays ends
# and frees its resources:
#   - A WHEP player that ends by a DELETE frees its session at once.
#   - A WHIP publisher that ends by a DELETE frees its session at once.
#   - A WHEP player killed without a DELETE frees its session by the idle
#     timeout, PROXY_RTC_SESSION_TTL after its last packet, although the
#     origin still sends it media; the live publisher keeps its own session.
#     The script sets the TTL to 10s, from the default 120s, to keep it short.
#
# A session holds one connected UDP socket to the origin and one goroutine,
# rtcConnection.readBackend, which reads it. The script counts both: the
# sockets with lsof, and the goroutines from the proxy's Go pprof.
#
# It takes about 30 seconds.
set -e

SCRIPT_DIR="$(cd -P "$(dirname "$0")" && pwd)"
WORKSPACE="$SCRIPT_DIR"
while [[ "$WORKSPACE" != "/" && ! -f "$WORKSPACE/go.mod" ]]; do
  WORKSPACE="$(dirname "$WORKSPACE")"
done

if [[ ! -f "$WORKSPACE/go.mod" ]]; then
  echo "Error: go.mod not found walking up from: $SCRIPT_DIR" >&2
  exit 1
fi

# The same layout as proxy-e2e-whip-test.sh, plus the Go pprof at +6.
# SRS_TEST_PORT_BASE, by default this script's slot in the Port Plan, moves every port,
# so the scripts can run in parallel.
PORT_BASE=${SRS_TEST_PORT_BASE:-11800}
PROXY_RTMP_PORT=$((PORT_BASE + 0))
PROXY_HTTP_API_PORT=$((PORT_BASE + 1))
PROXY_HTTP_SERVER_PORT=$((PORT_BASE + 2))
PROXY_WEBRTC_PORT=$((PORT_BASE + 3))
PROXY_SRT_PORT=$((PORT_BASE + 4))
PROXY_SYSTEM_API_PORT=$((PORT_BASE + 5))
PROXY_PPROF_PORT=$((PORT_BASE + 6))

# Origin ports (from srs_proxy_origin 1 in proxy-e2e-origin.sh).
ORIGIN_RTMP_PORT=$((PORT_BASE + 20))
ORIGIN_HTTP_PORT=$((PORT_BASE + 21))
ORIGIN_API_PORT=$((PORT_BASE + 22))
ORIGIN_RTC_PORT=$((PORT_BASE + 23))
ORIGIN_SRT_PORT=$((PORT_BASE + 24))

SOURCE_FLV="$WORKSPACE/trunk/doc/source.flv"
SRS_BINARY="$WORKSPACE/trunk/objs/srs"
source "$SCRIPT_DIR/proxy-e2e-origin.sh"
STREAM_NAME="rtcsession$(date +%s)"
WHIP_PUBLISH_URL="http://localhost:$PROXY_HTTP_API_PORT/rtc/v1/whip/?app=live&stream=$STREAM_NAME"
WHEP_PLAY_URL="http://localhost:$PROXY_HTTP_API_PORT/rtc/v1/whep/?app=live&stream=$STREAM_NAME"

# The proxy rewrites only the port of the origin's candidate, so the IP must
# already be reachable for the clients.
ORIGIN_CANDIDATE="127.0.0.1"

# The idle sweep in internal/proxy/rtc.go runs every quarter of a TTL under
# 40s, so with 10s a session ends between 10s and 12.5s after its last packet.
RTC_SESSION_TTL=10s
IDLE_MIN=9
IDLE_MAX=16

PROXY_LOG=/tmp/srs-proxy-rtc-session-e2e.log
ORIGIN_LOG=/tmp/srs-origin-rtc-session-e2e.log
FFMPEG_LOG=/tmp/srs-ffmpeg-rtc-session-e2e.log
WHEP_LOG=/tmp/srs-pion-whep-rtc-session-e2e.log

PROXY_PID=""
ORIGIN_PID=""
FFMPEG_PID=""
WHEP_PID=""

cleanup() {
  echo ""
  echo "=== Cleaning up ==="
  for pid in $PROXY_PID $ORIGIN_PID $FFMPEG_PID $WHEP_PID; do
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
    fi
  done
  sleep 1
  for pid in $PROXY_PID $ORIGIN_PID $FFMPEG_PID $WHEP_PID; do
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
      kill -9 "$pid" 2>/dev/null || true
    fi
  done
  echo "Cleanup done."
}
trap cleanup EXIT

# The goroutines that read an origin socket, one per session.
session_readers() {
  curl -fsS "http://127.0.0.1:$PROXY_PPROF_PORT/debug/pprof/goroutine?debug=2" 2>/dev/null |
    grep -c 'rtcConnection).readBackend(' || true
}

# The proxy's connected UDP sockets, one per session; the WebRTC listener is
# not connected, so lsof prints it without "->".
session_sockets() {
  lsof -nP -a -p "$PROXY_PID" -iUDP 2>/dev/null | awk 'NR > 1 && $9 ~ /->/' | wc -l | tr -d ' '
}

# Waits until the proxy holds exactly the given sessions, by both counts.
wait_sessions() {
  local want="$1" deadline="$2" what="$3"
  local readers sockets
  for ((i = 0; i <= deadline; i++)); do
    readers="$(session_readers)"
    sockets="$(session_sockets)"
    if [[ "$readers" == "$want" && "$sockets" == "$want" ]]; then
      echo "PASS: $what: $want sessions (readers=$readers, sockets=$sockets)."
      return 0
    fi
    if ((i < deadline)); then
      sleep 1
    fi
  done
  echo "FAIL: $what: want $want sessions in ${deadline}s, got readers=$readers, sockets=$sockets." >&2
  lsof -nP -a -p "$PROXY_PID" -iUDP >&2 || true
  tail -20 "$PROXY_LOG" >&2
  exit 1
}

# Stops FFmpeg by SIGINT, so it sends a DELETE to the Location of its answer.
stop_ffmpeg() {
  kill -INT "$FFMPEG_PID" 2>/dev/null || true
  for ((i = 1; i <= 10; i++)); do
    if ! kill -0 "$FFMPEG_PID" 2>/dev/null; then
      break
    fi
    sleep 1
  done
  FFMPEG_PID=""
}

start_ffmpeg() {
  "$FFMPEG_BIN" -stream_loop -1 -re -i "$SOURCE_FLV" \
    -c:v libx264 -profile:v baseline -level 3.1 -pix_fmt yuv420p \
    -tune zerolatency -preset ultrafast -g 25 \
    -c:a libopus -ar 48000 -ac 2 \
    -f whip "$WHIP_PUBLISH_URL" >>"$FFMPEG_LOG" 2>&1 &
  FFMPEG_PID=$!
}

echo "=== E2E Proxy WebRTC Session Test ==="
echo "Workspace: $WORKSPACE"
echo "Stream:    live/$STREAM_NAME"
echo ""

# --- Pre-checks ---
if [[ ! -f "$SOURCE_FLV" ]]; then
  echo "Error: test source not found: $SOURCE_FLV" >&2
  exit 1
fi
for tool in curl go lsof; do
  if ! command -v "$tool" &>/dev/null; then
    echo "Error: $tool not found in PATH" >&2
    exit 1
  fi
done

# WHIP needs an ffmpeg with the whip muxer, see proxy-e2e-whip-test.sh.
ffmpeg_has_whip() {
  local bin="$1"
  [[ -x "$bin" ]] && "$bin" -hide_banner -muxers 2>/dev/null | grep -qw whip
}

resolve_ffmpeg() {
  local sys_ffmpeg local_ffmpeg
  sys_ffmpeg="$(command -v ffmpeg || true)"
  local_ffmpeg="$HOME/.local/bin/ffmpeg"
  if ffmpeg_has_whip "$sys_ffmpeg"; then
    FFMPEG_BIN="$sys_ffmpeg"
    return 0
  fi
  if ffmpeg_has_whip "$local_ffmpeg"; then
    FFMPEG_BIN="$local_ffmpeg"
    return 0
  fi
  return 1
}

if ! resolve_ffmpeg; then
  echo "No ffmpeg with WHIP muxer found on PATH or in ~/.local/bin."
  echo "Building ffmpeg from source via setup-ffmpeg-with-whip.sh — this can take several minutes."
  bash "$SCRIPT_DIR/setup-ffmpeg-with-whip.sh"
  FFMPEG_BIN="$HOME/.local/bin/ffmpeg"
  if ! ffmpeg_has_whip "$FFMPEG_BIN"; then
    echo "Error: ffmpeg still lacks WHIP muxer after running setup-ffmpeg-with-whip.sh." >&2
    exit 1
  fi
fi
echo "ffmpeg : $FFMPEG_BIN"

WHEP_DIR="$WORKSPACE/tools/pion-whep"
WHEP_BIN="$WHEP_DIR/objs/pion-whep"
if [[ ! -x "$WHEP_BIN" ]] || [[ -n "$(find "$WHEP_DIR" -maxdepth 1 \( -name '*.go' -o -name 'go.mod' -o -name 'go.sum' \) -newer "$WHEP_BIN")" ]]; then
  echo "Building pion-whep: $WHEP_BIN"
  (cd "$WHEP_DIR" && mkdir -p objs && go build -o objs/pion-whep .)
fi

# --- Step 0: Clean up stale state ---
ALL_PORTS="$PROXY_RTMP_PORT $PROXY_HTTP_API_PORT $PROXY_HTTP_SERVER_PORT $PROXY_WEBRTC_PORT $PROXY_SRT_PORT $PROXY_SYSTEM_API_PORT $PROXY_PPROF_PORT $ORIGIN_RTMP_PORT $ORIGIN_HTTP_PORT $ORIGIN_API_PORT $ORIGIN_RTC_PORT $ORIGIN_SRT_PORT"
for port in $ALL_PORTS; do
  # Kill only listeners: a client of the port, such as another test's player, is not ours.
  lsof -ti TCP:"$port" -sTCP:LISTEN 2>/dev/null | xargs kill 2>/dev/null || true
  lsof -nP -iUDP:"$port" 2>/dev/null | awk -v p=":$port" 'NR > 1 && $9 !~ /->/ && $9 ~ p "$" {print $2}' | xargs kill 2>/dev/null || true
done
sleep 1
: >"$FFMPEG_LOG"

# --- Step 1: Build proxy ---
echo "=== Step 1: Building proxy ==="
cd "$WORKSPACE"
make -s 2>&1
echo "Proxy built: $WORKSPACE/bin/srs-proxy"

# --- Step 2: Build SRS origin (if not already built) ---
if [[ ! -f "$SRS_BINARY" ]]; then
  echo "=== Step 2: Building SRS origin ==="
  cd "$WORKSPACE/trunk"
  ./configure --simulator=on && make 2>&1 | tail -3
  echo "SRS origin built: $SRS_BINARY"
else
  echo "=== Step 2: SRS origin already built ==="
fi

# --- Step 3: Start proxy with Go pprof ---
echo "=== Step 3: Starting proxy (HTTP API :$PROXY_HTTP_API_PORT, WebRTC :$PROXY_WEBRTC_PORT, pprof :$PROXY_PPROF_PORT) ==="
cd "$WORKSPACE"
env PROXY_RTMP_SERVER=$PROXY_RTMP_PORT \
    PROXY_HTTP_API=$PROXY_HTTP_API_PORT \
    PROXY_HTTP_SERVER=$PROXY_HTTP_SERVER_PORT \
    PROXY_WEBRTC_SERVER=$PROXY_WEBRTC_PORT \
    PROXY_SRT_SERVER=$PROXY_SRT_PORT \
    PROXY_SYSTEM_API=$PROXY_SYSTEM_API_PORT \
    PROXY_LOAD_BALANCER_TYPE=memory \
    PROXY_RTC_SESSION_TTL=$RTC_SESSION_TTL \
    GO_PPROF=127.0.0.1:$PROXY_PPROF_PORT \
    ./bin/srs-proxy >"$PROXY_LOG" 2>&1 &
PROXY_PID=$!
echo "Proxy PID: $PROXY_PID"

for ((i = 1; i <= 10; i++)); do
  if curl -fsS "http://127.0.0.1:$PROXY_PPROF_PORT/debug/pprof/" >/dev/null 2>&1; then
    break
  fi
  sleep 1
done
if ! kill -0 "$PROXY_PID" 2>/dev/null || ! curl -fsS "http://127.0.0.1:$PROXY_PPROF_PORT/debug/pprof/" >/dev/null 2>&1; then
  echo "Error: proxy or its pprof failed to start. Logs:" >&2
  cat "$PROXY_LOG" >&2
  exit 1
fi
echo "Proxy started."

# --- Step 4: Start SRS origin ---
echo "=== Step 4: Starting SRS origin (CANDIDATE=$ORIGIN_CANDIDATE) ==="
ulimit -n 10000 2>/dev/null || true
cd "$WORKSPACE/trunk"
srs_proxy_origin 1 SRS_RTC_SERVER_CANDIDATE="$ORIGIN_CANDIDATE" >"$ORIGIN_LOG" 2>&1 &
ORIGIN_PID=$!
echo "SRS origin PID: $ORIGIN_PID"

echo "Waiting for SRS origin to register with proxy (up to 15s)..."
for i in $(seq 1 15); do
  if grep -q "Register SRS media server" "$PROXY_LOG" 2>/dev/null; then
    break
  fi
  sleep 1
done
if ! grep -q "Register SRS media server" "$PROXY_LOG" 2>/dev/null || ! kill -0 "$ORIGIN_PID" 2>/dev/null; then
  echo "Error: SRS origin did not start or register with proxy after 15s. Logs:" >&2
  tail -20 "$ORIGIN_LOG" >&2
  cat "$PROXY_LOG" >&2
  exit 1
fi
echo "SRS origin started and registered."
wait_sessions 0 0 "before any client"

# --- Step 5: A WHIP publisher has one session ---
echo "=== Step 5: Publishing WHIP stream to proxy ==="
start_ffmpeg
wait_sessions 1 15 "WHIP publisher connected"

# --- Step 6: A WHEP player that ends by a DELETE frees its session ---
echo "=== Step 6: WHEP play, then DELETE ==="
PION_WHEP_REQUIRE_SESSION=on "$WHEP_BIN" -hide_banner -i "$WHEP_PLAY_URL" -t 8 -f null - >"$WHEP_LOG" 2>&1 &
WHEP_PID=$!
wait_sessions 2 6 "WHEP player connected"
if ! wait "$WHEP_PID"; then
  echo "FAIL: WHEP play or its DELETE failed. pion-whep log:" >&2
  cat "$WHEP_LOG" >&2
  exit 1
fi
WHEP_PID=""
echo "pion-whep: $(grep -a "^Received video=" "$WHEP_LOG" | tail -1)"
wait_sessions 1 3 "WHEP player deleted"

# --- Step 7: A WHIP publisher that ends by a DELETE frees its session ---
echo "=== Step 7: Stop the WHIP publisher, which sends a DELETE ==="
stop_ffmpeg
wait_sessions 0 3 "WHIP publisher deleted"

# --- Step 8: A WHEP player that leaves without a DELETE ends by the idle timeout ---
# The origin keeps sending media to the killed player until its own STUN
# timeout, which must not keep the proxy session alive. The publisher sends
# packets all along, so its session stays.
echo "=== Step 8: Kill a WHEP player without a DELETE, wait for the idle timeout ==="
start_ffmpeg
wait_sessions 1 15 "WHIP publisher connected again"
"$WHEP_BIN" -hide_banner -i "$WHEP_PLAY_URL" -f null - >"$WHEP_LOG" 2>&1 &
WHEP_PID=$!
wait_sessions 2 6 "WHEP player connected"
sleep 2
kill -9 "$WHEP_PID" 2>/dev/null || true
wait "$WHEP_PID" 2>/dev/null || true
WHEP_PID=""
killed_at=$(date +%s)
echo "Killed the WHEP player; its session must end in ${IDLE_MIN}s to ${IDLE_MAX}s."

ended_after=""
while true; do
  elapsed=$(($(date +%s) - killed_at))
  readers="$(session_readers)"
  sockets="$(session_sockets)"
  if [[ "$readers" == "1" && "$sockets" == "1" ]]; then
    ended_after=$elapsed
    break
  fi
  if [[ "$readers" != "2" || "$sockets" != "2" ]]; then
    echo "FAIL: idle timeout: want 2 then 1 sessions, got readers=$readers, sockets=$sockets after ${elapsed}s." >&2
    exit 1
  fi
  if ((elapsed > IDLE_MAX)); then
    echo "FAIL: idle timeout: the killed player still holds its session after ${elapsed}s." >&2
    tail -20 "$PROXY_LOG" >&2
    exit 1
  fi
  sleep 1
done
if ((ended_after < IDLE_MIN)); then
  echo "FAIL: idle timeout: the session ended after ${ended_after}s, before ${IDLE_MIN}s." >&2
  exit 1
fi
echo "PASS: idle timeout: the killed player's session ended after ${ended_after}s; the publisher's stayed."

stop_ffmpeg
wait_sessions 0 3 "WHIP publisher deleted again"

echo ""
echo "=== E2E Proxy WebRTC Session Test PASSED ==="
