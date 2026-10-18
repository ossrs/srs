#!/bin/bash
# Sourced OBS test harness, compatible with Bash 3.2. Call init, start, setup,
# assertions and finish. init installs owned-resource EXIT/signal cleanup.
# Slot offsets: RTMP +0, HTTP +1, API +2, RTC UDP +3, SRT UDP +4.
# OBS_PYTHON names the Python command (default python3). OBS_SRS_HOST means
# the caller already runs SRS on the slot at that address, so start never
# spawns or kills SRS.

obs_test_ports() {
  OBS_RTMP_PORT=$1
  OBS_HTTP_PORT=$(($1 + 1))
  OBS_API_PORT=$(($1 + 2))
  OBS_RTC_PORT=$(($1 + 3))
  OBS_SRT_PORT=$(($1 + 4))
  OBS_API_URL="http://${OBS_SRS_HOST:-127.0.0.1}:$OBS_API_PORT"
  OBS_HTTP_URL="http://${OBS_SRS_HOST:-127.0.0.1}:$OBS_HTTP_PORT"
}

obs_test_fail() { echo "FAIL $*" >&2; return 1; }
obs_test_pass() { OBS_PASS_COUNT=$((OBS_PASS_COUNT + 1)); echo "PASS $*"; }
obs_test_known() { OBS_KNOWN_COUNT=$((OBS_KNOWN_COUNT + 1)); echo "KNOWN $*"; }
obs_test_skip() { echo "SKIP $*"; exit 0; }
obs_test_probe_available() {
  echo "$1" | jq -e '.obs_binary != null and .websocket_plugin != null' >/dev/null
}

obs_test_budget() {
  local elapsed=$((SECONDS - OBS_STARTED))
  [[ $elapsed -le 60 ]] || obs_test_fail "budget ${elapsed}s > 60s"
}

# Bound each external command by both its own timeout and the remaining budget.
obs_test_command() {
  local limit=$1 deadline status=0
  shift
  deadline=$((SECONDS + limit))
  OBS_COMMAND_TIMED_OUT=
  "$@" &
  OBS_COMMAND_PID=$!
  while kill -0 "$OBS_COMMAND_PID" 2>/dev/null; do
    if [[ $SECONDS -ge $deadline || $((SECONDS - OBS_STARTED)) -ge 60 ]]; then
      kill "$OBS_COMMAND_PID" 2>/dev/null || true
      sleep 0.1
      kill -KILL "$OBS_COMMAND_PID" 2>/dev/null || true
      wait "$OBS_COMMAND_PID" 2>/dev/null || true
      OBS_COMMAND_PID=
      OBS_COMMAND_TIMED_OUT=1
      obs_test_fail 'command timed out'
      return 1
    fi
    sleep 0.1
  done
  wait "$OBS_COMMAND_PID" || status=$?
  OBS_COMMAND_PID=
  return "$status"
}

obs_test_cleanup() {
  local pid attempt status=0
  if [[ -n "${OBS_COMMAND_PID:-}" ]]; then
    kill "$OBS_COMMAND_PID" 2>/dev/null || true
    wait "$OBS_COMMAND_PID" 2>/dev/null || true
  fi
  if [[ -n "${OBS_LAUNCH_PID:-}" ]]; then
    kill "$OBS_LAUNCH_PID" 2>/dev/null || true
    wait "$OBS_LAUNCH_PID" 2>/dev/null || true
    OBS_LAUNCH_PID=
  fi
  if [[ -n "${OBS_INSTANCE_ID:-}" ]]; then
    "${OBS_PYTHON:-python3}" "$OBS_WS" instance remove --instance "$OBS_INSTANCE_ID" --force --timeout 10 \
      >"$OBS_TEST_DIR/cleanup.log" 2>&1 && OBS_INSTANCE_ID= || { obs_test_fail 'OBS cleanup'; status=1; }
  fi
  if [[ -n "${OBS_SRS_PID:-}" && -f "$OBS_TEST_DIR/srs.pid" ]]; then
    pid=$(cat "$OBS_TEST_DIR/srs.pid")
    # Only the process spawned by this shell, confirmed by its pid file.
    if [[ "$pid" == "$OBS_SRS_PID" ]]; then
      kill "$pid" 2>/dev/null || true
      for attempt in 1 2 3 4 5 6 7 8 9 10; do
        kill -0 "$pid" 2>/dev/null || break
        sleep 0.1
      done
      kill -KILL "$pid" 2>/dev/null || true
      wait "$pid" 2>/dev/null || true
      rm -f "$OBS_TEST_DIR/srs.pid"
    else
      obs_test_fail 'SRS pid file no longer matches owned process'
      return 1
    fi
  fi
  return "$status"
}

