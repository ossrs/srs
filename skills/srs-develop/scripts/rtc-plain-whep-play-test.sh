#!/bin/bash
# E2E test for plain retransmission on the WebRTC play (downlink) path, the
# fallback of RFC 4588 RTX. It starts a disposable SRS built with the NACK drop
# simulator, which every E2E script here enables, with nack_prefer_rtx off,
# publishes over WHIP with FFmpeg, and plays over WHEP with pion-whep from
# tools/pion-whep: a pion peer that offers rtx like a browser, sends a NACK for
# every gap it sees, and logs every retransmission it gets back, plain or RTX.
# Then:
#   1. checks the player's offer carries rtx and apt, so the peer is RTX-capable
#      and the fallback is SRS's choice, not the peer's;
#   2. checks the SRS answer carries no rtx payload, no apt, no FID group and
#      exactly one video SSRC, and that pion bound no repair stream;
#   3. drops sent packets through the NACK simulator API on the player's
#      session so the player must request a retransmission;
#   4. expects the detail NACK logs of the simulator build to show a
#      "NACK: received" for the video SSRC answered by a "NACK: resend plain"
#      of one of the requested sequences on the media SSRC, with no
#      "NACK: resend RTX", and the tool log to show "NACK sent" answered by
#      "Recovered plain" for that sequence with no "Recovered RTX".
# This locks in the accepted fallback: plain retransmission is today's behavior
# and stays the answer for every peer under nack_prefer_rtx off, so the test
# passes as soon as the tool works and was never red on SRS itself. To force
# the fallback from the client side instead, against an SRS that prefers RTX,
# run the tool with PION_WHEP_RTX=off; this script keeps the server-side rule.
# On failure it prints where the chain broke: no video packet dropped, no NACK
# received by SRS, no plain resend, or a resend not recovered by the tool.
#
# Environment:
#   SRS_PLAIN_DROP=<n>     packets to drop through the simulator (default 30)
#   SRS_PLAIN_WAIT=<s>     seconds to wait for the recovery (default 30)
set -euo pipefail

SCRIPT_DIR="$(cd -P "$(dirname "$0")" && pwd)"
WORKSPACE="$SCRIPT_DIR"
while [[ "$WORKSPACE" != "/" && ! -f "$WORKSPACE/trunk/configure" ]]; do
  WORKSPACE="$(dirname "$WORKSPACE")"
done

if [[ ! -f "$WORKSPACE/trunk/configure" ]]; then
  echo "Error: SRS workspace not found walking up from: $SCRIPT_DIR" >&2
  exit 1
fi

SRS_BINARY="$WORKSPACE/trunk/objs/srs"
SRS_AUTO_HEADERS="$WORKSPACE/trunk/objs/srs_auto_headers.hpp"
SOURCE_FLV="$WORKSPACE/trunk/doc/source.flv"
TOOL_DIR="$WORKSPACE/tools/pion-whep"
TOOL_BIN="$TOOL_DIR/objs/pion-whep"
RTMP_PORT="${SRS_PLAIN_RTMP_PORT:-31935}"
HTTP_API_PORT="${SRS_PLAIN_HTTP_API_PORT:-31985}"
RTC_PORT="${SRS_PLAIN_RTC_PORT:-38000}"
DROP="${SRS_PLAIN_DROP:-30}"
WAIT="${SRS_PLAIN_WAIT:-30}"
STREAM_NAME="plain$(date +%s)"
STREAM_URL="live/$STREAM_NAME"
WHIP_URL="http://127.0.0.1:$HTTP_API_PORT/rtc/v1/whip/?app=live&stream=$STREAM_NAME"
WHEP_URL="http://127.0.0.1:$HTTP_API_PORT/rtc/v1/whep/?app=live&stream=$STREAM_NAME"
API="http://127.0.0.1:$HTTP_API_PORT"
TEST_DIR=$(mktemp -d "${TMPDIR:-/tmp}/srs-rtc-plain-play.XXXXXX")
SRS_CONF="$TEST_DIR/srs.conf"
SRS_LOG="$TEST_DIR/srs.log"
SRS_PID_FILE="$TEST_DIR/srs.pid"
FFMPEG_LOG="$TEST_DIR/ffmpeg.log"
TOOL_LOG="$TEST_DIR/pion-whep.log"
BUILD_LOG="$TEST_DIR/build.log"
SRS_PID=""
FFMPEG_PID=""
TOOL_PID=""
TEST_PASSED=0

