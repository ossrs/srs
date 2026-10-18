#!/bin/bash
# OBS on this Mac publishes RTMP, SRT and WHIP to SRS built and run for Linux in Docker.
#
#   obs-linux-test.sh [--build]
#
# SRS_TEST_OBS=0 skips; without a running Docker it prints SKIP linux: unverified: Docker not available.
#   - The build step copies the checkout's tracked trunk files, with their modification times, into the
#     named volume srs-obs-linux-build and builds objs/srs there in the ossrs/srs:ubuntu20 image, for
#     Linux on the Docker host's CPU. make rebuilds only what changed, and the Mac's trunk/objs keeps its
#     own flags. --build stops after this step.
#   - SRS starts env-only in a container on the slot at SRS_TEST_PORT_BASE (default 15400): RTMP +0,
#     HTTP +1, API +2 over TCP, RTC +3, SRT +4 over UDP, published on 127.0.0.1 only. The container is
#     removed on exit.
#   - The RTMP, SRT and WHIP cases run in parallel from isolated OBS instances on this Mac through
#     obs-test-lib.sh, WHIP with eip=127.0.0.1.
#
# Prints the build time, each case's log and result, and RESULT linux; the cases fail over 60 s. Exits 0
# when passed or skipped, 1 when the build or a case failed, and 3 for bad arguments.
set -eu
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
LOG_DIR=/tmp/srs-obs-linux
IMAGE=ossrs/srs:ubuntu20
VOLUME=srs-obs-linux-build
# The Full-tier flags without --utest: the container runs only the server.
FLAGS="--srt=on --gb28181=on --rtsp=on --sanitizer=on --ffmpeg-opus=off --simulator=on"
SLOT=${SRS_TEST_PORT_BASE:-15400}
CONTAINER=

usage() {
  echo "Usage: $0 [--build]" >&2
  exit 3
}

check_slot() {
  case "$SLOT" in ''|*[!0-9]*) echo 'FAIL invalid port base' >&2; exit 3;; esac
  [[ $SLOT -ge 11000 && $((SLOT + 99)) -lt 32768 ]] || { echo 'FAIL port slot outside test range' >&2; exit 3; }
}

# On the Mac: copy the tracked trunk files into the volume and build SRS there.
build() {
  local checkout sha started
  checkout=$(cd "$SCRIPT_DIR/../../.." && pwd)
  sha=$(git -C "$checkout" rev-parse --short HEAD)
  git -C "$checkout" diff --quiet HEAD -- trunk || sha="$sha+dirty"
  mkdir -p "$LOG_DIR"
  started=$SECONDS
  if ! git -C "$checkout" ls-files -z trunk | docker run --rm -i --name srs-obs-linux-build \
    -v $VOLUME:/build -v "$checkout":/src:ro -v "$SCRIPT_DIR":/scripts:ro $IMAGE \
    bash /scripts/obs-linux-test.sh --container-build >"$LOG_DIR/build.log" 2>&1; then
    tail -n 20 "$LOG_DIR/build.log"
    echo "FAIL Linux build of $sha (log: $LOG_DIR/build.log)"
    exit 1
  fi
  echo "BUILD linux $sha $((SECONDS - started))s"
}

# In the build container: the file list arrives on stdin; tar keeps each file's modification time.
container_build() {
  mkdir -p /build/srs
  tar -C /src -cf - --null -T - | tar -C /build/srs -xf -
  cd /build/srs/trunk
  if ! grep -q "\"$FLAGS\"" objs/srs_auto_headers.hpp 2>/dev/null || [[ configure -nt objs/Makefile ]]; then
    ./configure $FLAGS
  fi
  make -j"$(nproc)" server
}

# In the SRS container: run SRS env-only on the slot in the foreground.
container_srs() {
  local status=0
  OBS_WORKSPACE=/build/srs
  OBS_TEST_DIR=$LOG_DIR/srs
  source /scripts/obs-test-lib.sh
  obs_test_ports "$SLOT"
  mkdir -p "$OBS_TEST_DIR/html"
  obs_test_start_srs
  wait "$OBS_SRS_PID" || status=$?
  tail -n 20 "$OBS_TEST_DIR/srs.log"
  exit "$status"
}

stop() {
  [[ -z "${PIDS:-}" ]] || kill -TERM $PIDS 2>/dev/null || true
  wait 2>/dev/null || true
  [[ -z "$CONTAINER" ]] || docker rm -f "$CONTAINER" >/dev/null 2>&1 || true
}

