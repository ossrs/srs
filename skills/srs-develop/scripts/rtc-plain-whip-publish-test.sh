#!/bin/bash
# E2E test for plain retransmission on the WebRTC publish (uplink) path, the
# fallback of RFC 4588 RTX. It starts a disposable SRS built with the NACK drop
# simulator, which every E2E script here enables, with nack_prefer_rtx off, and
# publishes over WHIP with pion-whip from tools/pion-whip: a pion peer that
# offers rtx with a FID group like a browser and, when the answer omits rtx,
# resends a lost packet unchanged on the media SSRC. FFmpeg's WHIP muxer cannot
# be this peer, because it answers a NACK only in the RTX format. Then:
#   1. checks the offer carries rtx, apt and a=ssrc-group:FID, so the peer is
#      RTX-capable and the fallback is SRS's choice, not the peer's;
#   2. checks the SRS answer carries no rtx payload, no apt and no FID group;
#   3. drops received packets through the NACK simulator API so SRS must
#      request a retransmission;
#   4. expects the detail NACK logs of the simulator build to show a
#      "NACK: request" for the video SSRC answered by a "NACK: recovered" of
#      one of the requested sequences on the media SSRC, with no "NACK: RTX
#      recv" line and no rtx=(s,r,u,p) counter, and the tool log to show
#      "NACK received" answered by "Resend plain" with no "Resend RTX".
# Plain retransmission is the answer for every peer under nack_prefer_rtx off.
# On failure it prints where the chain broke: no video packet dropped, no gap
# detected by SRS, no NACK received by the tool, or a resend not recovered.
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
TOOL_DIR="$WORKSPACE/tools/pion-whip"
TOOL_BIN="$TOOL_DIR/objs/pion-whip"
RTMP_PORT="${SRS_PLAIN_RTMP_PORT:-31935}"
HTTP_API_PORT="${SRS_PLAIN_HTTP_API_PORT:-31985}"
RTC_PORT="${SRS_PLAIN_RTC_PORT:-38000}"
DROP="${SRS_PLAIN_DROP:-30}"
WAIT="${SRS_PLAIN_WAIT:-30}"
SOURCE_SECONDS=30
STREAM_NAME="plain$(date +%s)"
STREAM_URL="live/$STREAM_NAME"
WHIP_URL="http://127.0.0.1:$HTTP_API_PORT/rtc/v1/whip/?app=live&stream=$STREAM_NAME"
API="http://127.0.0.1:$HTTP_API_PORT"
TEST_DIR=$(mktemp -d "${TMPDIR:-/tmp}/srs-rtc-plain-publish.XXXXXX")
SRS_CONF="$TEST_DIR/srs.conf"
SRS_LOG="$TEST_DIR/srs.log"
SRS_PID_FILE="$TEST_DIR/srs.pid"
SOURCE_MP4="$TEST_DIR/source.mp4"
FFMPEG_LOG="$TEST_DIR/ffmpeg.log"
TOOL_LOG="$TEST_DIR/pion-whip.log"
BUILD_LOG="$TEST_DIR/build.log"
SRS_PID=""
TOOL_PID=""
TEST_PASSED=0

cleanup() {
  echo ""
  echo "=== Cleaning up ==="
  for pid in $TOOL_PID $SRS_PID; do
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
    echo "  pion-whip: $TOOL_LOG" >&2
  fi
  echo "Cleanup done."
}
trap cleanup EXIT

# The SRS console log escapes the SDP as one line with literal \r\n; expand it.
sdp_lines() {
  awk '{ gsub(/\\r\\n/, "\n"); print }'
}

# Print the SRS counters and NACK lines that explain a failure.
dump_evidence() {
  echo "--- SRS RTC counters ---" >&2
  grep -a "RTC: Server conns=" "$SRS_LOG" | tail -6 >&2 || true
  echo "--- SRS NACK simulator drops ---" >&2
  grep -a "NACK simulator" "$SRS_LOG" | tail -8 >&2 || true
  echo "--- SRS NACK detail logs ---" >&2
  grep -a "NACK: update\|NACK: request\|NACK: RTX\|NACK: recovered" "$SRS_LOG" | tail -8 >&2 || true
  echo "--- pion-whip NACK and resend ---" >&2
  grep -a "NACK received\|Resend " "$TOOL_LOG" | tail -8 >&2 || true
}

