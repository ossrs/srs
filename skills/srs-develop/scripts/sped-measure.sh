#!/bin/bash
# Measure the WebRTC setup time with and without SPED (the DTLS handshake
# inside the ICE STUN checks, draft-hancke-webrtc-sped). It starts an SRS
# with sped on from trunk/objs/srs, then runs `srs-integ measure` against it:
# a raw WHIP publisher sends the WHIP test file, and raw WHEP players connect
# over paths with a one-way delay and a loss of their STUN and DTLS, each
# offering sped or not. A player that doesn't offer it gets no SPED from
# SRS, as with sped off. It prints the host, the build and one row per point:
# delay, loss, sped, runs, failures, and the p50 and p95 in ms from the offer
# to the DTLS handshake done and to the first RTP packet.
#
# Usage: sped-measure.sh [measure flags]
#   The flags go to `srs-integ measure`, such as -runs 5 -delays 0ms,60ms
#   -losses 0 -sped off,on; `bin/srs-integ measure -h` lists them. The
#   default is the whole matrix: 0/20/60/150 ms, 0/10/25% loss, both modes,
#   30 runs each.
#
# Environment:
#   SRS_TEST_PORT_BASE=<port>  RTMP +0, HTTP API +1, RTC UDP +2 (default 16800)
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
MEDIA="$WORKSPACE/cmd/integ/testdata/media/h264-baseline-opus.mp4"
PORT_BASE=${SRS_TEST_PORT_BASE:-16800}
RTMP_PORT=$((PORT_BASE + 0))
API_PORT=$((PORT_BASE + 1))
RTC_PORT=$((PORT_BASE + 2))
API="http://127.0.0.1:$API_PORT"
TEST_DIR=/tmp/srs-sped-measure
SRS_LOG="$TEST_DIR/srs.log"
SRS_PID=""

for tool in curl jq make go; do
  command -v "$tool" >/dev/null 2>&1 || { echo "Error: $tool is required" >&2; exit 1; }
done
if [[ ! -x "$SRS_BINARY" ]]; then
  echo "Error: no $SRS_BINARY; build SRS first" >&2
  exit 1
fi

cleanup() {
  if [[ -n "$SRS_PID" ]] && kill -0 "$SRS_PID" 2>/dev/null; then
    kill "$SRS_PID" 2>/dev/null || true
    for _ in {1..20}; do
      kill -0 "$SRS_PID" 2>/dev/null || break
      sleep 0.1
    done
    kill -9 "$SRS_PID" 2>/dev/null || true
  fi
  [[ -z "$SRS_PID" ]] || wait "$SRS_PID" 2>/dev/null || true
}
trap cleanup EXIT

rm -rf /tmp/srs-sped-measure
mkdir -p "$TEST_DIR"

make -s -C "$WORKSPACE" bin/srs-integ >"$TEST_DIR/build.log" 2>&1 || { cat "$TEST_DIR/build.log" >&2; exit 1; }
if [[ ! -f "$MEDIA" ]]; then
  make -s -C "$WORKSPACE" integ-prepare >"$TEST_DIR/prepare.log" 2>&1 || { cat "$TEST_DIR/prepare.log" >&2; exit 1; }
fi

(
  cd "$WORKSPACE/trunk"
  exec env SRS_RTMP_LISTEN=$RTMP_PORT SRS_HTTP_API_ENABLED=on SRS_HTTP_API_LISTEN=$API_PORT \
    SRS_RTC_SERVER_ENABLED=on SRS_RTC_SERVER_LISTEN=$RTC_PORT SRS_RTC_SERVER_CANDIDATE=127.0.0.1 \
    SRS_VHOST_RTC_ENABLED=on SRS_VHOST_RTC_SPED=on SRS_VHOST_RTC_RTC_TO_RTMP=off SRS_LOG_TANK=console \
    "$SRS_BINARY" -e >"$SRS_LOG" 2>&1
) &
SRS_PID=$!
for _ in {1..50}; do
  curl -fsS --max-time 1 "$API/api/v1/versions" >"$TEST_DIR/version.json" 2>/dev/null && break
  kill -0 "$SRS_PID" 2>/dev/null || { echo "Error: SRS exited, see $SRS_LOG" >&2; tail -20 "$SRS_LOG" >&2; exit 1; }
  sleep 0.1
done
[[ -s "$TEST_DIR/version.json" ]] || { echo "Error: SRS API did not start, see $SRS_LOG" >&2; exit 1; }

echo "HOST $(uname -sm), $(sysctl -n machdep.cpu.brand_string 2>/dev/null || uname -p)"
echo "SRS $(jq -r '.data.version' "$TEST_DIR/version.json"), commit $(git -C "$WORKSPACE" rev-parse --short HEAD)"
echo "BUILD $(sed -n 's/^#define SRS_USER_CONFIGURE "\(.*\)"$/\1/p' "$WORKSPACE/trunk/objs/srs_auto_headers.hpp")"
status=0
"$WORKSPACE/bin/srs-integ" measure -file "$MEDIA" "$@" "$API" || status=$?
if [[ $status -ne 0 ]]; then
  echo "Error: measure exited $status, SRS log in $SRS_LOG" >&2
fi
echo "SRS: $(grep -ac 'RTC init session,.* sped=1,' "$SRS_LOG" || true) sessions negotiated SPED"
exit $status