obs_test_exit() {
  local status=$?
  trap - EXIT
  obs_test_cleanup || status=1
  obs_test_budget || status=1
  [[ $status -eq 0 ]] || echo "FAIL $OBS_CASE (logs: $OBS_TEST_DIR)" >&2
  exit "$status"
}

obs_test_init() {
  [[ "${SRS_TEST_OBS:-1}" != 0 ]] || obs_test_skip SRS_TEST_OBS=0
  OBS_STARTED=$SECONDS
  OBS_CASE=$1
  local base=${SRS_TEST_PORT_BASE:-$2} dep probe script_dir
  case "$OBS_CASE" in ''|*[!a-zA-Z0-9_-]*) obs_test_fail 'invalid case name'; return 1;; esac
  case "$base" in ''|*[!0-9]*) obs_test_fail 'invalid port base'; return 1;; esac
  [[ $base -ge 11000 && $((base + 99)) -lt 32768 ]] || { obs_test_fail 'port slot outside test range'; return 1; }
  script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
  OBS_WORKSPACE=$(cd "$script_dir/../../.." && pwd)
  OBS_WS="$OBS_WORKSPACE/skills/internal-obs-control/scripts/obsws.py"
  OBS_RECIPE="$OBS_WORKSPACE/skills/internal-obs-control/scripts/obs_srs_test.py"
  OBS_TEST_DIR="/tmp/srs-obs-$OBS_CASE"
  obs_test_ports "$base"
  OBS_PASS_COUNT=0 OBS_KNOWN_COUNT=0
  OBS_INSTANCE_ID= OBS_SRS_PID= OBS_LAUNCH_PID= OBS_COMMAND_PID= OBS_COMMAND_TIMED_OUT=
  for dep in "${OBS_PYTHON:-python3}" ffmpeg ffprobe jq curl; do
    command -v "$dep" >/dev/null || { obs_test_fail "missing dependency $dep"; return 1; }
  done
  # Probe is read-only; never enable/restart the user's main OBS.
  probe=$("${OBS_PYTHON:-python3}" "$OBS_WS" probe --json) || { obs_test_skip 'OBS not available (probe failed)'; }
  obs_test_probe_available "$probe" || obs_test_skip 'OBS not available'
  [[ -n "${OBS_SRS_HOST:-}" || -x "$OBS_WORKSPACE/trunk/objs/srs" ]] || { obs_test_fail 'build trunk/objs/srs first'; return 1; }
  mkdir -p "$OBS_TEST_DIR/html"
  # Reject another live owner; never terminate a listener to acquire a slot.
  if [[ -z "${OBS_SRS_HOST:-}" && -f "$OBS_TEST_DIR/srs.pid" ]] && kill -0 "$(cat "$OBS_TEST_DIR/srs.pid")" 2>/dev/null; then
    obs_test_fail 'case already running'; return 1
  fi
  trap obs_test_exit EXIT
  trap 'exit 130' INT
  trap 'exit 143' TERM
  export OBS_CONTROL_WS_PORTS=15000-15099 PYTHONDONTWRITEBYTECODE=1
  # Shared clip: atomic rename prevents a parallel setup reading half a file.
  OBS_CLIP=/tmp/srs-obs-test-640x360-30.mp4
  if [[ ! -s "$OBS_CLIP" ]]; then
    obs_test_command 15 ffmpeg -hide_banner -loglevel error -y \
      -f lavfi -i testsrc2=size=640x360:rate=30 -f lavfi -i sine=frequency=440:sample_rate=48000 \
      -t 4 -c:v libx264 -preset ultrafast -pix_fmt yuv420p -c:a aac \
      "$OBS_TEST_DIR/clip.mp4" || return 1
    mv "$OBS_TEST_DIR/clip.mp4" "$OBS_CLIP"
  fi
}

obs_test_start_srs() {
  (cd "$OBS_WORKSPACE/trunk" && exec env \
    SRS_RTMP_LISTEN="$OBS_RTMP_PORT" SRS_HTTP_SERVER_ENABLED=on SRS_HTTP_SERVER_LISTEN="$OBS_HTTP_PORT" \
    SRS_HTTP_SERVER_DIR="$OBS_TEST_DIR/html" SRS_HTTP_API_ENABLED=on SRS_HTTP_API_LISTEN="$OBS_API_PORT" \
    SRS_RTC_SERVER_ENABLED=on SRS_RTC_SERVER_LISTEN="$OBS_RTC_PORT" SRS_RTC_SERVER_CANDIDATE=127.0.0.1 \
    SRS_SRT_SERVER_ENABLED=on SRS_SRT_SERVER_LISTEN="$OBS_SRT_PORT" \
    SRS_VHOST_HTTP_REMUX_ENABLED=on SRS_VHOST_HLS_ENABLED=on SRS_VHOST_HLS_HLS_FRAGMENT=1 \
    SRS_VHOST_HLS_HLS_WINDOW=4 SRS_VHOST_HLS_HLS_PATH="$OBS_TEST_DIR/html" \
    SRS_VHOST_RTC_ENABLED=on SRS_VHOST_RTC_RTMP_TO_RTC=on SRS_VHOST_RTC_RTC_TO_RTMP=on \
    SRS_VHOST_SRT_ENABLED=on SRS_VHOST_SRT_SRT_TO_RTMP=on SRS_LOG_TANK=console \
    "$@" ./objs/srs -e) >"$OBS_TEST_DIR/srs.log" 2>&1 &
  OBS_SRS_PID=$!
  printf '%s\n' "$OBS_SRS_PID" > "$OBS_TEST_DIR/srs.pid"
}

