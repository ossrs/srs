#!/bin/bash
# E2E test for RFC 4588 RTX on the WebRTC publish (uplink) path. It starts a
# disposable SRS built with the NACK drop simulator, which every E2E script
# here enables, with nack_prefer_rtx on, publishes over WHIP with FFmpeg (which
# offers rtx with a FID group and answers a NACK with an RTX packet), then:
#   1. checks the FFmpeg offer carries rtx, apt and a=ssrc-group:FID;
#   2. checks the SRS answer carries a=rtpmap:<pt> rtx/90000 and
#      a=fmtp:<pt> apt=<media pt>, and no FID group of its own;
#   3. drops received packets through the NACK simulator API so SRS must
#      request a retransmission;
#   4. expects the SRS console counters rtx=(s,r,u,p) to report RTX packets
#      received (r) and unwrapped (u) from the publisher;
#   5. expects the detail NACK logs of the simulator build to show the chain
#      packet by packet: "NACK: request" for the video SSRC, "NACK: RTX recv"
#      on the RTX SSRC with an osn, and "NACK: recovered" for that same osn.
# On failure it prints where the chain broke: no video packet dropped, no gap
# detected by SRS, no RTX sent by FFmpeg, or RTX sent but not counted by SRS.
#
# Environment:
#   SRS_RTX_DROP=<n>       packets to drop through the simulator (default 30)
#   SRS_RTX_WAIT=<s>       seconds to wait for the RTX counters (default 30)
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
RTMP_PORT="${SRS_RTX_RTMP_PORT:-31935}"
HTTP_API_PORT="${SRS_RTX_HTTP_API_PORT:-31985}"
RTC_PORT="${SRS_RTX_RTC_PORT:-38000}"
DROP="${SRS_RTX_DROP:-30}"
WAIT="${SRS_RTX_WAIT:-30}"
STREAM_NAME="rtx$(date +%s)"
STREAM_URL="live/$STREAM_NAME"
WHIP_URL="http://127.0.0.1:$HTTP_API_PORT/rtc/v1/whip/?app=live&stream=$STREAM_NAME"
API="http://127.0.0.1:$HTTP_API_PORT"
TEST_DIR=$(mktemp -d "${TMPDIR:-/tmp}/srs-rtc-rtx-publish.XXXXXX")
SRS_LOG="$TEST_DIR/srs.log"
FFMPEG_LOG="$TEST_DIR/ffmpeg.log"
BUILD_LOG="$TEST_DIR/build.log"
SRS_PID=""
FFMPEG_PID=""
TEST_PASSED=0

cleanup() {
  echo ""
  echo "=== Cleaning up ==="
  for pid in $FFMPEG_PID $SRS_PID; do
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
    echo "  SRS:    $SRS_LOG" >&2
    echo "  FFmpeg: $FFMPEG_LOG" >&2
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
  echo "--- SRS NACK gap detection and RTX ---" >&2
  grep -a "NACK: update\|Drop invalid RTX\|rtx=" "$SRS_LOG" | tail -8 >&2 || true
  echo "--- SRS NACK detail logs ---" >&2
  grep -a "NACK: request\|NACK: RTX\|NACK: recovered" "$SRS_LOG" | tail -8 >&2 || true
  echo "--- FFmpeg NACK and RTX ---" >&2
  grep -a "RTX\|NACK" "$FFMPEG_LOG" | tail -8 >&2 || true
}

echo "=== E2E WebRTC RTX Publish Test ==="
echo "Workspace: $WORKSPACE"
echo "Stream:    $STREAM_URL"
echo "Drop:      $DROP packets, wait up to ${WAIT}s for RTX"
echo ""

for tool in curl jq make; do
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

