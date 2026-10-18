#!/bin/bash
# Regression checks for the sourced harness; --live also exercises real RTMP.
set -eu
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
LIB="$SCRIPT_DIR/obs-test-lib.sh"
if [[ ! -f "$LIB" ]]; then
  echo 'FAIL shared OBS harness is missing'
  exit 1
fi
source "$LIB"
if [[ "${1:-}" == --live ]]; then
  obs_test_init helper 15100
  obs_test_start
  obs_test_setup --rtmp "rtmp://127.0.0.1:$OBS_RTMP_PORT/live/$OBS_INSTANCE_ID"
  obs_test_assert_api H264 AAC
  obs_test_play http-flv aac
  obs_test_play hls aac
  obs_test_whep
  obs_test_finish
  exit
fi
checks=0
check() {
  checks=$((checks + 1))
  if "$@"; then echo "PASS regression $checks"; else echo "FAIL regression $checks"; exit 1; fi
}
disabled() {
  local output
  output=$(SRS_TEST_OBS=0 obs_test_init disabled 15100)
  [[ "$output" == 'SKIP SRS_TEST_OBS=0' ]]
}
check disabled
check obs_test_probe_available '{"obs_binary":"OBS","websocket_plugin":"plugin"}'
check test "$(obs_test_probe_available '{"obs_binary":null,"websocket_plugin":null}'; echo $?)" = 1
obs_test_read_stream() { printf '%s\n' "$fixture"; }
OBS_INSTANCE_ID=test
OBS_API_URL=http://127.0.0.1:15102
OBS_CASE=regression
OBS_PASS_COUNT=0 OBS_KNOWN_COUNT=0
mkdir -p /tmp/srs-obs-lib-regression
OBS_TEST_DIR=/tmp/srs-obs-lib-regression
OBS_STARTED=$SECONDS
OBS_API_WAIT=0
fixture='{"code":0,"streams":[{"app":"live","name":"test","publish":{"active":true},"video":{"codec":"H264","width":640,"height":360},"audio":{"codec":"AAC"}}]}'
check obs_test_assert_api H264 AAC
fixture='{"code":0,"streams":[{"app":"live","name":"test","publish":{"active":true},"video":{"codec":"H264","width":640,"height":360}}]}'
check test "$(obs_test_assert_api H264 AAC >/dev/null 2>&1; echo $?)" = 1
fixture='{"code":0,"streams":[{"app":"live","name":"other","publish":{"active":true},"video":{"codec":"H264","width":640,"height":360},"audio":{"codec":"AAC"}}]}'
check test "$(obs_test_assert_api H264 AAC >/dev/null 2>&1; echo $?)" = 1
fixture='{"code":0,"streams":[{"app":"live","name":"test","publish":{"active":false},"video":{"codec":"H264","width":640,"height":360},"audio":{"codec":"AAC"}}]}'
check test "$(obs_test_assert_api H264 AAC >/dev/null 2>&1; echo $?)" = 1
fixture='{"code":0,"streams":[{"app":"live","name":"test","publish":{"active":true},"video":{"codec":"H264","width":1280,"height":720},"audio":{"codec":"AAC"}}]}'
check test "$(obs_test_assert_api H264 AAC >/dev/null 2>&1; echo $?)" = 1
fixture='{"code":0,"streams":[{"app":"live","name":"test","publish":{"active":true},"video":{"codec":"H264","width":640,"height":360},"audio":{"codec":"Opus"}}]}'
check test "$(obs_test_assert_api H264 AAC >/dev/null 2>&1; echo $?)" = 1
fixture='{"streams":[{"codec_type":"video","codec_name":"h264","width":640,"height":360},{"codec_type":"audio","codec_name":"aac"}]}'
check obs_test_media_matches "$fixture" aac
fixture='{"streams":[{"codec_type":"video","codec_name":"h264","width":640,"height":360}]}'
check test "$(obs_test_media_matches "$fixture" aac; echo $?)" = 1
OBS_STARTED=$((SECONDS - 61))
check test "$(obs_test_budget >/dev/null 2>&1; echo $?)" = 1
OBS_STARTED=$SECONDS
check obs_test_budget
OBS_PASS_COUNT=0
OBS_KNOWN_COUNT=0
obs_test_known F2 'API frame count'
check test "$OBS_PASS_COUNT" = 0
check test "$OBS_KNOWN_COUNT" = 1
# A zero frame count is the known SRS finding, excluded from the pass count.
OBS_PASS_COUNT=0
OBS_KNOWN_COUNT=0
fixture='{"code":0,"streams":[{"app":"live","name":"test","publish":{"active":true},"frames":0}]}'
check test "$(obs_test_assert_frames)" = 'KNOWN F2 API frames 0'
fixture='{"code":0,"streams":[{"app":"live","name":"other","publish":{"active":true},"frames":90}]}'
check test "$(obs_test_assert_frames)" = 'KNOWN F2 API frames 0'
fixture='{"code":0,"streams":[{"app":"live","name":"test","publish":{"active":true},"frames":57}]}'
check test "$(obs_test_assert_frames)" = 'PASS API frames 57'
obs_test_assert_frames >/dev/null
check test "$OBS_PASS_COUNT" = 1
check test "$OBS_KNOWN_COUNT" = 0
crashed_profile_retry() (
  OBS_WS=unused OBS_RECIPE=recipe OBS_CLIP=clip OBS_HTTP_URL=http://127.0.0.1:15101
  local calls=0
  obs_test_command() { calls=$((calls + 1)); [[ $calls -ne 1 ]] || return 2; }
  python3() { echo '{"obs_running":false,"live":{"connected":false}}'; }
  obs_test_setup --rtmp rtmp://127.0.0.1:15100/live/test || return 1
  [[ $calls -eq 3 ]]
)
check crashed_profile_retry
# A setup whose OBS stops answering is force-quit, relaunched and retried once.
hung_setup_retry() (
  OBS_WS=unused OBS_RECIPE=recipe OBS_CLIP=clip OBS_HTTP_URL=http://127.0.0.1:15101 OBS_INSTANCE_ID=t12345
  local calls=0 log=/tmp/srs-obs-lib-regression/hung.args
  : > "$log"
  obs_test_command() {
    calls=$((calls + 1)); shift
    printf '%s\n' "$*" >> "$log"
    OBS_COMMAND_TIMED_OUT=
    [[ $calls -ne 1 ]] || { OBS_COMMAND_TIMED_OUT=1; return 1; }
  }
  python3() { return 1; }
  obs_test_setup --rtmp rtmp://127.0.0.1:15100/live/test || return 1
  [[ $calls -eq 4 ]] && grep -q '^python3 unused quit --instance t12345 --force' "$log" \
    && grep -q '^python3 unused launch --instance t12345 ' "$log"
)
check hung_setup_retry
cleanup_failure() (
  OBS_WS=unused OBS_INSTANCE_ID=t12345 OBS_SRS_PID=
  python3() { return 1; }
  if obs_test_cleanup >/dev/null 2>&1; then return 1; fi
)
check cleanup_failure
# Cleanup must remove exactly the claimed instance and reap its owned SRS.
OBS_TEST_DIR=/tmp/srs-obs-lib-regression
OBS_WS=unused
OBS_INSTANCE_ID=t12345
python3() { printf '%s\n' "$*" > /tmp/srs-obs-lib-regression/remove.args; }
sleep 30 &
OBS_SRS_PID=$!
printf '%s\n' "$OBS_SRS_PID" > "$OBS_TEST_DIR/srs.pid"
obs_test_cleanup
check test "$(cat /tmp/srs-obs-lib-regression/remove.args)" = 'unused instance remove --instance t12345 --force --timeout 10'
check test "$(kill -0 "$OBS_SRS_PID" 2>/dev/null; echo $?)" != 0
check test ! -f /tmp/srs-obs-lib-regression/srs.pid
# The slot's URLs point at 127.0.0.1, or at OBS_SRS_HOST for an SRS started elsewhere.
local_ports() (
  unset OBS_SRS_HOST
  obs_test_ports 15100
  [[ $OBS_RTMP_PORT == 15100 && $OBS_API_URL == http://127.0.0.1:15102 && $OBS_HTTP_URL == http://127.0.0.1:15101 ]]
)
check local_ports
# With OBS_SRS_HOST, start claims and launches OBS but never spawns SRS, and a
# Windows Python's CRLF does not stay in the instance id.
remote_srs_start() (
  OBS_WS=unused OBS_PYTHON=python OBS_SRS_HOST=172.24.0.5 OBS_SRS_PID=
  obs_test_ports 15900
  [[ $OBS_RTMP_PORT == 15900 && $OBS_API_URL == http://172.24.0.5:15902 && $OBS_HTTP_URL == http://172.24.0.5:15901 ]] || return 1
  rm -f /tmp/srs-obs-lib-regression/spawned
  python() { [[ $3 != new ]] || printf 't12345\r\n'; }
  obs_test_start_srs() { echo spawned > /tmp/srs-obs-lib-regression/spawned; }
  curl() { [[ $* == *http://172.24.0.5:15902/api/v1/versions ]]; }
  obs_test_start || return 1
  [[ $OBS_INSTANCE_ID == t12345 && -z $OBS_SRS_PID && ! -f /tmp/srs-obs-lib-regression/spawned ]]
)
check remote_srs_start
# The Windows run: gates and arguments, checked without a host.
WIN="$SCRIPT_DIR/obs-windows-test.sh"
check test "$(SRS_TEST_OBS=0 bash "$WIN")" = 'SKIP SRS_TEST_OBS=0'
check test "$(SRS_TEST_WINDOWS_HOST= bash "$WIN")" = 'SKIP windows: SRS_TEST_WINDOWS_HOST is not set'
check test "$(bash "$WIN" --bogus >/dev/null 2>&1; echo $?)" = 3
check test "$(bash "$WIN" --host-case srt 127.0.0.1 >/dev/null 2>&1; echo $?)" = 3
check test "$(SRS_TEST_WINDOWS_HOST= bash "$WIN" --wip --build)" = 'SKIP windows: SRS_TEST_WINDOWS_HOST is not set'
check test "$(bash "$WIN" --build --bogus >/dev/null 2>&1; echo $?)" = 3
# The host step with --build builds SRS in WSL and stops before SRS and the cases, checked with a fake
# wsl.exe and cygpath on PATH.
WIN_FAKE=/tmp/srs-obs-lib-regression/win-bin
mkdir -p "$WIN_FAKE"
printf '%s\n' '#!/bin/bash' 'echo "$*" >> /tmp/srs-obs-lib-regression/wsl.args' > "$WIN_FAKE/wsl.exe"
printf '%s\n' '#!/bin/bash' 'echo "$2"' > "$WIN_FAKE/cygpath"
chmod +x "$WIN_FAKE/wsl.exe" "$WIN_FAKE/cygpath"
windows_host_build() {
  local output sha
  : > /tmp/srs-obs-lib-regression/wsl.args
  sha=$(git -C "$SCRIPT_DIR" rev-parse HEAD)
  output=$(cd "$SCRIPT_DIR/../../.." && PATH="$WIN_FAKE:$PATH" bash "$WIN" --host rel main --build) || return 1
  [[ "$output" == "BUILD wsl $sha "* ]] || return 1
  [[ $(wc -l < /tmp/srs-obs-lib-regression/wsl.args) -eq 1 ]] \
    && grep -q -- " --wsl-build $sha .* main rel$" /tmp/srs-obs-lib-regression/wsl.args
}
check windows_host_build
# The Linux run: gates, the Docker build and the SRS container, checked with a
# fake docker and a fake OBS probe on PATH.
LINUX="$SCRIPT_DIR/obs-linux-test.sh"
FAKE=/tmp/srs-obs-lib-regression/bin
mkdir -p "$FAKE"
printf '%s\n' '#!/bin/bash' 'echo "$*" >> /tmp/srs-obs-lib-regression/docker.args' \
  'case "$1" in' \
  '  info) [[ -z "${FAKE_DOCKER_DOWN:-}" ]] || exit 1; echo arm64;;' \
  '  run) if [[ " $* " == *" -d "* ]]; then echo cid123; else cat >/dev/null; fi;;' \
  '  inspect) echo false;;' \
  'esac' > "$FAKE/docker"
printf '%s\n' '#!/bin/bash' 'echo "{\"obs_binary\":\"OBS\",\"websocket_plugin\":\"plugin\"}"' > "$FAKE/fake-python"
chmod +x "$FAKE/docker" "$FAKE/fake-python"
linux_run() { PATH="$FAKE:$PATH" OBS_PYTHON=fake-python bash "$LINUX" "$@"; }
check test "$(SRS_TEST_OBS=0 bash "$LINUX")" = 'SKIP SRS_TEST_OBS=0'
check test "$(FAKE_DOCKER_DOWN=1 linux_run)" = 'SKIP linux: unverified: Docker not available'
check test "$(bash "$LINUX" --bogus >/dev/null 2>&1; echo $?)" = 3
check test "$(SRS_TEST_PORT_BASE=1000 linux_run >/dev/null 2>&1; echo $?)" = 3
# --build copies the tracked trunk files into the named volume and builds there, without OBS.
linux_build() {
  local output
  : > /tmp/srs-obs-lib-regression/docker.args
  output=$(OBS_PYTHON=false PATH="$FAKE:$PATH" bash "$LINUX" --build) || return 1
  [[ "$output" == "BUILD linux $(git -C "$SCRIPT_DIR" rev-parse --short HEAD)"* ]] || return 1
  grep -q -- '-v srs-obs-linux-build:/build ' /tmp/srs-obs-lib-regression/docker.args \
    && grep -q -- ':/src:ro ' /tmp/srs-obs-lib-regression/docker.args \
    && grep -q -- ' --container-build$' /tmp/srs-obs-lib-regression/docker.args \
    && ! grep -q -- 'run -d' /tmp/srs-obs-lib-regression/docker.args
}
check linux_build
# SRS exiting in its container fails the run, and the EXIT trap removes that container.
linux_srs_exits() {
  local output status=0
  : > /tmp/srs-obs-lib-regression/docker.args
  output=$(SRS_TEST_PORT_BASE=15400 linux_run 2>&1) || status=$?
  [[ $status -eq 1 && "$output" == *'FAIL SRS exited in the Linux container'* ]] || return 1
  grep -q -- 'run -d .*-p 127.0.0.1:15400:15400 -p 127.0.0.1:15401:15401 -p 127.0.0.1:15402:15402 -p 127.0.0.1:15403:15403/udp -p 127.0.0.1:15404:15404/udp ' \
    /tmp/srs-obs-lib-regression/docker.args && tail -n 1 /tmp/srs-obs-lib-regression/docker.args | grep -q '^rm -f cid123$'
}
check linux_srs_exits
# The WSL build configures on the first run, again when configure changed or the flags differ, and not
# otherwise; checked with local repositories and a fake configure, make, nproc and wslpath on PATH.
WSL_DIR=/tmp/srs-obs-lib-regression/wsl
mkdir -p "$WSL_DIR/bin" "$WSL_DIR/src/trunk" "$WSL_DIR/home"
printf '%s\n' '#!/bin/bash' 'echo "${@: -1}"' > "$WSL_DIR/bin/wslpath"
printf '%s\n' '#!/bin/bash' 'echo 2' > "$WSL_DIR/bin/nproc"
printf '%s\n' '#!/bin/bash' 'exit 0' > "$WSL_DIR/bin/make"
chmod +x "$WSL_DIR/bin/wslpath" "$WSL_DIR/bin/nproc" "$WSL_DIR/bin/make"
printf '%s\n' '#!/bin/bash' 'echo "$*" >> ../configure.args' 'mkdir -p objs' 'touch objs/Makefile' \
  'echo "#define SRS_USER_CONFIGURE \"$*\"" > objs/srs_auto_headers.hpp' > "$WSL_DIR/src/trunk/configure"
chmod +x "$WSL_DIR/src/trunk/configure"
git -C "$WSL_DIR/src" init -q
git -C "$WSL_DIR/src" add trunk/configure
git -C "$WSL_DIR/src" -c user.name=t -c user.email=t@t commit -q -m one
git clone -q "$WSL_DIR/src" "$WSL_DIR/home/main"
wsl_build_run() {
  (cd "$WSL_DIR" && HOME="$WSL_DIR/home" PATH="$WSL_DIR/bin:$PATH" \
    bash "$WIN" --wsl-build "$(git -C "$WSL_DIR/src" rev-parse HEAD)" "$WSL_DIR/src" main rel >/dev/null 2>&1)
}
wsl_configures() {
  local before after
  before=$(cat "$WSL_DIR/home/rel/configure.args" 2>/dev/null | wc -l)
  wsl_build_run || return 1
  after=$(wc -l < "$WSL_DIR/home/rel/configure.args")
  [[ $after -eq $((before + 1)) ]] && grep -q -- '"--srt=on --ffmpeg-opus=off"' "$WSL_DIR/home/rel/trunk/objs/srs_auto_headers.hpp"
}
wsl_reuses() {
  local before
  before=$(wc -l < "$WSL_DIR/home/rel/configure.args")
  wsl_build_run && [[ $(wc -l < "$WSL_DIR/home/rel/configure.args") -eq $before ]]
}
check wsl_configures
check wsl_reuses
touch -t 202001010000 "$WSL_DIR/home/rel/trunk/objs/Makefile"
echo '# changed' >> "$WSL_DIR/src/trunk/configure"
git -C "$WSL_DIR/src" -c user.name=t -c user.email=t@t commit -q -am two
check wsl_configures
echo '#define SRS_USER_CONFIGURE "--srt=on"' > "$WSL_DIR/home/rel/trunk/objs/srs_auto_headers.hpp"
check wsl_configures
check wsl_reuses
rm -rf /tmp/srs-obs-lib-regression
echo "PASS $checks harness regressions"