obs_test_start() {
  OBS_INSTANCE_ID=$("${OBS_PYTHON:-python3}" "$OBS_WS" instance new) || return 1
  # Python on Windows ends its lines with CRLF.
  OBS_INSTANCE_ID=${OBS_INSTANCE_ID%$'\r'}
  [[ "$OBS_INSTANCE_ID" == t* ]] || { obs_test_fail 'instance claim failed'; return 1; }
  [[ -n "${OBS_SRS_HOST:-}" ]] || obs_test_start_srs "$@"
  "${OBS_PYTHON:-python3}" "$OBS_WS" launch --instance "$OBS_INSTANCE_ID" --wait 15 --minimize-to-tray \
    >"$OBS_TEST_DIR/launch.log" 2>&1 &
  OBS_LAUNCH_PID=$!
  local deadline=$((SECONDS + 10))
  until curl -fsS --max-time 1 "$OBS_API_URL/api/v1/versions" >"$OBS_TEST_DIR/version.json" 2>/dev/null; do
    [[ -n "${OBS_SRS_HOST:-}" ]] || kill -0 "$OBS_SRS_PID" 2>/dev/null || { obs_test_fail 'SRS exited during startup'; return 1; }
    [[ $SECONDS -lt $deadline ]] || { obs_test_fail 'SRS API startup timeout'; return 1; }
    sleep 0.1
  done
  if ! wait "$OBS_LAUNCH_PID"; then
    OBS_LAUNCH_PID=
    obs_test_skip 'OBS not available (isolated launch failed; see launch.log)'
  fi
  OBS_LAUNCH_PID=
}

obs_test_setup() {
  local status=0 probe relaunch=
  obs_test_setup_once "$@" || status=$?
  if [[ -n "${OBS_COMMAND_TIMED_OUT:-}" ]]; then
    # When several instances reset video at once, OBS can hang in
    # obs_reset_video, joining a graphics thread that keeps rendering; the
    # websocket stops answering while the process lives. Replace it once.
    echo 'OBS stopped answering during setup; relaunching the isolated instance once'
    obs_test_command 10 "${OBS_PYTHON:-python3}" "$OBS_WS" quit --instance "$OBS_INSTANCE_ID" --force --timeout 3 \
      >>"$OBS_TEST_DIR/launch.log" 2>&1 || true
    relaunch=1
  elif [[ $status -eq 2 ]]; then
    probe=$("${OBS_PYTHON:-python3}" "$OBS_WS" probe --instance "$OBS_INSTANCE_ID" --json) || probe='{}'
    # Some macOS OBS versions exit on their first CreateProfile. Recover once,
    # only when the owned process exited; never retry a running/modal OBS.
    if echo "$probe" | jq -e '.obs_running == false and .live.connected == false' >/dev/null; then
      echo 'OBS exited during setup; relaunching the isolated instance once'
      relaunch=1
    fi
  fi
  if [[ -n "$relaunch" ]]; then
    obs_test_command 15 "${OBS_PYTHON:-python3}" "$OBS_WS" launch --instance "$OBS_INSTANCE_ID" --wait 10 --minimize-to-tray \
      >>"$OBS_TEST_DIR/launch.log" 2>&1 || return 1
    status=0
    obs_test_setup_once "$@" || status=$?
  fi
  [[ $status -eq 0 ]] || { obs_test_fail "OBS setup/publish (see $OBS_TEST_DIR/setup.log)"; return 1; }
}

# 20 s bounds a hung OBS early enough for one relaunch inside the 60 s budget.
obs_test_setup_once() {
  obs_test_command 20 "${OBS_PYTHON:-python3}" "$OBS_RECIPE" setup --instance "$OBS_INSTANCE_ID" \
    --width 640 --height 360 --fps 30 --video-bitrate 800 --preset ultrafast \
    --media-file "$OBS_CLIP" --srs-api "$OBS_API_URL" --http-url "$OBS_HTTP_URL" \
    "$@" --start --verify >>"$OBS_TEST_DIR/setup.log" 2>&1
}