echo "=== E2E WebRTC Plain Retransmission Publish Test ==="
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

# The transcode needs libx264 and libopus; the whip muxer is not needed here.
ffmpeg_has_encoders() {
  local bin="$1"
  [[ -x "$bin" ]] && "$bin" -hide_banner -encoders 2>/dev/null | grep -qw libx264 \
    && "$bin" -hide_banner -encoders 2>/dev/null | grep -qw libopus
}
FFMPEG_BIN=""
for candidate in "$(command -v ffmpeg || true)" "$HOME/.local/bin/ffmpeg"; do
  if [[ -n "$candidate" ]] && ffmpeg_has_encoders "$candidate"; then
    FFMPEG_BIN="$candidate"
    break
  fi
done
if [[ -z "$FFMPEG_BIN" ]]; then
  echo "Error: no ffmpeg with libx264 and libopus on PATH or in ~/.local/bin." >&2
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

echo "=== Step 2: Transcoding the test source into an H.264 baseline and Opus MP4 ==="
# WebRTC needs H.264 baseline + Opus; source.flv is H.264 High + AAC. One MP4
# carries both tracks with their real timestamps, and baseline has no B-frames,
# which the tool refuses because WebRTC sends pictures in decode order.
"$FFMPEG_BIN" -hide_banner -loglevel error -y -t "$SOURCE_SECONDS" -i "$SOURCE_FLV" \
  -c:v libx264 -profile:v baseline -level 3.1 -pix_fmt yuv420p -preset ultrafast -r 25 \
  -c:a libopus -ar 48000 -ac 2 -movflags +faststart \
  -f mp4 "$SOURCE_MP4" >"$FFMPEG_LOG" 2>&1
if [[ ! -s "$SOURCE_MP4" ]]; then
  echo "Error: transcode failed, see $FFMPEG_LOG" >&2
  exit 1
fi
echo "Input: $SOURCE_MP4"

# Rebuild the tool when its binary is missing or older than its sources.
tool_is_stale() {
  [[ ! -x "$TOOL_BIN" ]] || [[ -n "$(find "$TOOL_DIR" -maxdepth 1 \( -name '*.go' -o -name 'go.mod' -o -name 'go.sum' \) -newer "$TOOL_BIN")" ]]
}
if tool_is_stale; then
  echo "=== Step 3: Building pion-whip ==="
  if ! ( cd "$TOOL_DIR" && mkdir -p objs && go build -o objs/pion-whip . ) >"$BUILD_LOG" 2>&1; then
    echo "Error: pion-whip build failed, see $BUILD_LOG" >&2
    cat "$BUILD_LOG" >&2
    exit 1
  fi
else
  echo "=== Step 3: pion-whip already built ==="
fi
echo "pion-whip: $TOOL_BIN"

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

echo "=== Step 4: Starting SRS with nack_prefer_rtx off ==="
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

echo "=== Step 5: Publishing over WHIP with pion-whip ==="
# The debug log level prints one line per NACK received and per resend.
"$TOOL_BIN" -hide_banner -loglevel debug \
  -re -stream_loop -1 -i "$SOURCE_MP4" \
  -f whip "$WHIP_URL" >"$TOOL_LOG" 2>&1 &
TOOL_PID=$!
echo "pion-whip PID: $TOOL_PID"

ESTABLISHED=0
for _ in {1..150}; do
  if grep -aq "RTC: Publisher url=/$STREAM_URL established" "$SRS_LOG"; then
    ESTABLISHED=1
    break
  fi
  if ! kill -0 "$TOOL_PID" 2>/dev/null; then
    break
  fi
  sleep 0.1