# On the Mac: start SRS in a container and wait for its API.
start_srs() {
  local port deadline started=$SECONDS
  local publish="-p 127.0.0.1:$SLOT:$SLOT"
  for port in $((SLOT + 1)) $((SLOT + 2)); do publish="$publish -p 127.0.0.1:$port:$port"; done
  for port in $((SLOT + 3)) $((SLOT + 4)); do publish="$publish -p 127.0.0.1:$port:$port/udp"; done
  rm -rf /tmp/srs-obs-linux/srs
  mkdir -p "$LOG_DIR/srs"
  trap stop EXIT
  trap 'exit 130' INT
  trap 'exit 143' TERM
  CONTAINER=$(docker run -d --rm --name "srs-obs-linux-$SLOT" -e SRS_TEST_PORT_BASE="$SLOT" $publish \
    -v $VOLUME:/build -v "$LOG_DIR/srs":"$LOG_DIR/srs" -v "$SCRIPT_DIR":/scripts:ro $IMAGE \
    bash /scripts/obs-linux-test.sh --container-srs) || { echo "FAIL start container srs-obs-linux-$SLOT"; exit 1; }
  deadline=$((SECONDS + 15))
  until curl -fsS --max-time 1 "http://127.0.0.1:$((SLOT + 2))/api/v1/versions" >/dev/null 2>&1; do
    if [[ "$(docker inspect -f '{{.State.Running}}' "$CONTAINER" 2>/dev/null)" != true ]]; then
      tail -n 20 "$LOG_DIR/srs/srs.log" 2>/dev/null || true
      echo "FAIL SRS exited in the Linux container (log: $LOG_DIR/srs/srs.log)"
      exit 1
    fi
    [[ $SECONDS -lt $deadline ]] || { echo 'FAIL SRS API startup timeout in the Linux container'; exit 1; }
    sleep 0.2
  done
  echo "SRS linux 127.0.0.1 slot $SLOT $((SECONDS - started))s"
}

# On the Mac: one OBS case against SRS in the container.
run_case() {
  local name=$1
  source "$SCRIPT_DIR/obs-test-lib.sh"
  export OBS_SRS_HOST=127.0.0.1
  obs_test_init "linux-$name" "$SLOT"
  obs_test_start
  case "$name" in
    rtmp)
      obs_test_setup --rtmp "rtmp://127.0.0.1:$OBS_RTMP_PORT/live/$OBS_INSTANCE_ID"
      obs_test_assert_api H264 AAC
      obs_test_play http-flv aac
      obs_test_play hls aac;;
    srt)
      obs_test_setup --srt "srt://127.0.0.1:$OBS_SRT_PORT" --srt-streamid "#!::r=live/$OBS_INSTANCE_ID,m=publish"
      obs_test_assert_api H264 AAC
      obs_test_play http-flv aac;;
    whip)
      obs_test_setup --whip "http://127.0.0.1:$OBS_API_PORT/rtc/v1/whip/?app=live&stream=$OBS_INSTANCE_ID" \
        --eip 127.0.0.1
      # The RTC-to-RTMP bridge starts at the second keyframe, as in obs-whip-publish-test.sh.
      OBS_API_WAIT=15 obs_test_assert_api H264 AAC
      obs_test_assert_frames
      obs_test_play http-flv aac;;
  esac
  obs_test_finish
}

main() {
  local build_only=$1 probe started elapsed name status passed=0 known=0 failed=0 case_known
  PIDS=
  check_slot
  [[ "${SRS_TEST_OBS:-1}" != 0 ]] || { echo 'SKIP SRS_TEST_OBS=0'; exit 0; }
  docker info >/dev/null 2>&1 || { echo 'SKIP linux: unverified: Docker not available'; exit 0; }
  if [[ -z "$build_only" ]]; then
    source "$SCRIPT_DIR/obs-test-lib.sh"
    probe=$("${OBS_PYTHON:-python3}" "$SCRIPT_DIR/../../internal-obs-control/scripts/obsws.py" probe --json 2>/dev/null) \
      && obs_test_probe_available "$probe" || { echo 'SKIP linux: OBS not available'; exit 0; }
  fi
  build
  [[ -z "$build_only" ]] || exit 0
  start_srs
  started=$SECONDS
  # Start the cases 2 s apart, as obs-lane-test.sh does.
  for name in rtmp srt whip; do
    [[ -z "$PIDS" ]] || sleep 2
    SRS_TEST_PORT_BASE=$SLOT bash "$SCRIPT_DIR/obs-linux-test.sh" --case "$name" >"$LOG_DIR/$name.log" 2>&1 &
    PIDS="$PIDS $!"
  done
  set -- $PIDS
  for name in rtmp srt whip; do
    status=0
    wait "$1" || status=$?
    shift
    cat "$LOG_DIR/$name.log"
    case_known=$(sed -n 's/^RESULT .* known=\([0-9]*\) .*/\1/p' "$LOG_DIR/$name.log" | tail -n 1)
    known=$((known + ${case_known:-0}))
    if [[ $status -ne 0 ]]; then
      failed=$((failed + 1)); echo "LANE linux-$name failed exit=$status"
    elif grep -q '^RESULT ' "$LOG_DIR/$name.log"; then
      passed=$((passed + 1)); echo "LANE linux-$name passed"
    else
      echo "LANE linux-$name skipped"
    fi
  done
  PIDS=
  elapsed=$((SECONDS - started))
  [[ $elapsed -le 60 ]] || { failed=$((failed + 1)); echo "FAIL budget ${elapsed}s > 60s"; }
  echo "RESULT linux passed=$passed known=$known failed=$failed cases=${elapsed}s"
  [[ $failed -eq 0 ]]
}

case "${1:-}" in
  ''|--build) [[ $# -le 1 ]] || usage; main "${1:-}";;
  --case) [[ $# -eq 2 && ( $2 == rtmp || $2 == srt || $2 == whip ) ]] || usage; check_slot; run_case "$2";;
  --container-build) [[ $# -eq 1 ]] || usage; container_build;;
  --container-srs) [[ $# -eq 1 ]] || usage; check_slot; container_srs;;
  *) usage;;
esac
