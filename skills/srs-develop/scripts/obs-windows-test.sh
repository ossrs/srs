#!/bin/bash
# OBS on the Windows test host publishes RTMP and WHIP to SRS built and run in WSL on the same host.
#
#   obs-windows-test.sh [--wip] [--build]
#
# Opt in with SRS_TEST_WINDOWS_HOST; SRS_TEST_OBS=0 skips. windows-host-run.sh syncs this checkout to
# the host, with --wip as a snapshot of the working tree, and runs the --host step in the host checkout:
#   - WSL builds SRS in the checkout at the same path under the WSL home, a worktree of the main
#     repository at the same path there, fetching the commit from the host's main repository. The first
#     run adds the worktree and copies the main repository's third-party libraries; it configures when
#     objs has no matching flags or configure is newer than objs/Makefile.
#   - SRS starts env-only in WSL on slot 15900: RTMP +0, HTTP +1, API +2, RTC UDP +3. WSL in NAT mode
#     takes UDP only on its own IP, so OBS publishes to that IP, and WHIP adds eip=<WSL IP>.
#   - The RTMP and WHIP cases run in parallel from isolated OBS instances on Windows through
#     obs-test-lib.sh, then SRS is stopped by its pid file.
#   - --build stops after the WSL build, as the setup step of the Full tier.
#
# The host needs WSL with the SRS repository cloned at the same path, Python 3.9+, FFmpeg, jq and curl
# in Git Bash, and OBS with obs-websocket. Prints the build time, each case's log and result, and
# RESULT windows; the cases fail over 60 s. Exits 0 when passed or skipped, 1 when a case or the build
# failed, and 3 for bad arguments or a failed host setup.
set -eu
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
LOG_DIR=/tmp/srs-obs-windows
SLOT=15900
WSL_FLAGS="--srt=on --ffmpeg-opus=off"

usage() {
  echo "Usage: $0 [--wip] [--build]" >&2
  exit 3
}