done
if [[ "$ESTABLISHED" != "1" ]]; then
  echo "FAIL: WHIP publisher was not established" >&2
  tail -40 "$TOOL_LOG" >&2
  exit 1
fi
echo "Publisher established."

echo "=== Step 6: Checking the offer carries rtx with a FID group ==="
OFFER=$(grep -a -m1 "RTC remote offer:" "$SRS_LOG" | sdp_lines || true)
OFFER_VIDEO=$(printf '%s\n' "$OFFER" | awk '/^m=video/ { on = 1 } /^m=audio/ { on = 0 } on')
if ! printf '%s\n' "$OFFER_VIDEO" | grep -q "^a=rtpmap:[0-9]* rtx/90000"; then
  echo "FAIL: offer has no rtx payload, the peer is not RTX-capable" >&2
  exit 1
fi
if ! printf '%s\n' "$OFFER_VIDEO" | grep -q "^a=fmtp:[0-9]* apt=[0-9]*"; then
  echo "FAIL: offer has no apt for its rtx payload" >&2
  exit 1
fi
MEDIA_SSRC=$(printf '%s\n' "$OFFER_VIDEO" | sed -n -E 's/^a=ssrc-group:FID ([0-9]+) ([0-9]+)$/\1/p' | head -1)
RTX_SSRC=$(printf '%s\n' "$OFFER_VIDEO" | sed -n -E 's/^a=ssrc-group:FID ([0-9]+) ([0-9]+)$/\2/p' | head -1)
if [[ -z "$MEDIA_SSRC" || -z "$RTX_SSRC" ]]; then
  echo "FAIL: offer has no a=ssrc-group:FID" >&2
  exit 1
fi
printf '%s\n' "$OFFER_VIDEO" | grep "rtx/90000\|apt=\|ssrc-group:FID" | sed 's/^/  offer: /'
echo "PASS: offer carries rtx, apt and FID; video ssrc=$MEDIA_SSRC, rtx ssrc=$RTX_SSRC."

echo "=== Step 7: Checking the SRS answer falls back to plain retransmission ==="
ANSWER=$(grep -a -m1 "RTC local answer:.*WMS $STREAM_URL" "$SRS_LOG" | sdp_lines || true)
ANSWER_VIDEO=$(printf '%s\n' "$ANSWER" | awk '/^m=video/ { on = 1 } /^m=audio/ { on = 0 } on')
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
printf '%s\n' "$ANSWER_VIDEO" | grep "^m=video\|^a=rtpmap:\|^a=rtcp-fb:$MEDIA_PT" | sed 's/^/  answer: /'
echo "PASS: answer keeps H264 pt=$MEDIA_PT with no rtx, apt or FID."

echo "=== Step 8: Waiting for video frames to reach SRS ==="
FRAMES=0
for _ in {1..100}; do
  FRAMES=$(curl --silent --fail "$API/api/v1/streams/" \
    | jq -r --arg name "$STREAM_NAME" '[.streams[]? | select(.name == $name and .publish.active == true) | .video_frames][0] // 0')
  if [[ "$FRAMES" -gt 0 ]]; then
    break
  fi
  sleep 0.1
done
if [[ "$FRAMES" -le 0 ]]; then
  echo "FAIL: stream $STREAM_URL is not publishing video" >&2
  exit 1
fi
echo "Video frames received: $FRAMES"

echo "=== Step 9: Dropping $DROP received packets through the NACK simulator ==="
USERNAME=$(grep -a -m1 "RTC init session, user=.*, url=/$STREAM_URL," "$SRS_LOG" \
  | sed -E 's/.*RTC init session, user=([^,]*),.*/\1/' || true)
if [[ -z "$USERNAME" ]]; then
  echo "FAIL: session username not found in the SRS log" >&2
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
VIDEO_DROPS=$(grep -ac "NACK simulator #.*/Video" "$SRS_LOG" || true)
if [[ "$VIDEO_DROPS" -le 0 ]]; then
  echo "FAIL: the simulator dropped no video packet, raise SRS_PLAIN_DROP" >&2
  dump_evidence
  exit 1