echo "=== Step 2: Starting SRS with nack_prefer_rtx on ==="
(
  cd "$WORKSPACE/trunk"
  exec env SRS_RTMP_LISTEN=$RTMP_PORT \
    SRS_HTTP_API_ENABLED=on SRS_HTTP_API_LISTEN=$HTTP_API_PORT \
    SRS_RTC_SERVER_ENABLED=on SRS_RTC_SERVER_LISTEN=$RTC_PORT SRS_RTC_SERVER_CANDIDATE=127.0.0.1 \
    SRS_VHOST_RTC_ENABLED=on SRS_VHOST_RTC_NACK_PREFER_RTX=on SRS_VHOST_RTC_RTC_TO_RTMP=off \
    "$SRS_BINARY" -e >"$SRS_LOG" 2>&1
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

echo "=== Step 3: Publishing over WHIP with FFmpeg ==="
# WebRTC needs H.264 baseline + Opus; source.flv is H.264 High + AAC. The debug
# log level is needed to see FFmpeg answer a NACK ("Found RTP history packet for RTX").
"$FFMPEG_BIN" -hide_banner -loglevel debug -stream_loop -1 -re -i "$SOURCE_FLV" \
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

echo "=== Step 4: Checking the FFmpeg offer carries rtx with a FID group ==="
OFFER=$(grep -a -m1 "RTC remote offer:" "$SRS_LOG" | sdp_lines || true)
OFFER_VIDEO=$(printf '%s\n' "$OFFER" | awk '/^m=video/ { on = 1 } /^m=audio/ { on = 0 } on')
if ! printf '%s\n' "$OFFER_VIDEO" | grep -q "^a=rtpmap:[0-9]* rtx/90000"; then
  echo "FAIL: offer has no rtx payload, this FFmpeg cannot exercise RTX" >&2
  exit 1
fi
if ! printf '%s\n' "$OFFER_VIDEO" | grep -q "^a=ssrc-group:FID [0-9]* [0-9]*"; then
  echo "FAIL: offer has no a=ssrc-group:FID" >&2
  exit 1
fi
printf '%s\n' "$OFFER_VIDEO" | grep "rtx/90000\|apt=\|ssrc-group:FID" | sed 's/^/  offer: /'
echo "PASS: offer carries rtx, apt and FID."

echo "=== Step 5: Checking the SRS answer negotiates rtx for the video codec ==="
ANSWER=$(grep -a -m1 "RTC local answer:.*WMS $STREAM_URL" "$SRS_LOG" | sdp_lines || true)
ANSWER_VIDEO=$(printf '%s\n' "$ANSWER" | awk '/^m=video/ { on = 1 } /^m=audio/ { on = 0 } on')
if [[ -z "$ANSWER_VIDEO" ]]; then
  echo "FAIL: no video section in the SRS answer" >&2
  exit 1
fi
MEDIA_PT=$(printf '%s\n' "$ANSWER_VIDEO" | sed -n -E 's/^a=rtpmap:([0-9]+) H264\/90000$/\1/p' | head -1)
RTX_PT=$(printf '%s\n' "$ANSWER_VIDEO" | sed -n -E 's/^a=rtpmap:([0-9]+) rtx\/90000$/\1/p' | head -1)
if [[ -z "$MEDIA_PT" ]]; then
  echo "FAIL: answer has no H264 payload" >&2
  printf '%s\n' "$ANSWER_VIDEO" >&2
  exit 1
fi
if [[ -z "$RTX_PT" ]]; then
  echo "FAIL: answer has no a=rtpmap:<pt> rtx/90000, RTX was not negotiated" >&2
  printf '%s\n' "$ANSWER_VIDEO" >&2
  exit 1
fi
if ! printf '%s\n' "$ANSWER_VIDEO" | grep -q "^a=fmtp:$RTX_PT apt=$MEDIA_PT$"; then
  echo "FAIL: answer lacks a=fmtp:$RTX_PT apt=$MEDIA_PT" >&2
  printf '%s\n' "$ANSWER_VIDEO" >&2
  exit 1
fi
# The publisher owns the sending SSRCs, so the publish answer declares no FID group.
if printf '%s\n' "$ANSWER_VIDEO" | grep -q "^a=ssrc-group:FID"; then
  echo "FAIL: publish answer must not carry a=ssrc-group:FID" >&2
  printf '%s\n' "$ANSWER_VIDEO" >&2
  exit 1
fi
printf '%s\n' "$ANSWER_VIDEO" | grep "^m=video\|rtx/90000\|apt=" | sed 's/^/  answer: /'
echo "PASS: answer negotiates rtx pt=$RTX_PT for H264 pt=$MEDIA_PT."

echo "=== Step 6: Waiting for video frames to reach SRS ==="
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

echo "=== Step 7: Dropping $DROP received packets through the NACK simulator ==="
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
  echo "FAIL: the simulator dropped no video packet, raise SRS_RTX_DROP" >&2
  dump_evidence
  exit 1
fi
echo "Video packets dropped: $VIDEO_DROPS"

echo "=== Step 8: Waiting for RTX packets from the publisher (up to ${WAIT}s) ==="
# The counters are cumulative totals since server start, printed every 5s once
# any is nonzero. rtx=(s,r,u,p): sent, received, unwrapped, padding-only dropped.
RTX_RECV=0
RTX_UNWRAP=0
for _ in $(seq 1 "$WAIT"); do
  RTX_LINE=$(grep -a "rtx=(" "$SRS_LOG" | tail -1 || true)
  if [[ -n "$RTX_LINE" ]]; then
    RTX_RECV=$(printf '%s' "$RTX_LINE" | sed -E 's/.*rtx=\(s:[0-9]+,r:([0-9]+),u:[0-9]+,p:[0-9]+\).*/\1/')
    RTX_UNWRAP=$(printf '%s' "$RTX_LINE" | sed -E 's/.*rtx=\(s:[0-9]+,r:[0-9]+,u:([0-9]+),p:[0-9]+\).*/\1/')
    if [[ "$RTX_RECV" -gt 0 && "$RTX_UNWRAP" -gt 0 ]]; then
      break
    fi
  fi
  sleep 1
done

GAPS=$(grep -ac "NACK: update" "$SRS_LOG" || true)
FFMPEG_RTX=$(grep -ac "Found RTP history packet for RTX" "$FFMPEG_LOG" || true)
echo "SRS gaps detected: $GAPS, FFmpeg RTX sent: $FFMPEG_RTX, SRS RTX received: $RTX_RECV, unwrapped: $RTX_UNWRAP"

if [[ "$RTX_RECV" -le 0 || "$RTX_UNWRAP" -le 0 ]]; then
  if [[ "$GAPS" -le 0 ]]; then
    echo "FAIL: SRS detected no sequence gap after the drops, so it requested no NACK" >&2
  elif [[ "$FFMPEG_RTX" -le 0 ]]; then
    echo "FAIL: SRS requested a NACK but FFmpeg sent no RTX packet" >&2
  else
    echo "FAIL: FFmpeg sent RTX packets but SRS counted none as received and unwrapped" >&2
  fi
  dump_evidence
  exit 1
fi
echo "PASS: SRS received and unwrapped RTX packets from the publisher."

echo "=== Step 9: Checking the detail NACK logs of the simulator build ==="
# The simulator build logs every NACK request and every retransmission, so the
# chain is verified packet by packet rather than by the total counters alone:
# SRS requests a sequence on the video SSRC, the RTX packet arrives on the RTX
# SSRC carrying that sequence as osn, and the same sequence is then recovered
# on the media SSRC. A plain retransmission would show only the last line.
MEDIA_SSRC=$(printf '%s\n' "$OFFER_VIDEO" | sed -n -E 's/^a=ssrc-group:FID ([0-9]+) ([0-9]+)$/\1/p' | head -1)
RTX_SSRC=$(printf '%s\n' "$OFFER_VIDEO" | sed -n -E 's/^a=ssrc-group:FID ([0-9]+) ([0-9]+)$/\2/p' | head -1)
REQUESTS=$(grep -ac "NACK: request ssrc=$MEDIA_SSRC, seqs=\[" "$SRS_LOG" || true)
RTX_RECVS=$(grep -ac "NACK: RTX recv ssrc=$RTX_SSRC, pt=$RTX_PT, seq=[0-9]*, osn=[0-9]*, media ssrc=$MEDIA_SSRC, pt=$MEDIA_PT," "$SRS_LOG" || true)
RECOVERED_BY_RTX=0
for osn in $(grep -a "NACK: RTX recv ssrc=$RTX_SSRC," "$SRS_LOG" | sed -E 's/.*osn=([0-9]+).*/\1/' || true); do
  if grep -aq "NACK: recovered seq=$osn, ssrc=$MEDIA_SSRC, pt=$MEDIA_PT" "$SRS_LOG"; then
    RECOVERED_BY_RTX=$((RECOVERED_BY_RTX + 1))
  fi
done
echo "NACK requests: $REQUESTS, RTX received: $RTX_RECVS, recovered by RTX: $RECOVERED_BY_RTX"
if [[ "$REQUESTS" -le 0 || "$RTX_RECVS" -le 0 || "$RECOVERED_BY_RTX" -le 0 ]]; then
  echo "FAIL: the detail NACK logs do not show a request answered by RTX and recovered" >&2
  dump_evidence
  exit 1
fi
grep -a "NACK: request ssrc=$MEDIA_SSRC,\|NACK: RTX recv ssrc=$RTX_SSRC,\|NACK: recovered seq=" "$SRS_LOG" | head -6 | sed 's/^/  log: /'
echo "PASS: the detail logs show a NACK request answered by RTX and recovered."

TEST_PASSED=1
echo ""
echo "=== E2E WebRTC RTX Publish Test PASSED ==="