# On the Mac: sync the checkout to the host and run the --host step there.
mac_main() {
  local wip=$1 build=$2 checkout rel main main_rel status=0
  [[ "${SRS_TEST_OBS:-1}" != 0 ]] || { echo 'SKIP SRS_TEST_OBS=0'; exit 0; }
  [[ -n "${SRS_TEST_WINDOWS_HOST:-}" ]] || { echo 'SKIP windows: SRS_TEST_WINDOWS_HOST is not set'; exit 0; }
  checkout=$(cd "$SCRIPT_DIR/../../.." && pwd)
  main=$(dirname "$(git -C "$checkout" rev-parse --path-format=absolute --git-common-dir)")
  rel=${checkout#$HOME/}
  main_rel=${main#$HOME/}
  bash "$SCRIPT_DIR/windows-host-run.sh" $wip "$checkout" "$SCRIPT_DIR/obs-windows-test.sh" --host "$rel" "$main_rel" \
    $build || status=$?
  # windows-host-run.sh exits 2 after its own SKIP line.
  [[ $status -ne 2 ]] || exit 0
  exit "$status"
}

wsl_run() {
  MSYS_NO_PATHCONV=1 wsl.exe --cd "$(cygpath -w "$PWD/skills/srs-develop/scripts")" -e bash obs-windows-test.sh "$@"
}

host_stop() {
  wsl_run --wsl-stop || true
  wait "$SRS_JOB" 2>/dev/null || true
}

# In Git Bash on the host, in the host checkout: build and start SRS in WSL, run the cases.
host_main() {
  local rel=$1 main_rel=$2 build=$3 here=$PWD/skills/srs-develop/scripts sha ip started elapsed name status pids=
  local passed=0 known=0 failed=0 case_known
  [[ -f "$here/obs-test-lib.sh" ]] || { echo "SETUP_FAIL: $PWD is not an SRS checkout"; exit 3; }
  if [[ -z "$build" ]]; then
    # The Windows python3 can be the Microsoft Store stub, which never runs.
    for OBS_PYTHON in python3 python ''; do
      [[ -z "$OBS_PYTHON" ]] || ! "$OBS_PYTHON" --version >/dev/null 2>&1 || break
    done
    [[ -n "$OBS_PYTHON" ]] || { echo 'SKIP windows: no Python'; exit 0; }
    export OBS_PYTHON
  fi
  command -v wsl.exe >/dev/null || { echo 'SKIP windows: no WSL'; exit 0; }
  mkdir -p "$LOG_DIR"
  sha=$(git rev-parse HEAD)
  started=$SECONDS
  if ! wsl_run --wsl-build "$sha" "$(cygpath -w "$HOME/$main_rel")" "$main_rel" "$rel" >"$LOG_DIR/build.log" 2>&1; then
    tail -n 20 "$LOG_DIR/build.log"
    echo "FAIL WSL build of $sha (log: $LOG_DIR/build.log)"
    exit 1
  fi
  echo "BUILD wsl $sha $((SECONDS - started))s"
  [[ -z "$build" ]] || exit 0
  ip=$(MSYS_NO_PATHCONV=1 wsl.exe -e hostname -I | tr -d '\r')
  ip=${ip%% *}
  [[ "$ip" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "FAIL no WSL IP: '$ip'"; exit 1; }
  echo "SRS wsl $ip slot $SLOT"
  wsl_run --wsl-srs "$rel" "$ip" >"$LOG_DIR/srs.log" 2>&1 &
  SRS_JOB=$!
  trap host_stop EXIT
  started=$SECONDS
  # Start the cases 2 s apart, as obs-lane-test.sh does.
  for name in rtmp whip; do
    [[ -z "$pids" ]] || sleep 2
    bash "$here/obs-windows-test.sh" --host-case "$name" "$ip" >"$LOG_DIR/$name.log" 2>&1 &
    pids="$pids $!"
  done
  set -- $pids
  for name in rtmp whip; do
    status=0
    wait "$1" || status=$?
    shift
    tr -d '\r' < "$LOG_DIR/$name.log"
    case_known=$(sed -n 's/^RESULT .* known=\([0-9]*\) .*/\1/p' "$LOG_DIR/$name.log" | tail -n 1)
    known=$((known + ${case_known:-0}))
    if [[ $status -ne 0 ]]; then
      failed=$((failed + 1)); echo "LANE windows-$name failed exit=$status"
    elif grep -q '^RESULT ' "$LOG_DIR/$name.log"; then
      passed=$((passed + 1)); echo "LANE windows-$name passed"
    else
      echo "LANE windows-$name skipped"
    fi
  done
  elapsed=$((SECONDS - started))
  [[ $elapsed -le 60 ]] || { failed=$((failed + 1)); echo "FAIL budget ${elapsed}s > 60s"; }
  echo "RESULT windows passed=$passed known=$known failed=$failed cases=${elapsed}s"
  [[ $failed -eq 0 ]]
}

# In Git Bash on the host: one OBS case against SRS in WSL.
host_case() {
  local name=$1 ip=$2
  source "$SCRIPT_DIR/obs-test-lib.sh"
  export OBS_SRS_HOST=$ip
  obs_test_init "windows-$name" "$SLOT"
  obs_test_start
  if [[ $name == rtmp ]]; then
    obs_test_setup --rtmp "rtmp://$ip:$OBS_RTMP_PORT/live/$OBS_INSTANCE_ID"
    obs_test_assert_api H264 AAC
    obs_test_play http-flv aac
    obs_test_play hls aac
  else
    obs_test_setup --whip "http://$ip:$OBS_API_PORT/rtc/v1/whip/?app=live&stream=$OBS_INSTANCE_ID" --eip "$ip"
    # The RTC-to-RTMP bridge starts at the second keyframe, as in obs-whip-publish-test.sh.
    OBS_API_WAIT=15 obs_test_assert_api H264 AAC
    obs_test_assert_frames
    obs_test_play http-flv aac
  fi
  obs_test_finish
}

# In WSL: fetch the commit from the host's main repository into the WSL checkout and build SRS.
wsl_build() {
  local sha=$1 src main=$HOME/$3 dir=$HOME/$4 lib platform
  src=$(wslpath -a "$2")
  [[ -d "$main/.git" ]] || { echo "SETUP_FAIL: no repository at $main in WSL; clone it first"; exit 3; }
  git -C "$main" fetch -q "$src" "$sha"
  if [[ ! -e "$dir" ]]; then
    git -C "$main" worktree add -q --detach "$dir" "$sha"
    # Reuse the built third-party libraries, so the first configure does not rebuild them.
    for lib in "$main"/trunk/objs/Platform-*/3rdparty; do
      [[ -d "$lib" ]] || continue
      platform=$(basename "$(dirname "$lib")")
      mkdir -p "$dir/trunk/objs/$platform"
      cp -R "$lib" "$dir/trunk/objs/$platform/"
    done
  fi
  git -C "$dir" checkout -q --detach "$sha"
  cd "$dir/trunk"
  # Configure again when configure changed, since it lists the source files, or the flags differ.
  if ! grep -q "\"$WSL_FLAGS\"" objs/srs_auto_headers.hpp 2>/dev/null || [[ configure -nt objs/Makefile ]]; then
    ./configure $WSL_FLAGS
  fi
  make -j"$(nproc)"
}

# In WSL: run SRS env-only on the slot in the foreground, so the caller's wsl.exe keeps WSL running.
wsl_srs() {
  local status=0
  OBS_WORKSPACE=$HOME/$1
  OBS_TEST_DIR=$LOG_DIR
  source "$OBS_WORKSPACE/skills/srs-develop/scripts/obs-test-lib.sh"
  obs_test_ports "$SLOT"
  mkdir -p "$OBS_TEST_DIR/html"
  if [[ -f "$OBS_TEST_DIR/srs.pid" ]] && kill -0 "$(cat "$OBS_TEST_DIR/srs.pid")" 2>/dev/null; then
    echo "FAIL SRS already running in WSL on slot $SLOT"; exit 1
  fi
  obs_test_start_srs SRS_RTC_SERVER_CANDIDATE="$2" SRS_SRT_SERVER_ENABLED=off
  wait "$OBS_SRS_PID" || status=$?
  rm -f "$OBS_TEST_DIR/srs.pid"
  tail -n 20 "$OBS_TEST_DIR/srs.log"
  exit "$status"
}

# In WSL: stop the SRS that wsl_srs started, only when its pid file still names an srs process.
wsl_stop() {
  local pid attempt
  [[ -f "$LOG_DIR/srs.pid" ]] || return 0
  pid=$(cat "$LOG_DIR/srs.pid")
  [[ "$(cat "/proc/$pid/comm" 2>/dev/null)" == srs ]] || return 0
  kill "$pid" 2>/dev/null || true
  for attempt in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    kill -0 "$pid" 2>/dev/null || return 0
    sleep 0.1
  done
  kill -KILL "$pid" 2>/dev/null || true
}

case "${1:-}" in
  ''|--wip|--build)
    wip= build=
    for arg in "$@"; do
      case "$arg" in
        --wip) [[ -z "$wip" ]] || usage; wip=--wip;;
        --build) [[ -z "$build" ]] || usage; build=--build;;
        *) usage;;
      esac
    done
    mac_main "$wip" "$build";;
  --host) [[ $# -eq 3 || ( $# -eq 4 && $4 == --build ) ]] || usage; host_main "$2" "$3" "${4:-}";;
  --host-case)
    [[ $# -eq 3 && ( $2 == rtmp || $2 == whip ) ]] || usage
    host_case "$2" "$3";;
  --wsl-build) [[ $# -eq 5 ]] || usage; wsl_build "$2" "$3" "$4" "$5";;
  --wsl-srs) [[ $# -eq 3 ]] || usage; wsl_srs "$2" "$3";;
  --wsl-stop) wsl_stop;;
  *) usage;;
esac