cleanup() {
  echo ""
  echo "=== Cleaning up ==="
  for pid in $TOOL_PID $FFMPEG_PID $SRS_PID; do
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
      for _ in {1..20}; do
        if ! kill -0 "$pid" 2>/dev/null; then
          break
        fi
        sleep 0.1
      done
    fi
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
      kill -9 "$pid" 2>/dev/null || true
    fi
    if [[ -n "$pid" ]]; then
      wait "$pid" 2>/dev/null || true
    fi
  done

  if [[ "$TEST_PASSED" == "1" ]]; then
    rm -rf "$TEST_DIR"
  else
    echo "Logs kept for review:" >&2
    echo "  SRS:       $SRS_LOG" >&2
    echo "  FFmpeg:    $FFMPEG_LOG" >&2
    echo "  pion-whep: $TOOL_LOG" >&2
  fi
  echo "Cleanup done."
}
trap cleanup EXIT

# The tool prints the SDP it sent and received at -loglevel debug, with CRLF.
tool_offer() {
  sed -n '/^Offer SDP:/,/^Input #0/p' "$TOOL_LOG" | tr -d '\r'
}
tool_answer() {
  sed -n '/^Answer SDP:/,/^Session URL:/p' "$TOOL_LOG" | tr -d '\r'
}
video_section() {
  awk '/^m=video/ { on = 1 } /^m=audio/ { on = 0 } on'
}

# Print the SRS counters and NACK lines that explain a failure.
dump_evidence() {
  echo "--- SRS RTC counters ---" >&2
  grep -a "RTC: Server conns=" "$SRS_LOG" | tail -6 >&2 || true
  echo "--- SRS NACK simulator drops ---" >&2
  grep -a "NACK simulator" "$SRS_LOG" | tail -8 >&2 || true
  echo "--- SRS NACK detail logs ---" >&2
  grep -a "NACK: received\|NACK: resend\|NACK: miss" "$SRS_LOG" | tail -8 >&2 || true
  echo "--- pion-whep NACK and recovery ---" >&2
  grep -a "NACK sent\|Recovered " "$TOOL_LOG" | tail -8 >&2 || true
}

echo "=== E2E WebRTC Plain Retransmission Play Test ==="
echo "Workspace: $WORKSPACE"
echo "Stream:    $STREAM_URL"
echo "Drop:      $DROP packets, wait up to ${WAIT}s for the recovery"
echo ""

for tool in curl jq make go; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "Error: $tool is required" >&2
    exit 1
  fi
done
if [[ ! -f "$SOURCE_FLV" ]]; then
  echo "Error: test source not found: $SOURCE_FLV" >&2
  exit 1
fi

# WHIP needs an ffmpeg with the whip muxer, see proxy-e2e-whip-test.sh.
ffmpeg_has_whip() {
  local bin="$1"
  [[ -x "$bin" ]] && "$bin" -hide_banner -muxers 2>/dev/null | grep -qw whip
}
FFMPEG_BIN=""
for candidate in "$(command -v ffmpeg || true)" "$HOME/.local/bin/ffmpeg"; do
  if [[ -n "$candidate" ]] && ffmpeg_has_whip "$candidate"; then
    FFMPEG_BIN="$candidate"
    break
  fi
done
if [[ -z "$FFMPEG_BIN" ]]; then
  echo "Error: no ffmpeg with the whip muxer on PATH or in ~/.local/bin." >&2
  echo "       Build one with: bash $SCRIPT_DIR/setup-ffmpeg-with-whip.sh" >&2
  exit 1