fi
echo "Video packets dropped: $VIDEO_DROPS"

echo "=== Step 10: Waiting for a plain retransmission to be recovered (up to ${WAIT}s) ==="
# The simulator build logs every NACK request and every recovery, so the chain
# is verified packet by packet: SRS requests a sequence on the video SSRC and
# the same sequence is then recovered on the media SSRC with the media payload
# type, with no RTX packet in between. An RTX answer would show a "NACK: RTX
# recv" line on the RTX SSRC before the recovery, and the rtx=(s,r,u,p) counter.
requested_seqs() {
  grep -a "NACK: request ssrc=$MEDIA_SSRC, seqs=\[" "$SRS_LOG" \
    | sed -E 's/.*seqs=\[([0-9,]*)\].*/\1/' | tr ',' '\n' | sort -u
}
RECOVERED=0
for _ in $(seq 1 "$WAIT"); do
  RECOVERED=0
  for seq in $(requested_seqs); do
    if grep -aqE "NACK: recovered seq=$seq, ssrc=$MEDIA_SSRC, pt=$MEDIA_PT([^0-9]|$)" "$SRS_LOG"; then
      RECOVERED=$((RECOVERED + 1))
    fi
  done
  if [[ "$RECOVERED" -gt 0 ]]; then
    break
  fi
  sleep 1
done

GAPS=$(grep -ac "NACK: update" "$SRS_LOG" || true)
REQUESTS=$(grep -ac "NACK: request ssrc=$MEDIA_SSRC, seqs=\[" "$SRS_LOG" || true)
RTX_RECVS=$(grep -ac "NACK: RTX recv" "$SRS_LOG" || true)
RTX_COUNTERS=$(grep -ac "rtx=(" "$SRS_LOG" || true)
TOOL_NACKS=$(grep -ac "NACK received ssrc=$MEDIA_SSRC, seqs=\[" "$TOOL_LOG" || true)
TOOL_PLAIN=$(grep -ac "Resend plain seq=" "$TOOL_LOG" || true)
TOOL_RTX=$(grep -ac "Resend RTX seq=" "$TOOL_LOG" || true)
echo "SRS gaps: $GAPS, requests: $REQUESTS, recovered: $RECOVERED, RTX received: $RTX_RECVS, rtx counters: $RTX_COUNTERS"
echo "pion-whip NACKs received: $TOOL_NACKS, plain resends: $TOOL_PLAIN, RTX resends: $TOOL_RTX"

if [[ "$RECOVERED" -le 0 ]]; then
  if [[ "$GAPS" -le 0 ]]; then
    echo "FAIL: SRS detected no sequence gap after the drops, so it requested no NACK" >&2
  elif [[ "$TOOL_NACKS" -le 0 ]]; then
    echo "FAIL: SRS requested a NACK but pion-whip received none" >&2
  elif [[ "$TOOL_PLAIN" -le 0 ]]; then
    echo "FAIL: pion-whip received a NACK but sent no plain resend" >&2
  else
    echo "FAIL: pion-whip resent the packet but SRS recovered none of the requested sequences" >&2
  fi
  dump_evidence
  exit 1
fi
if [[ "$RTX_RECVS" -gt 0 || "$RTX_COUNTERS" -gt 0 || "$TOOL_RTX" -gt 0 ]]; then
  echo "FAIL: RTX was used although the answer negotiated plain retransmission" >&2
  dump_evidence
  exit 1
fi
grep -a "NACK: request ssrc=$MEDIA_SSRC,\|NACK: recovered seq=" "$SRS_LOG" | head -4 | sed 's/^/  srs: /'
grep -a "NACK received ssrc=$MEDIA_SSRC,\|Resend plain seq=" "$TOOL_LOG" | head -4 | sed 's/^/  pion-whip: /'
echo "PASS: a NACK request was answered by a plain resend and recovered on the media SSRC."

TEST_PASSED=1
echo ""
echo "=== E2E WebRTC Plain Retransmission Publish Test PASSED ==="