obs_test_read_stream() { curl -fsS --max-time 1 "$OBS_API_URL/api/v1/streams/"; }

obs_test_assert_api() {
  local video=$1 audio=$2 data deadline=$((SECONDS + ${OBS_API_WAIT:-8}))
  while :; do
    data=$(obs_test_read_stream 2>/dev/null) || data='{}'
    if echo "$data" | jq -e --arg name "$OBS_INSTANCE_ID" --arg video "$video" --arg audio "$audio" \
      '.code == 0 and any(.streams[]?; .app == "live" and .name == $name and .publish.active == true
      and .video.codec == $video and .audio.codec == $audio and .video.width == 640 and .video.height == 360)' >/dev/null; then
      echo "$data" > "$OBS_TEST_DIR/streams.json"
      obs_test_pass "API $video/$audio 640x360"; return
    fi
    [[ $SECONDS -lt $deadline ]] || break
    obs_test_budget || return 1
    sleep 0.1
  done
  obs_test_fail "API expected active live/$OBS_INSTANCE_ID $video/$audio 640x360"
}

# SRS flushes an RTC publisher's frame count to the statistic only every 288
# frames, and the RTC-to-RTMP bridge counts none, so a fresh WHIP publisher
# reads frames=0: report that as the known finding, never as a pass.
obs_test_assert_frames() {
  local frames
  frames=$(obs_test_read_stream 2>/dev/null | jq -r --arg name "$OBS_INSTANCE_ID" \
    '[.streams[]? | select(.app == "live" and .name == $name) | .frames // 0] | first // 0') || frames=0
  case "$frames" in ''|*[!0-9]*) frames=0;; esac
  if [[ $frames -gt 0 ]]; then obs_test_pass "API frames $frames"; else obs_test_known 'F2 API frames 0'; fi
}

obs_test_media_matches() {
  echo "$1" | jq -e --arg audio "$2" 'any(.streams[]?; .codec_type == "video" and .codec_name == "h264"
    and .width == 640 and .height == 360) and any(.streams[]?; .codec_type == "audio" and .codec_name == $audio)' >/dev/null
}

obs_test_play() {
  local kind=$1 audio=${2:-aac} suffix data deadline=$((SECONDS + 10))
  case "$kind" in http-flv) suffix=flv;; hls) suffix=m3u8;; *) obs_test_fail 'unknown player'; return 1;; esac
  while :; do
    if obs_test_command 5 ffprobe -v error -rw_timeout 3000000 -analyzeduration 1000000 \
      -probesize 1000000 -show_streams -of json "$OBS_HTTP_URL/live/$OBS_INSTANCE_ID.$suffix" \
      >"$OBS_TEST_DIR/$kind.json" 2>"$OBS_TEST_DIR/$kind.log"; then
      data=$(cat "$OBS_TEST_DIR/$kind.json")
      if obs_test_media_matches "$data" "$audio"; then obs_test_pass "$kind H264/$audio"; return; fi
    fi
    [[ $SECONDS -lt $deadline ]] || break
    obs_test_budget || return 1
    sleep 0.1
  done
  obs_test_fail "$kind missing H264/$audio playback"
}

obs_test_whep() {
  local tool="$OBS_WORKSPACE/tools/pion-whep/objs/pion-whep" summary
  [[ -x "$tool" ]] || { obs_test_fail 'build tools/pion-whep before WHEP checks'; return 1; }
  obs_test_command 10 "$tool" -hide_banner -f whep \
    -i "$OBS_API_URL/rtc/v1/whep/?app=live&stream=$OBS_INSTANCE_ID&eip=127.0.0.1" \
    -t 3 -f null - >"$OBS_TEST_DIR/whep.log" 2>&1 || return 1
  summary=$(sed -n 's/^Received video=\([0-9]*\), audio=\([0-9]*\) .*/\1 \2/p' "$OBS_TEST_DIR/whep.log" | tail -1)
  [[ "$summary" =~ ^([0-9]+)\ ([0-9]+)$ ]] && [[ ${BASH_REMATCH[1]} -gt 0 && ${BASH_REMATCH[2]} -gt 0 ]] \
    || { obs_test_fail 'WHEP missing video/audio packets'; return 1; }
  obs_test_pass 'WHEP video/audio packets'
}

obs_test_finish() {
  obs_test_cleanup || return 1
  obs_test_budget || return 1
  echo "RESULT $OBS_CASE passed=$OBS_PASS_COUNT known=$OBS_KNOWN_COUNT elapsed=$((SECONDS - OBS_STARTED))s"
}