fi
echo "ffmpeg: $FFMPEG_BIN"

# The NACK API /rtc/v1/nack/ is compiled only with --simulator=on, which every
# E2E script here passes to configure, so an existing binary is reused as is.
srs_has_simulator() {
  [[ -x "$SRS_BINARY" ]] && grep -q "^#define SRS_SIMULATOR$" "$SRS_AUTO_HEADERS" 2>/dev/null
}
if srs_has_simulator; then
  echo "=== Step 1: SRS already built with the NACK simulator ==="
else
  echo "=== Step 1: Building SRS with the NACK simulator ==="
  (
    cd "$WORKSPACE/trunk"
    ./configure --simulator=on >"$BUILD_LOG" 2>&1
    make -s >>"$BUILD_LOG" 2>&1
  )
  if ! srs_has_simulator; then
    echo "Error: SRS build did not enable the simulator, see $BUILD_LOG" >&2
    exit 1
  fi
fi
echo "SRS: $SRS_BINARY"

# Rebuild the tool when its binary is missing or older than its sources.
tool_is_stale() {
  [[ ! -x "$TOOL_BIN" ]] || [[ -n "$(find "$TOOL_DIR" -maxdepth 1 \( -name '*.go' -o -name 'go.mod' -o -name 'go.sum' \) -newer "$TOOL_BIN")" ]]
}
if tool_is_stale; then
  echo "=== Step 2: Building pion-whep ==="
  if ! ( cd "$TOOL_DIR" && mkdir -p objs && go build -o objs/pion-whep . ) >"$BUILD_LOG" 2>&1; then
    echo "Error: pion-whep build failed, see $BUILD_LOG" >&2
    cat "$BUILD_LOG" >&2
    exit 1
  fi
else
  echo "=== Step 2: pion-whep already built ==="
fi
echo "pion-whep: $TOOL_BIN"

cat >"$SRS_CONF" <<CONF
listen $RTMP_PORT;
pid $SRS_PID_FILE;
max_connections 1000;
daemon off;
srs_log_tank console;

http_api {
  enabled on;
  listen $HTTP_API_PORT;
}

rtc_server {
  enabled on;
  listen $RTC_PORT;
  candidate 127.0.0.1;
}

vhost __defaultVhost__ {
  rtc {
    enabled on;
    nack_prefer_rtx off;
    rtc_to_rtmp off;
  }
}
CONF

echo "=== Step 3: Starting SRS with nack_prefer_rtx off ==="
(
  cd "$WORKSPACE/trunk"
  exec "$SRS_BINARY" -c "$SRS_CONF" >"$SRS_LOG" 2>&1
) &
SRS_PID=$!
echo "SRS PID: $SRS_PID"

READY=0
for _ in {1..50}; do
  if curl --silent --fail "$API/api/v1/versions" >/dev/null; then
    READY=1
    break
  fi
  if ! kill -0 "$SRS_PID" 2>/dev/null; then
    break
  fi
  sleep 0.1
done
if [[ "$READY" != "1" ]]; then
  echo "FAIL: SRS did not start" >&2
  tail -40 "$SRS_LOG" >&2
  exit 1
fi
echo "SRS started: API :$HTTP_API_PORT, WebRTC udp :$RTC_PORT"

echo "=== Step 4: Publishing over WHIP with FFmpeg ==="
# WebRTC needs H.264 baseline + Opus; source.flv is H.264 High + AAC.
"$FFMPEG_BIN" -hide_banner -loglevel warning -nostats -stream_loop -1 -re -i "$SOURCE_FLV" \
  -c:v libx264 -profile:v baseline -level 3.1 -pix_fmt yuv420p \
  -tune zerolatency -preset ultrafast \
  -c:a libopus -ar 48000 -ac 2 \
  -f whip "$WHIP_URL" >"$FFMPEG_LOG" 2>&1 &
FFMPEG_PID=$!
echo "FFmpeg PID: $FFMPEG_PID"

ESTABLISHED=0
for _ in {1..150}; do
  if grep -aq "RTC: Publisher url=/$STREAM_URL established" "$SRS_LOG"; then
    ESTABLISHED=1
    break
  fi
  if ! kill -0 "$FFMPEG_PID" 2>/dev/null; then
    break
  fi
  sleep 0.1
done
if [[ "$ESTABLISHED" != "1" ]]; then
  echo "FAIL: WHIP publisher was not established" >&2
  tail -40 "$FFMPEG_LOG" >&2
  exit 1
fi
echo "Publisher established."

echo "=== Step 5: Playing over WHEP with pion-whep ==="
# The debug log level prints the offer, the answer, the session URL and one
# line per NACK sent and per recovery. The duration bounds the run.
"$TOOL_BIN" -hide_banner -loglevel debug \
  -f whep -i "$WHEP_URL" -t "$((WAIT + 20))" -f null - >"$TOOL_LOG" 2>&1 &
TOOL_PID=$!
echo "pion-whep PID: $TOOL_PID"

ESTABLISHED=0
for _ in {1..150}; do
  if grep -aq "RTC: Subscriber url=/$STREAM_URL established" "$SRS_LOG" && grep -aq "^Session URL:" "$TOOL_LOG"; then
    ESTABLISHED=1
    break
  fi
  if ! kill -0 "$TOOL_PID" 2>/dev/null; then
    break
  fi
  sleep 0.1
done
if [[ "$ESTABLISHED" != "1" ]]; then
  echo "FAIL: WHEP player was not established" >&2
  tail -40 "$TOOL_LOG" >&2
  exit 1
fi
echo "Player established."

echo "=== Step 6: Checking the player's offer carries rtx ==="
OFFER_VIDEO=$(tool_offer | video_section)
if ! printf '%s\n' "$OFFER_VIDEO" | grep -q "^a=rtpmap:[0-9]* rtx/90000"; then
  echo "FAIL: offer has no rtx payload, the peer is not RTX-capable" >&2
  exit 1
fi
if ! printf '%s\n' "$OFFER_VIDEO" | grep -q "^a=fmtp:[0-9]* apt=[0-9]*"; then
  echo "FAIL: offer has no apt for its rtx payload" >&2
  exit 1
fi
printf '%s\n' "$OFFER_VIDEO" | grep "rtx/90000\|apt=" | head -4 | sed 's/^/  offer: /'
echo "PASS: offer carries rtx and apt."

echo "=== Step 7: Checking the SRS answer falls back to plain retransmission ==="
ANSWER_VIDEO=$(tool_answer | video_section)
if [[ -z "$ANSWER_VIDEO" ]]; then
  echo "FAIL: no video section in the SRS answer" >&2
  exit 1
fi
MEDIA_PT=$(printf '%s\n' "$ANSWER_VIDEO" | sed -n -E 's/^a=rtpmap:([0-9]+) H264\/90000$/\1/p' | head -1)
if [[ -z "$MEDIA_PT" ]]; then
  echo "FAIL: answer has no H264 payload" >&2
  printf '%s\n' "$ANSWER_VIDEO" >&2
  exit 1
fi
if printf '%s\n' "$ANSWER_VIDEO" | grep -q "^a=rtpmap:[0-9]* rtx/90000"; then
  echo "FAIL: answer carries an rtx payload, nack_prefer_rtx off must answer plain" >&2
  printf '%s\n' "$ANSWER_VIDEO" >&2
  exit 1
fi
if printf '%s\n' "$ANSWER_VIDEO" | grep -q "^a=fmtp:[0-9]* apt="; then
  echo "FAIL: answer carries an apt fmtp" >&2
  printf '%s\n' "$ANSWER_VIDEO" >&2
  exit 1
fi
if printf '%s\n' "$ANSWER_VIDEO" | grep -q "^a=ssrc-group:FID"; then
  echo "FAIL: answer carries a=ssrc-group:FID" >&2
  printf '%s\n' "$ANSWER_VIDEO" >&2
  exit 1
fi
# SRS sends, so the play answer declares its SSRCs: exactly one for the video.
VIDEO_SSRCS=$(printf '%s\n' "$ANSWER_VIDEO" | sed -n -E 's/^a=ssrc:([0-9]+) .*/\1/p' | sort -u)
if [[ "$(printf '%s\n' "$VIDEO_SSRCS" | grep -c .)" != "1" ]]; then
  echo "FAIL: answer must declare exactly one video SSRC, got: $(printf '%s ' $VIDEO_SSRCS)" >&2
  printf '%s\n' "$ANSWER_VIDEO" >&2
  exit 1
fi
MEDIA_SSRC="$VIDEO_SSRCS"
printf '%s\n' "$ANSWER_VIDEO" | grep "^m=video\|^a=rtpmap:\|^a=rtcp-fb:$MEDIA_PT\|cname:" | sed 's/^/  answer: /'
echo "PASS: answer keeps H264 pt=$MEDIA_PT on ssrc=$MEDIA_SSRC with no rtx, apt or FID."

echo "=== Step 8: Waiting for video packets to reach the player ==="
RECEIVED=0
for _ in {1..100}; do
  RECEIVED=$(grep -a "Received packets video=" "$TOOL_LOG" | tail -1 | sed -n -E 's/.*video=([0-9]+),.*/\1/p' || true)
  if [[ -n "$RECEIVED" && "$RECEIVED" -gt 0 ]]; then
    break
  fi
  sleep 0.1
done
if [[ -z "$RECEIVED" || "$RECEIVED" -le 0 ]]; then
  echo "FAIL: the player received no video packet" >&2
  tail -20 "$TOOL_LOG" >&2
  exit 1
fi
# Without a FID group pion binds no repair stream to the video track.
if ! grep -aq "Stream #0:[0-9]*: Video: .*ssrc=$MEDIA_SSRC, .*rtx ssrc=0$" "$TOOL_LOG"; then
  echo "FAIL: the player bound a repair stream to the video track ssrc=$MEDIA_SSRC" >&2
  grep -a "Stream #0:" "$TOOL_LOG" >&2 || true
  exit 1
fi
echo "Video packets received: $RECEIVED, no repair stream bound"

echo "=== Step 9: Dropping $DROP sent packets through the NACK simulator ==="
USERNAME=$(grep -a -m1 "^Session URL:" "$TOOL_LOG" | sed -E 's/.*[?&]session=([^ &]*).*/\1/' || true)
if [[ -z "$USERNAME" ]]; then
  echo "FAIL: session username not found in the pion-whep log" >&2
  exit 1
fi
echo "Session username: $USERNAME"
# SRS reads the query verbatim, without percent-decoding, so the username stays literal.
NACK_RESPONSE=$(curl --silent --show-error "$API/rtc/v1/nack/?username=$USERNAME&drop=$DROP")
echo "NACK API response: $NACK_RESPONSE"
if [[ "$(printf '%s' "$NACK_RESPONSE" | jq -r '.code')" != "0" ]]; then
  echo "FAIL: NACK API refused the drop request" >&2
  exit 1
fi

sleep 2
VIDEO_DROPS=$(grep -ac "NACK simulator #.* player drop seq=[0-9]*, ssrc=$MEDIA_SSRC," "$SRS_LOG" || true)
if [[ "$VIDEO_DROPS" -le 0 ]]; then
  echo "FAIL: the simulator dropped no video packet, raise SRS_PLAIN_DROP" >&2
  dump_evidence
  exit 1
fi
echo "Video packets dropped: $VIDEO_DROPS"

echo "=== Step 10: Waiting for a plain retransmission to be recovered (up to ${WAIT}s) ==="
# The simulator build logs every NACK received and every resend, so the chain
# is verified packet by packet: the player asks for a sequence on the video
# SSRC, SRS resends that same packet on the media SSRC with the media payload
# type, and the tool recovers the same sequence from the media SSRC. An RTX
# answer would show "NACK: resend RTX" on the RTX SSRC with an osn instead.
received_seqs() {
  grep -a "NACK: received ssrc=$MEDIA_SSRC, seqs=\[" "$SRS_LOG" \
    | sed -E 's/.*seqs=\[([0-9,]*)\].*/\1/' | tr ',' '\n' | sort -u || true
}
RESENT=0
RECOVERED=0
for _ in $(seq 1 "$WAIT"); do
  RESENT=0
  RECOVERED=0
  for seq in $(received_seqs); do
    if grep -aq "NACK: resend plain seq=$seq, ssrc=$MEDIA_SSRC, pt=$MEDIA_PT," "$SRS_LOG"; then
      RESENT=$((RESENT + 1))
      if grep -aq "Recovered plain seq=$seq, ssrc=$MEDIA_SSRC, pt=$MEDIA_PT$" "$TOOL_LOG"; then
        RECOVERED=$((RECOVERED + 1))
      fi
    fi
  done
  if [[ "$RECOVERED" -gt 0 ]]; then
    break
  fi
  sleep 1
done

RECEIVED_NACKS=$(grep -ac "NACK: received ssrc=$MEDIA_SSRC, seqs=\[" "$SRS_LOG" || true)
RTX_IGNORED=$(grep -ac "NACK: received ssrc=[0-9]* is RTX, ignored" "$SRS_LOG" || true)
RTX_RESENDS=$(grep -ac "NACK: resend RTX seq=" "$SRS_LOG" || true)
TOOL_NACKS=$(grep -ac "NACK sent ssrc=$MEDIA_SSRC, seqs=\[" "$TOOL_LOG" || true)
TOOL_RTX=$(grep -ac "Recovered RTX seq=" "$TOOL_LOG" || true)
echo "SRS NACKs received: $RECEIVED_NACKS, sequences resent plain: $RESENT, recovered by the tool: $RECOVERED, RTX resends: $RTX_RESENDS, NACKs to an RTX SSRC: $RTX_IGNORED"
echo "pion-whep NACKs sent: $TOOL_NACKS, RTX recoveries: $TOOL_RTX"

if [[ "$RECOVERED" -le 0 ]]; then
  if [[ "$TOOL_NACKS" -le 0 ]]; then
    echo "FAIL: the player sent no NACK after the drops" >&2
  elif [[ "$RECEIVED_NACKS" -le 0 ]]; then
    echo "FAIL: the player sent a NACK but SRS received none for ssrc=$MEDIA_SSRC" >&2
  elif [[ "$RESENT" -le 0 ]]; then
    echo "FAIL: SRS received a NACK but resent none of its sequences plain" >&2
  else
    echo "FAIL: SRS resent the packets but the player recovered none of them" >&2
  fi
  dump_evidence
  exit 1
fi
if [[ "$RTX_RESENDS" -gt 0 || "$TOOL_RTX" -gt 0 || "$RTX_IGNORED" -gt 0 ]]; then
  echo "FAIL: RTX was used although the answer negotiated plain retransmission" >&2
  dump_evidence
  exit 1
fi
grep -a "NACK: received ssrc=$MEDIA_SSRC,\|NACK: resend plain seq=" "$SRS_LOG" | head -3 | sed 's/^/  srs: /' || true
grep -a "NACK sent ssrc=$MEDIA_SSRC,\|Recovered plain seq=" "$TOOL_LOG" | head -3 | sed 's/^/  pion-whep: /' || true
echo "PASS: a NACK was answered by a plain resend on the media SSRC and recovered by the player."

TEST_PASSED=1
echo ""
echo "=== E2E WebRTC Plain Retransmission Play Test PASSED ==="
