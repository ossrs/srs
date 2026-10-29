#!/bin/bash
# Regression checks for the sourced Chrome harness, with a fake node, npm and
# SRS; --live also runs Chrome WHIP and WHEP against the built SRS.
set -eu
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
LIB="$SCRIPT_DIR/chrome-test-lib.sh"
if [[ ! -f "$LIB" ]]; then
  echo 'FAIL shared Chrome harness is missing'
  exit 1
fi
source "$LIB"
if [[ "${1:-}" == --live ]]; then
  chrome_test_init helper 15600
  chrome_test_start
  stream=$(chrome_test_stream)
  chrome_test_run live --seconds 3 \
    --mode whip --url "$(chrome_test_whip_url "$stream")" --mode whep --url "$(chrome_test_whep_url "$stream")"
  chrome_test_job_assert live whep 'rise(.inboundVideo.framesDecoded) >= 40' 'WHEP frames decoded +40'
  chrome_test_job_assert live whep 'latest(.inboundVideo.frameWidth) == 640 and latest(.inboundVideo.frameHeight) == 480' \
    'WHEP 640x480'
  chrome_test_job_assert live whep 'latest(.inboundAudio.totalAudioEnergy) > 0' 'WHEP audio energy'
  chrome_test_finish
  exit
fi

T=/tmp/srs-chrome-lib-regression
rm -rf /tmp/srs-chrome-lib-regression
mkdir -p "$T/bin"
checks=0
check() {
  checks=$((checks + 1))
  if "$@"; then echo "PASS regression $checks $1"; else echo "FAIL regression $checks $*"; exit 1; fi
}
# The output and the exit status of a command, on one line.
out_status() {
  local output status=0
  output=$("$@" 2>&1) || status=$?
  echo "$output $status"
}
export CHROME_TEST_ROOT=$T
CHROME_STARTED=$SECONDS

# A fake node: --version prints FAKE_NODE_VERSION, find-browser and run follow
# FAKE_BROWSER_EXIT and FAKE_RUN_EXIT, and each call is logged.
printf '%s\n' '#!/bin/bash' \
  'echo "$*" >> /tmp/srs-chrome-lib-regression/node.calls' \
  'if [[ "$1" == --version ]]; then echo "${FAKE_NODE_VERSION:-v22.23.2}"; exit 0; fi' \
  'if [[ "$1" == -e ]]; then echo 1700000000123; exit 0; fi' \
  'if [[ "$1" == --test ]]; then echo "unit driver $PWD" >> /tmp/srs-chrome-lib-regression/lane.calls' \
  '  printf "ok 1 - a\n# pass %s\n# fail %s\n" "${FAKE_TEST_PASS:-3}" "${FAKE_TEST_FAIL:-0}"; exit ${FAKE_TEST_EXIT:-0}; fi' \
  'case "$2" in' \
  '  find-browser) [[ ${FAKE_BROWSER_EXIT:-0} -ne 0 ]] || echo /fake/chrome; exit ${FAKE_BROWSER_EXIT:-0};;' \
  '  run) out=; while [[ $# -gt 0 ]]; do [[ "$1" != --out ]] || out=$2; shift; done' \
  '    [[ -z "$out" ]] || cp /tmp/srs-chrome-lib-regression/stats.json "$out"' \
  '    [[ -z "${FAKE_RUN_DELAY:-}" ]] || sleep "$FAKE_RUN_DELAY"' \
  '    echo "PASS whep ready playing in 120ms"' \
  '    echo "LOCATION whep http://127.0.0.1:15602/rtc/v1/whip/?action=delete&token=t1&app=live&stream=s&session=ab12:cd34"' \
  '    [[ -z "${FAKE_RUN_SLEEP:-}" ]] || sleep "$FAKE_RUN_SLEEP"' \
  '    exit ${FAKE_RUN_EXIT:-0};;' \
  'esac' > "$T/bin/node"
printf '%s\n' '#!/bin/bash' 'echo "$PWD $*" >> /tmp/srs-chrome-lib-regression/npm.calls' \
  '[[ -z "${FAKE_NPM_FAIL:-}" ]] || exit 1' 'mkdir -p node_modules' > "$T/bin/npm"
chmod +x "$T/bin/node" "$T/bin/npm"
export CHROME_NODE="$T/bin/node" CHROME_NPM="$T/bin/npm" CHROME_NPM_LOCK="$T/npm-ci.lock"

# Gates: SRS_TEST_CHROME=0 skips before anything else; a bad name or slot fails.
check test "$(SRS_TEST_CHROME=0 chrome_test_init disabled 15600)" = 'SKIP SRS_TEST_CHROME=0'
check test "$(chrome_test_init 'bad name' 15600 >/dev/null 2>&1; echo $?)" = 1
check test "$(chrome_test_init ok 80 >/dev/null 2>&1; echo $?)" = 1
check test "$(SRS_TEST_PORT_BASE=abc chrome_test_init ok 15600 >/dev/null 2>&1; echo $?)" = 1

# The slot: RTMP +0, HTTP +1, API +2, RTC UDP +3, the page server +9.
slot_ports() (
  unset CHROME_SRS_HOST
  chrome_test_ports 15600
  [[ $CHROME_RTMP_PORT == 15600 && $CHROME_HTTP_PORT == 15601 && $CHROME_API_PORT == 15602 \
    && $CHROME_RTC_PORT == 15603 && $CHROME_PAGE_PORT == 15609 && $CHROME_API_URL == http://127.0.0.1:15602 \
    && $CHROME_HTTP_URL == http://127.0.0.1:15601 && $CHROME_RTMP_URL == rtmp://127.0.0.1:15600 ]] || return 1
  CHROME_SRS_HOST=172.24.0.5 chrome_test_ports 16000
  [[ $CHROME_API_URL == http://172.24.0.5:16002 && $CHROME_RTMP_URL == rtmp://172.24.0.5:16000 ]]
)
check slot_ports
# A case can run two SRS in its slot: the second on +4..+7, with its own log.
slot_second_srs() (
  unset CHROME_SRS_HOST
  chrome_test_ports 15700
  [[ $CHROME_SRS_NAME == srs ]] || return 1
  chrome_test_srs 1
  [[ $CHROME_RTMP_PORT == 15704 && $CHROME_HTTP_PORT == 15705 && $CHROME_API_PORT == 15706 \
    && $CHROME_RTC_PORT == 15707 && $CHROME_PAGE_PORT == 15709 && $CHROME_API_URL == http://127.0.0.1:15706 \
    && $CHROME_RTMP_URL == rtmp://127.0.0.1:15704 && $CHROME_SRS_NAME == srs-1 ]] || return 1
  chrome_test_srs 0
  [[ $CHROME_API_PORT == 15702 && $CHROME_SRS_NAME == srs ]] || return 1
  ! chrome_test_srs 4 2>/dev/null
)
check slot_second_srs
# A third and fourth SRS skip the page port: +10..+13 and +14..+17.
slot_more_srs() (
  unset CHROME_SRS_HOST
  chrome_test_ports 15700
  chrome_test_srs 2
  [[ $CHROME_RTMP_PORT == 15710 && $CHROME_HTTP_PORT == 15711 && $CHROME_API_PORT == 15712 \
    && $CHROME_RTC_PORT == 15713 && $CHROME_PAGE_PORT == 15709 && $CHROME_SRS_NAME == srs-2 ]] || return 1
  chrome_test_srs 3
  [[ $CHROME_RTMP_PORT == 15714 && $CHROME_HTTP_PORT == 15715 && $CHROME_API_PORT == 15716 \
    && $CHROME_RTC_PORT == 15717 && $CHROME_API_URL == http://127.0.0.1:15716 && $CHROME_SRS_NAME == srs-3 ]] || return 1
  ! chrome_test_srs -1 2>/dev/null
)
check slot_more_srs

# No usable Node or browser is a skip listed as unverified, not a failure.
check test "$(CHROME_NODE=/nonexistent/node chrome_test_init nonode 15600)" \
  = 'SKIP nonode: unverified: Chrome not available (no node)'
check test "$(FAKE_NODE_VERSION=v20.11.1 chrome_test_init oldnode 15600)" \
  = 'SKIP oldnode: unverified: Chrome not available (node v20.11.1, needs 22.12)'
check test "$(FAKE_NODE_VERSION=v22.11.0 chrome_test_init oldnode 15600)" \
  = 'SKIP oldnode: unverified: Chrome not available (node v22.11.0, needs 22.12)'
node_versions() {
  chrome_test_node_ok v22.12.0 && chrome_test_node_ok v24.19.0 && chrome_test_node_ok v23.0.0 \
    && ! chrome_test_node_ok v21.99.9 && ! chrome_test_node_ok garbage
}
check node_versions
no_browser() {
  local output
  output=$(FAKE_BROWSER_EXIT=2 CHROME_SKILL_DIR=$T/skill-ok chrome_test_init nobrowser 15600) || return 1
  [[ "$output" == 'SKIP nobrowser: unverified: Chrome not available (no browser)' ]]
}
mkdir -p "$T/skill-ok/node_modules"
touch "$T/skill-ok/package-lock.json"
touch "$T/skill-ok/node_modules"
check no_browser
# A CHROME_PATH that is set but wrong is a failure, so CI never hides it.
check test "$(FAKE_BROWSER_EXIT=3 CHROME_SKILL_DIR=$T/skill-ok chrome_test_init badpath 15600 >/dev/null 2>&1; echo $?)" = 1

# npm ci runs only when node_modules is missing or older than the lockfile,
# under a lock that parallel scripts share; a dead owner's lock is taken over.
npm_ci_cases() (
  local skill=$T/skill-npm
  CHROME_TEST_DIR=$T
  mkdir -p "$skill"
  touch "$skill/package-lock.json"
  rm -f "$T/npm.calls"
  chrome_test_npm_ci "$skill" || return 1
  [[ "$(cat "$T/npm.calls")" == "$skill ci --ignore-scripts --no-audit --no-fund" ]] || return 1
  rm -f "$T/npm.calls"
  chrome_test_npm_ci "$skill" || return 1
  [[ ! -f "$T/npm.calls" ]] || return 1
  touch -t 203001010000 "$skill/package-lock.json"
  chrome_test_npm_ci "$skill" || return 1
  [[ -f "$T/npm.calls" ]] || return 1
  [[ ! -d "$CHROME_NPM_LOCK" ]] || return 1
  # A lock left by a process that is gone.
  sleep 30 &
  local dead=$!
  kill "$dead"; wait "$dead" 2>/dev/null || true
  mkdir "$CHROME_NPM_LOCK"; echo "$dead" > "$CHROME_NPM_LOCK/pid"
  rm -f "$T/npm.calls"
  rm -rf "$T/skill-npm/node_modules"
  chrome_test_npm_ci "$skill" || return 1
  [[ -f "$T/npm.calls" && ! -d "$CHROME_NPM_LOCK" ]] || return 1
  rm -rf "$T/skill-npm/node_modules"
  if FAKE_NPM_FAIL=1 chrome_test_npm_ci "$skill" >/dev/null 2>&1; then return 1; fi
  [[ ! -d "$CHROME_NPM_LOCK" ]]
)
check npm_ci_cases
# A live owner keeps the lock until the wait gives up.
npm_ci_busy() (
  CHROME_TEST_DIR=$T CHROME_NPM_WAIT=1
  mkdir -p "$CHROME_NPM_LOCK"; echo $$ > "$CHROME_NPM_LOCK/pid"
  local output status=0
  output=$(chrome_test_npm_ci "$T/skill-npm" 2>&1) || status=$?
  rm -f "$CHROME_NPM_LOCK/pid"; rmdir "$CHROME_NPM_LOCK"
  [[ $status -eq 1 && "$output" == 'FAIL npm ci lock held by pid '* ]]
)
check npm_ci_busy

# The WebRTC candidate is the host's own IPv4 from the default route, or the
# override; with none, 127.0.0.1 and the browser's loopback flag.
host_ip_mac() (
  uname() { echo Darwin; }
  route() { printf '%s\n' '   route to: default' 'destination: default' '  interface: en9'; }
  ipconfig() { [[ "$*" == 'getifaddr en9' ]] && echo 192.168.3.203; }
  [[ "$(chrome_test_host_ip)" == 192.168.3.203 ]]
)
check host_ip_mac
host_ip_linux() (
  uname() { echo Linux; }
  ip() { echo '1.1.1.1 via 10.1.0.1 dev eth0 src 10.1.0.4 uid 1001'; }
  [[ "$(chrome_test_host_ip)" == 10.1.0.4 ]]
)
check host_ip_linux
# Git Bash on Windows: the active default route with the lowest metric; the
# persistent routes have no interface column.
host_ip_windows() (
  uname() { echo MINGW64_NT-10.0-19045; }
  route() {
    [[ "$*" == 'print -4 0.0.0.0' ]] || return 1
    printf '%s\r\n' 'IPv4 Route Table' '=====' 'Active Routes:' \
      'Network Destination        Netmask          Gateway       Interface  Metric' \
      '          0.0.0.0          0.0.0.0      10.0.0.1         10.0.0.7     50' \
      '          0.0.0.0          0.0.0.0      192.168.3.1     192.168.3.85     35' '=====' \
      'Persistent Routes:' '  Network Address          Netmask  Gateway Address  Metric' \
      '          0.0.0.0          0.0.0.0      172.16.0.1  1'
  }
  [[ "$(chrome_test_host_ip)" == 192.168.3.85 ]]
)
check host_ip_windows
eip_override() (
  CHROME_TEST_EIP=10.9.8.7
  uname() { echo Linux; }
  ip() { echo '1.1.1.1 via 10.1.0.1 dev eth0 src 10.1.0.4'; }
  chrome_test_eip
  [[ $CHROME_EIP == 10.9.8.7 && -z $CHROME_LOOPBACK ]]
)
check eip_override
eip_loopback() (
  unset CHROME_TEST_EIP
  uname() { echo Darwin; }
  route() { return 1; }
  chrome_test_eip
  [[ $CHROME_EIP == 127.0.0.1 && $CHROME_LOOPBACK == 1 ]]
)
check eip_loopback
eip_rejects_loopback_route() (
  unset CHROME_TEST_EIP
  uname() { echo Linux; }
  ip() { echo 'local 127.0.0.1 dev lo src 127.0.0.1'; }
  chrome_test_eip
  [[ $CHROME_EIP == 127.0.0.1 && $CHROME_LOOPBACK == 1 ]]
)
check eip_rejects_loopback_route

# Common state for the checks below, as init leaves it.
CHROME_CASE=regression
CHROME_TEST_DIR=$T
CHROME_JS=/fake/chrome.js
CHROME_STARTED=$SECONDS
CHROME_PASS_COUNT=0 CHROME_KNOWN_COUNT=0 CHROME_SKIP_COUNT=0 CHROME_RECORD_PID=
CHROME_EIP=192.168.3.203 CHROME_LOOPBACK=
chrome_test_ports 15600

# URLs carry the candidate address; stream names never repeat.
check test "$(chrome_test_whip_url s1)" = 'http://127.0.0.1:15602/rtc/v1/whip/?app=live&stream=s1&eip=192.168.3.203'
check test "$(chrome_test_whep_url s1)" = 'http://127.0.0.1:15602/rtc/v1/whep/?app=live&stream=s1&eip=192.168.3.203'
check test "$(chrome_test_flv_url s1)" = 'http://127.0.0.1:15601/live/s1.flv'
check test "$(chrome_test_ts_url s1)" = 'http://127.0.0.1:15601/live/s1.ts'
check test "$(chrome_test_hls_url s1)" = 'http://127.0.0.1:15601/live/s1.m3u8'
unique_streams() {
  local a b
  a=$(chrome_test_stream) b=$(chrome_test_stream)
  [[ "$a" == regression-* && "$a" != "$b" && "$a" =~ ^[A-Za-z0-9-]+$ ]]
}
check unique_streams

# The run: the page port, the stats file and the loopback flag go to the
# driver; its lines are indented; its exit code decides.
cat > "$T/stats.json" <<'EOF'
{"jobs":[
 {"name":"whip","result":"pass","samples":[
   {"t":1000,"outboundVideo":{"framesEncoded":10}},
   {"t":2000,"outboundVideo":{"framesEncoded":30}}]},
 {"name":"whep","result":"pass","samples":[
   {"t":1000,"inboundVideo":null,"inboundAudio":null},
   {"t":2000,"inboundVideo":{"framesDecoded":5,"frameWidth":640,"frameHeight":480,"packetsLost":-1},"inboundAudio":{"totalAudioEnergy":0.2}},
   {"t":3000,"inboundVideo":{"framesDecoded":65,"frameWidth":640,"frameHeight":480,"packetsLost":0},"inboundAudio":{"totalAudioEnergy":0.9}}]},
 {"name":"whip-h265","result":"skip","samples":[]}]}
EOF
run_passes() {
  local output
  rm -f "$T/node.calls"
  output=$(chrome_test_run r1 --mode whep --url http://x/) || return 1
  [[ "$output" == '  PASS whep ready playing in 120ms
  LOCATION whep http://127.0.0.1:15602/rtc/v1/whip/?action=delete&token=t1&app=live&stream=s&session=ab12:cd34
PASS chrome r1' ]] || return 1
  [[ "$(cat "$T/node.calls")" == "/fake/chrome.js run --page-port 15609 --out $T/r1-stats.json --mode whep --url http://x/" ]] \
    && [[ -f "$T/r1-stats.json" && -f "$T/r1.log" ]]
}
check run_passes
run_loopback() {
  rm -f "$T/node.calls"
  CHROME_LOOPBACK=1 chrome_test_run r2 --mode whep --url http://x/ >/dev/null || return 1
  [[ "$(cat "$T/node.calls")" == "/fake/chrome.js run --page-port 15609 --out $T/r2-stats.json --allow-loopback --mode whep --url http://x/" ]]
}
check run_loopback
run_fails() {
  local output status=0
  output=$(FAKE_RUN_EXIT=1 chrome_test_run r3 --mode whep --url http://x/ 2>&1) || status=$?
  [[ $status -eq 1 && "$output" == *'FAIL chrome r3 (exit 1, see '"$T"'/r3.log)' ]]
}
check run_fails
run_unreachable() {
  local output status=0
  output=$(FAKE_RUN_EXIT=6 chrome_test_run r4 --mode whep --url http://x/ 2>&1) || status=$?
  [[ $status -eq 1 && "$output" == *'FAIL chrome r4: SRS did not answer' ]]
}
check run_unreachable
run_skips() {
  local output status=0
  output=$(FAKE_RUN_EXIT=2 chrome_test_run r5 --mode whep --url http://x/ 2>&1) || status=$?
  [[ $status -eq 0 && "$output" == *'SKIP regression: unverified: Chrome not available (r5)' ]]
}
check run_skips
# The driver gets the rest of the 60 s budget and no more.
run_over_budget() {
  local output status=0
  output=$(CHROME_STARTED=$((SECONDS - 58)) FAKE_RUN_SLEEP=5 chrome_test_run r6 --mode whep --url http://x/ 2>&1) || status=$?
  [[ $status -eq 1 && "$output" == *'FAIL command timed out'* ]]
}
check run_over_budget
# A run in the background: the case acts on a job once it is ready, then
# waits for the run; the job's session comes from its LOCATION line.
run_in_background() {
  local output
  FAKE_RUN_SLEEP=1 chrome_test_run_start r7 --mode whep --url http://x/ || return 1
  [[ -n $CHROME_COMMAND_PID ]] || return 1
  chrome_test_run_ready whep || return 1
  kill -0 "$CHROME_COMMAND_PID" || return 1
  [[ "$(chrome_test_session whep)" == ab12:cd34 ]] || return 1
  # Not in a subshell: only this shell can wait for the run.
  chrome_test_run_wait > "$T/r7.out" || return 1
  [[ "$(tail -n 1 "$T/r7.out")" == 'PASS chrome r7' && -f "$T/r7-stats.json" ]]
}
check run_in_background
run_ready_never() {
  local output status=0
  chrome_test_run_start r8 --mode whep --url http://x/ || return 1
  output=$(chrome_test_run_ready nosuch 2>&1) || status=$?
  chrome_test_run_wait >/dev/null 2>&1 || true
  [[ $status -eq 1 && "$output" == "FAIL chrome r8: job nosuch not ready (see $T/r8.log)" ]]
}
check run_ready_never
run_ready_waits() {
  FAKE_RUN_DELAY=0.5 FAKE_RUN_SLEEP=0.5 chrome_test_run_start r9 --mode whep --url http://x/ || return 1
  grep -q 'ready' "$T/r9.log" 2>/dev/null && return 1
  chrome_test_run_ready whep || return 1
  grep -q '^PASS whep ready' "$T/r9.log" && chrome_test_run_wait >/dev/null
}
check run_ready_waits
check test "$(chrome_test_session nosuch)" = ''
# Cleanup stops a run still in the background.
cleanup_run() {
  FAKE_RUN_SLEEP=30 chrome_test_run_start r10 --mode whep --url http://x/ || return 1
  local pid=$CHROME_COMMAND_PID
  chrome_test_cleanup || return 1
  ! kill -0 "$pid" 2>/dev/null && [[ -z $CHROME_COMMAND_PID ]]
}
check cleanup_run
check test "$(chrome_test_now_ms)" = 1700000000123
# The NACK simulator drops packets of one session, by its username.
sim_drop() (
  curl() { echo "$*" > "$T/curl.args"; echo '{"code":0,"query":{"drop":"30"}}'; }
  local output
  # SRS reads query values without percent-decoding, so a ufrag with + or /
  # goes as SRS wrote it in the Location.
  output=$(chrome_test_drop ab12:+c/4 30) || return 1
  [[ "$output" == 'DROP 30 packets of session ab12:+c/4' ]] || return 1
  [[ "$(cat "$T/curl.args")" == '-fsS --max-time 2 http://127.0.0.1:15602/rtc/v1/nack/?username=ab12:+c/4&drop=30' ]]
)
check sim_drop
sim_drop_refused() (
  curl() { echo '{"code":5021}'; }
  [[ "$(out_status chrome_test_drop nosuch:x 30)" == 'FAIL simulator drop 30 on nosuch:x: {"code":5021} 1' ]]
)
check sim_drop_refused

# Stats assertions read one job of a run's stats file, with deltas from the
# first sample that has the field.
check chrome_test_job_assert r1 whep 'rise(.inboundVideo.framesDecoded) >= 40' 'frames +60'
check chrome_test_job_assert r1 whep 'latest(.inboundVideo.frameWidth) == 640' 'width'
check chrome_test_job_assert r1 whep 'earliest(.inboundVideo.packetsLost) == -1' 'first lost'
check chrome_test_job_assert r1 whip 'rise(.outboundVideo.framesEncoded) == 20' 'encoded +20'
check test "$(out_status chrome_test_job_assert r1 whep 'rise(.inboundVideo.framesDecoded) >= 100' 'frames +100')" \
  = 'FAIL frames +100 (job whep in r1-stats.json) 1'
check test "$(out_status chrome_test_job_assert r1 nosuch 'true' 'missing job')" \
  = 'FAIL missing job (job nosuch in r1-stats.json) 1'
# Samples split at a wall-clock time, such as a drop, with series(f) over each part.
cat > "$T/r11-stats.json" <<'EOF2'
{"jobs":[{"name":"whep","result":"pass","samples":[
  {"t":1000,"at":5000,"inboundVideo":{"nackCount":0,"packetsLost":0}},
  {"t":2000,"at":6000,"inboundVideo":{"nackCount":0,"packetsLost":-1}},
  {"t":3000,"at":7000,"inboundVideo":{"nackCount":1,"packetsLost":15}},
  {"t":4000,"at":8000,"inboundVideo":{"nackCount":1,"packetsLost":0}},
  {"t":4500,"inboundVideo":{"nackCount":9}}]}]}
EOF2
check chrome_test_job_assert r11 whep '(before(6500) | series(.inboundVideo.nackCount)) == [0,0]' 'before'
check chrome_test_job_assert r11 whep '(since(6500) | series(.inboundVideo.packetsLost)) == [15,0]' 'since'
check chrome_test_job_assert r11 whep '(before(5000) | length) == 0 and (since(9000) | length) == 0' 'empty sides'
# settled(s): the samples of s from a second after the first decoded frame,
# past the startup freeze a loaded host can show.
cat > "$T/r12-stats.json" <<'EOF2'
{"jobs":[{"name":"whep","result":"pass","samples":[
  {"t":1000,"at":5000,"inboundVideo":{"framesDecoded":0,"freezeCount":0}},
  {"t":2000,"at":6000,"inboundVideo":{"framesDecoded":18,"freezeCount":0}},
  {"t":2900,"at":6900,"inboundVideo":{"framesDecoded":25,"freezeCount":1}},
  {"t":3000,"at":7000,"inboundVideo":{"framesDecoded":47,"freezeCount":1}},
  {"t":4000,"at":8000,"inboundVideo":{"framesDecoded":66,"freezeCount":1}}]}]}
EOF2
check chrome_test_job_assert r12 whep '(settled([.samples[]]) | series(.inboundVideo.freezeCount)) == [1,1]' 'settled'
check chrome_test_job_assert r12 whep '(settled(before(7500)) | map(.t)) == [3000]' 'settled before'
check chrome_test_job_assert r12 whep '(settled(before(5500)) | length) == 0' 'settled no frame'
check test "$(chrome_test_startup_freeze r12 whep WHEP)" = 'INFO WHEP froze 1 times in its first second of play'
check test "$(chrome_test_startup_freeze r11 whep WHEP)" = ''
check test "$(chrome_test_startup_freeze r12 nosuch X)" = ''
# A freeze check that fails while the player's SRS logged a stall of its own
# loop is KNOWN F5; without such a line it fails.
printf '%s\n' 'x' $'\e[33m[2026-10-29 00:50:23.017][WARN][2264][e398dg90][11][LARGE_TIMER] twcc delay 286ms > 100ms, count=1/1' \
  $'\e[33m[2026-10-29 00:50:25.100][WARN][2264][e398dg90][11][LARGE_TIMER] twcc delay 150ms > 100ms, count=1/1' > "$T/stall.log"
printf 'x\n' > "$T/calm.log"
check test "$(out_status chrome_test_freeze_assert r12 whep 'true' 'no freeze' calm)" = 'PASS no freeze 0'
check test "$(out_status chrome_test_freeze_assert r12 whep 'true' 'no freeze' stall)" = 'PASS no freeze 0'
check test "$(out_status chrome_test_freeze_assert r12 whep 'false' 'no freeze' calm)" \
  = 'FAIL no freeze (job whep in r12-stats.json) 1'
check test "$(out_status chrome_test_freeze_assert r12 whep 'false' 'no freeze' nosuch)" \
  = 'FAIL no freeze (job whep in r12-stats.json) 1'
check test "$(out_status chrome_test_freeze_assert r12 whep 'false' 'no freeze' stall)" \
  = 'KNOWN F5 no freeze: SRS stalled 2 times, first twcc delay 286ms > 100ms at 00:50:23.017 (stall.log) 0'
count_known() (
  before=$CHROME_KNOWN_COUNT
  chrome_test_freeze_assert r12 whep 'false' 'counted' stall >/dev/null
  [[ $CHROME_KNOWN_COUNT -eq $((before + 1)) ]]
)
check count_known
check test "$(chrome_test_job_result r1 whip-h265)" = skip
check test "$(chrome_test_job_result r1 whep)" = pass
check test "$(chrome_test_job_result r1 nosuch)" = ''
count_passes() {
  local before=$CHROME_PASS_COUNT
  chrome_test_job_assert r1 whep 'true' 'counted' >/dev/null
  [[ $CHROME_PASS_COUNT -eq $((before + 1)) ]]
}
check count_passes

# API assertions poll /api/v1/streams/ until the filter holds or the wait ends.
chrome_test_read_api() { printf '%s\n' "$fixture"; }
CHROME_API_WAIT=0
fixture='{"code":0,"streams":[{"app":"live","name":"s1","publish":{"active":true},"video":{"codec":"H264"},"audio":{"codec":"AAC"}}]}'
check chrome_test_api_assert 'any(.streams[]; .name == "s1" and .video.codec == "H264")' 'API s1 H264'
check test "$(out_status chrome_test_api_assert 'any(.streams[]; .name == "s1" and .video.codec == "VP9")' 'API s1 VP9')" \
  = 'FAIL API s1 VP9 (see '"$T"'/streams.json) 1'
check chrome_test_wait_stream s1
check test "$(out_status chrome_test_wait_stream s2)" = 'FAIL stream live/s2 not published in 0s 1'
fixture='{"code":0,"streams":[{"app":"live","name":"s1","publish":{"active":false}}]}'
check test "$(chrome_test_wait_stream s1 >/dev/null 2>&1; echo $?)" = 1
# A watch polls the API in the background while the browser runs.
fixture='{"code":0,"streams":[{"app":"live","name":"s1","publish":{"active":true},"video":{"codec":"H264"}}]}'
api_watch_pass() {
  chrome_test_api_watch 'any(.streams[]; .name == "s1")'
  [[ -n $CHROME_WATCH_PID ]] || return 1
  chrome_test_api_watched 'API watch s1' > "$T/watch.out" || return 1
  [[ "$(cat "$T/watch.out")" == 'PASS API watch s1' && -z $CHROME_WATCH_PID ]]
}
check api_watch_pass
api_watch_fail() {
  local status=0
  chrome_test_api_watch 'any(.streams[]; .name == "s2")'
  chrome_test_api_watched 'API watch s2' > "$T/watch.out" 2>&1 || status=$?
  [[ $status -eq 1 && "$(cat "$T/watch.out")" == "FAIL API watch s2 (see $T/streams.json)" ]]
}
check api_watch_fail
# Cleanup stops a watch that is still polling.
cleanup_watch() {
  CHROME_API_WAIT=30 chrome_test_api_watch 'any(.streams[]; .name == "s3")'
  local pid=$CHROME_WATCH_PID
  chrome_test_cleanup || return 1
  ! kill -0 "$pid" 2>/dev/null && [[ -z $CHROME_WATCH_PID ]]
}
check cleanup_watch
# A record keeps every answer while publishers come and go; a check passes
# when its filter held in any of them.
api_record_seen() {
  fixture='{"code":0,"streams":[{"app":"live","name":"s1","publish":{"active":true},"video":{"codec":"VP9"}}]}'
  chrome_test_api_record
  [[ -n $CHROME_RECORD_PID ]] || return 1
  local deadline=$((SECONDS + 5))
  until [[ -s "$T/streams-record.json" ]]; do [[ $SECONDS -lt $deadline ]] || return 1; sleep 0.1; done
  chrome_test_api_record_stop
  [[ -z $CHROME_RECORD_PID ]] || return 1
  chrome_test_api_seen 'any(.streams[]; .name == "s1" and .video.codec == "VP9")' 'API s1 VP9' > "$T/seen.out" || return 1
  [[ "$(cat "$T/seen.out")" == 'PASS API s1 VP9' ]] || return 1
  [[ "$(out_status chrome_test_api_seen 'any(.streams[]; .name == "s1" and .video.codec == "AV1")' 'API s1 AV1')" \
    == "FAIL API s1 AV1 (see $T/streams-record.json) 1" ]]
}
check api_record_seen
# A cut-off last answer does not hide the earlier ones.
api_record_partial() {
  printf '%s\n' '{"streams":[{"name":"s2","video":{"codec":"AV1"}}]}' '{"streams":[{"na' > "$T/streams-record.json"
  chrome_test_api_seen 'any(.streams[]; .name == "s2" and .video.codec == "AV1")' 'API s2 AV1' >/dev/null
}
check api_record_partial
cleanup_record() {
  chrome_test_api_record
  local pid=$CHROME_RECORD_PID
  chrome_test_cleanup || return 1
  ! kill -0 "$pid" 2>/dev/null && [[ -z $CHROME_RECORD_PID ]]
}
check cleanup_record
# The simulator is a build feature the loss cases need.
fixture='{"code":0,"data":{"features":{"rtc":true,"simulator":true}}}'
check chrome_test_simulator
fixture='{"code":0,"data":{"features":{"rtc":true,"simulator":false}}}'
check test "$(chrome_test_simulator; echo $?)" = 1
unset -f chrome_test_read_api

# hls.js fails at once on a missing playlist, so a case waits until SRS
# serves it. SRS answers 404 until its first segment is written, then a
# playlist that names the media playlist with an hls_ctx session.
hls_ready() (
  curl() {
    [[ "$*" == *http://127.0.0.1:15601/live/s1.m3u8 ]] || return 22
    printf '%s\n' '#EXTM3U' '#EXT-X-STREAM-INF:BANDWIDTH=1,AVERAGE-BANDWIDTH=1' '/live/s1.m3u8?hls_ctx=l537k19l'
  }
  chrome_test_wait_hls s1
)
check hls_ready
hls_not_ready() (
  curl() { [[ "$*" == *s1.m3u8 ]] || return 22; echo '<html>'; }
  CHROME_API_WAIT=0
  local status=0 output
  output=$(chrome_test_wait_hls s1 2>&1) || status=$?
  [[ $status -eq 1 && "$output" == 'FAIL HLS live/s1.m3u8 not served in 0s' ]] || return 1
  status=0
  output=$(chrome_test_wait_hls s2 2>&1) || status=$?
  [[ $status -eq 1 && "$output" == 'FAIL HLS live/s2.m3u8 not served in 0s' ]]
)
check hls_not_ready

# SRS log lines are counted for the NACK checks.
printf '%s\n' 'NACK: resend RTX a' 'other' 'NACK: resend RTX b' > "$T/srs.log"
check test "$(chrome_test_srs_log_count 'NACK: resend RTX')" = 2
check test "$(chrome_test_srs_log_count 'NACK: resend plain')" = 0
srs_log_per_instance() (
  printf '%s\n' 'NACK: resend plain x' > "$T/srs-1.log"
  chrome_test_srs 1
  [[ "$(chrome_test_srs_log_count 'NACK: resend plain')" == 1 && "$(chrome_test_srs_log_count 'NACK: resend RTX')" == 0 ]]
)
check srs_log_per_instance

# The budget, KNOWN and the loopback finding.
CHROME_STARTED=$((SECONDS - 61))
check test "$(out_status chrome_test_budget)" = 'FAIL budget 61s > 60s 1'
CHROME_STARTED=$SECONDS
check chrome_test_budget
known_counts() {
  local before=$CHROME_PASS_COUNT
  chrome_test_known F2 'API size' >/dev/null
  [[ $CHROME_KNOWN_COUNT -eq 1 && $CHROME_PASS_COUNT -eq $before ]]
}
check known_counts
# A part the browser cannot run is skipped and listed, and the case goes on.
skipped_counts() {
  local before=$CHROME_PASS_COUNT output
  output=$(chrome_test_skipped 'codec h265: unverified: not in the capabilities'; echo after)
  [[ "$output" == 'SKIP codec h265: unverified: not in the capabilities
after' ]] || return 1
  chrome_test_skipped 'codec av1: unverified: x' >/dev/null
  [[ $CHROME_SKIP_COUNT -eq 1 && $CHROME_PASS_COUNT -eq $before ]]
}
check skipped_counts
result_line() (
  CHROME_CASE=codec CHROME_PASS_COUNT=3 CHROME_KNOWN_COUNT=1 CHROME_SKIP_COUNT=2 CHROME_STARTED=$SECONDS
  CHROME_SRS_OWNED= CHROME_COMMAND_PID= CHROME_WATCH_PID= CHROME_RECORD_PID= CHROME_FFMPEG_PIDS=
  [[ "$(chrome_test_finish)" == 'RESULT codec passed=3 known=1 skipped=2 elapsed=0s' ]]
)
check result_line
check test "$(CHROME_LOOPBACK=1 chrome_test_loopback_known 'WHEP play')" = 'KNOWN F1 WHEP play (loopback candidate)'
check test "$(chrome_test_loopback_known 'WHEP play'; echo $?)" = 1

# SRS starts env-only from the slot with the candidate, and extra settings win.
srs_start_env() (
  local fake=$T/ws
  mkdir -p "$fake/trunk/objs"
  printf '%s\n' '#!/bin/bash' \
    'echo "$0 $* RTMP=$SRS_RTMP_LISTEN API=$SRS_HTTP_API_LISTEN RTC=$SRS_RTC_SERVER_LISTEN CAND=$SRS_RTC_SERVER_CANDIDATE R2R=$SRS_VHOST_RTC_RTMP_TO_RTC RTX=${SRS_VHOST_RTC_NACK_PREFER_RTX:-}"' \
    > "$fake/trunk/objs/srs"
  chmod +x "$fake/trunk/objs/srs"
  CHROME_WORKSPACE=$fake
  chrome_test_start_srs SRS_VHOST_RTC_NACK_PREFER_RTX=on
  wait "$CHROME_SRS_PID"
  [[ "$(cat "$T/srs.log")" == './objs/srs -e RTMP=15600 API=15602 RTC=15603 CAND=192.168.3.203 R2R=on RTX=on' ]] \
    && [[ "$(cat "$T/srs.pid")" == "$CHROME_SRS_PID" ]]
)
check srs_start_env
srs_start_second() (
  local fake=$T/ws
  CHROME_WORKSPACE=$fake CHROME_SRS_OWNED=
  chrome_test_start_srs
  local first=$CHROME_SRS_PID
  wait "$first"
  chrome_test_srs 1
  chrome_test_start_srs SRS_VHOST_RTC_NACK_PREFER_RTX=on
  wait "$CHROME_SRS_PID"
  [[ "$(cat "$T/srs-1.log")" == './objs/srs -e RTMP=15604 API=15606 RTC=15607 CAND=192.168.3.203 R2R=on RTX=on' ]] \
    && [[ "$(cat "$T/srs-1.pid")" == "$CHROME_SRS_PID" && "$CHROME_SRS_OWNED" == "srs:$first srs-1:$CHROME_SRS_PID" ]]
)
check srs_start_second
# With CHROME_SRS_HOST, start never spawns SRS and only waits for its API.
remote_srs_start() (
  CHROME_SRS_HOST=172.24.0.5 CHROME_SRS_PID=
  chrome_test_ports 16000
  chrome_test_start_srs() { echo spawned > "$T/spawned"; }
  curl() { [[ $* == *http://172.24.0.5:16002/api/v1/versions ]]; }
  rm -f "$T/spawned"
  chrome_test_start || return 1
  [[ -z $CHROME_SRS_PID && ! -f "$T/spawned" ]]
)
check remote_srs_start
srs_exits_at_start() (
  CHROME_SRS_PID=
  chrome_test_start_srs() { sleep 0 & CHROME_SRS_PID=$!; }
  curl() { return 7; }
  local output status=0
  output=$(chrome_test_start 2>&1) || status=$?
  [[ $status -eq 1 && "$output" == "FAIL SRS exited during startup (see $T/srs.log)" ]]
)
check srs_exits_at_start

# With CHROME_SRS_DOCKER, each SRS runs in a container of that build volume,
# with the same settings, its TCP ports on 127.0.0.1 and its RTC port on the
# candidate address; its console log still goes to the case's log file.
printf '%s\n' '#!/bin/bash' 'echo "$*" >> /tmp/srs-chrome-lib-regression/docker.calls' \
  'case "$1" in' \
  '  info) exit ${FAKE_DOCKER_INFO:-0};;' \
  '  run) if [[ " $* " == *" --container-build"* ]]; then cat > /tmp/srs-chrome-lib-regression/docker.stdin; exit ${FAKE_DOCKER_BUILD:-0}; fi' \
  '    echo "srs in docker"; [[ -z "${FAKE_DOCKER_SLEEP:-}" ]] || exec sleep "$FAKE_DOCKER_SLEEP";;' \
  'esac' > "$T/bin/docker"
chmod +x "$T/bin/docker"
docker_srs_start() (
  CHROME_SRS_DOCKER=vol1 CHROME_DOCKER=$T/bin/docker CHROME_SRS_OWNED=
  rm -f "$T/docker.calls"
  chrome_test_start_srs SRS_VHOST_RTC_NACK_PREFER_RTX=on
  wait "$CHROME_SRS_PID"
  local calls
  calls=$(cat "$T/docker.calls")
  [[ "$calls" == "run --rm --name srs-chrome-regression-srs-15600 -p 127.0.0.1:15600:15600 -p 127.0.0.1:15601:15601 -p 127.0.0.1:15602:15602 -p 192.168.3.203:15603:15603/udp -v vol1:/build:ro -v $T:$T -w /build/srs/trunk -e SRS_RTMP_LISTEN=15600 "* ]] || return 1
  [[ "$calls" == *" -e SRS_RTC_SERVER_CANDIDATE=192.168.3.203 "* && "$calls" == *" -e SRS_HTTP_SERVER_DIR=$T/html "* ]] || return 1
  [[ "$calls" == *" -e SRS_VHOST_RTC_NACK_PREFER_RTX=on ossrs/srs:ubuntu20 ./objs/srs -e" ]] || return 1
  [[ "$(cat "$T/srs.log")" == 'srs in docker' && "$(cat "$T/srs.pid")" == "$CHROME_SRS_PID" ]] || return 1
  # The second SRS of the slot gets its own container, ports and log.
  rm -f "$T/docker.calls"
  chrome_test_srs 1
  CHROME_SRS_IMAGE=img:2 chrome_test_start_srs
  wait "$CHROME_SRS_PID"
  calls=$(cat "$T/docker.calls")
  [[ "$calls" == "run --rm --name srs-chrome-regression-srs-1-15600 -p 127.0.0.1:15604:15604 -p 127.0.0.1:15605:15605 -p 127.0.0.1:15606:15606 -p 192.168.3.203:15607:15607/udp "* ]] \
    && [[ "$calls" == *" img:2 ./objs/srs -e" && -f "$T/srs-1.log" ]]
)
check docker_srs_start
# A Docker SRS needs no local build.
check test "$(CHROME_SRS_BIN=objs/missing CHROME_SKILL_DIR=$T/skill-ok out_status chrome_test_init nolocal 15600)" \
  = "FAIL build trunk/objs/missing first 1"
docker_init() {
  local output
  output=$(CHROME_SRS_DOCKER=vol1 CHROME_SRS_BIN=objs/missing CHROME_SKILL_DIR=$T/skill-ok CHROME_TEST_EIP=10.9.8.7 \
    chrome_test_init dockerinit 15600 2>&1) || return 1
  [[ "$output" == 'BROWSER /fake/chrome, candidate 10.9.8.7' ]]
}
check docker_init
# Cleanup stops the docker client by its pid, then removes its container.
docker_cleanup() (
  CHROME_SRS_DOCKER=vol1 CHROME_DOCKER=$T/bin/docker CHROME_SRS_OWNED= FAKE_DOCKER_SLEEP=30
  chrome_test_srs 0
  chrome_test_start_srs
  local srs=$CHROME_SRS_PID
  until grep -q '^run ' "$T/docker.calls" 2>/dev/null; do sleep 0.05; done
  chrome_test_cleanup || return 1
  ! kill -0 "$srs" 2>/dev/null && [[ "$(tail -n 1 "$T/docker.calls")" == 'rm -f srs-chrome-regression-srs-15600' ]] \
    && [[ ! -f "$T/srs.pid" ]]
)
check docker_cleanup

# FFmpeg publishes the sample file in a loop, owned by this shell.
ffmpeg_publish() (
  local fake=$T/ws2
  mkdir -p "$fake/trunk/doc"
  : > "$fake/trunk/doc/source.flv"
  CHROME_WORKSPACE=$fake CHROME_FFMPEG_PIDS=
  ffmpeg() { echo "$*" > "$T/ffmpeg.args"; }
  chrome_test_publish_ffmpeg s9
  wait $CHROME_FFMPEG_PIDS
  [[ "$(cat "$T/ffmpeg.args")" == "-hide_banner -loglevel error -re -stream_loop -1 -i $fake/trunk/doc/source.flv -c copy -f flv rtmp://127.0.0.1:15600/live/s9" ]]
)
check ffmpeg_publish

# Cleanup stops only what this shell started: FFmpeg, and SRS by its pid file.
cleanup_owned() {
  sleep 30 &
  local srs=$!
  printf '%s\n' "$srs" > "$T/srs.pid"
  sleep 30 &
  local srs1=$!
  printf '%s\n' "$srs1" > "$T/srs-1.pid"
  CHROME_SRS_OWNED="srs:$srs srs-1:$srs1" CHROME_SRS_PID=$srs1
  sleep 30 &
  CHROME_FFMPEG_PIDS=$!
  local ff=$CHROME_FFMPEG_PIDS
  chrome_test_cleanup || return 1
  ! kill -0 "$srs" 2>/dev/null && ! kill -0 "$srs1" 2>/dev/null && ! kill -0 "$ff" 2>/dev/null \
    && [[ ! -f "$T/srs.pid" && ! -f "$T/srs-1.pid" && -z $CHROME_FFMPEG_PIDS && -z $CHROME_SRS_OWNED ]]
}
check cleanup_owned
cleanup_foreign_pid() {
  sleep 30 &
  local mine=$!
  CHROME_SRS_OWNED="srs:$mine"
  echo 1 > "$T/srs.pid"
  local status=0
  chrome_test_cleanup >/dev/null 2>&1 || status=$?
  kill "$mine"; wait "$mine" 2>/dev/null || true
  CHROME_SRS_OWNED=
  [[ $status -eq 1 ]]
}
check cleanup_foreign_pid

# chrome-lane-test.sh in a fake scripts folder: fake cases and self-test, the
# real helper, and the fake node for the driver's unit tests.
L=$T/lane
mkdir -p "$L"
cp "$SCRIPT_DIR/chrome-lane-test.sh" "$LIB" "$L/"
printf '%s\n' '#!/bin/bash' 'echo "start unit-lib" >> /tmp/srs-chrome-lib-regression/lane.calls' 'sleep 0.1' \
  '[[ "$FAKE_LIB_FAIL" != 1 ]] || { echo "FAIL regression 7 x"; exit 1; }' \
  'echo "PASS regression 1 a"; echo "PASS regression 2 b"; echo "PASS 2 harness regressions"' \
  'echo "end unit-lib" >> /tmp/srs-chrome-lib-regression/lane.calls' > "$L/chrome-test-lib-test.sh"
for name in webrtc loss http codec; do
  printf '%s\n' '#!/bin/bash' "name=$name" \
    'echo "start $name $SRS_TEST_PORT_BASE" >> /tmp/srs-chrome-lib-regression/lane.calls' 'sleep 0.2' \
    'echo "end $name" >> /tmp/srs-chrome-lib-regression/lane.calls' \
    '[[ "$FAKE_CASE_SKIP" != $name ]] || { echo "SKIP $name: unverified: Chrome not available (no browser)"; exit 0; }' \
    'echo "PASS $name check"; echo "  PASS $name driver line"' \
    'known=0; [[ $name != webrtc ]] || { echo "KNOWN F3 $name track"; known=1; }' \
    '[[ "$FAKE_CASE_FAIL" != $name ]] || { echo "FAIL $name check" >&2; exit 1; }' \
    'echo "RESULT $name passed=1 known=$known skipped=0 elapsed=1s"' > "$L/chrome-$name-test.sh"
done
run_lane() {
  rm -f "$T/lane.calls" "$T/npm.calls"
  local status=0
  env SRS_TEST_PORT_BASE= SRS_TEST_CHROME= CHROME_SKILL_DIR=$T/skill-ok FAKE_LIB_FAIL= FAKE_CASE_FAIL= FAKE_CASE_SKIP= \
    "$@" bash "$L/chrome-lane-test.sh" ${LANE_ARGS:-} > "$T/lane.out" 2>&1 || status=$?
  echo "$status" > "$T/lane.code"
}
lane_says() { grep -qxF -- "$1" "$T/lane.out"; }
lane_line() { grep -n -m 1 -F -- "$1" "$T/lane.calls" | cut -d: -f1; }
# Every case starts after both unit suites end, and all four run at once.
lane_units_first() {
  local unit=$(lane_line 'unit driver') lib=$(lane_line 'end unit-lib') name start
  local first_end=$(grep -n -m 1 -E '^end (webrtc|loss|http|codec)$' "$T/lane.calls" | cut -d: -f1)
  [[ -n $unit && -n $lib && -n $first_end ]] || return 1
  for name in webrtc loss http codec; do
    start=$(lane_line "start $name ")
    [[ -n $start && $start -gt $unit && $start -gt $lib && $start -lt $first_end ]] || return 1
  done
}
lane_slots() {
  grep -qx "start webrtc $1" "$T/lane.calls" && grep -qx "start loss $(($1 + 100))" "$T/lane.calls" \
    && grep -qx "start http $(($1 + 200))" "$T/lane.calls" && grep -qx "start codec $(($1 + 300))" "$T/lane.calls"
}

# SRS_TEST_CHROME=0 runs nothing.
run_lane SRS_TEST_CHROME=0
check test "$(cat "$T/lane.out") $(cat "$T/lane.code")" = 'SKIP SRS_TEST_CHROME=0 0'
check test ! -f "$T/lane.calls"
# Default: the unit suites, then the four cases on 15600-15900; only the
# lane's and the cases' own lines are at the start of a line.
run_lane
check test "$(cat "$T/lane.code")" = 0
check lane_units_first
check lane_slots 15600
check grep -qx "unit driver $T/skill-ok" "$T/lane.calls"
check lane_says 'PASS unit driver: 3 tests'
check lane_says 'PASS unit chrome-test-lib-test.sh: 2 checks'
check test "$(grep -c '^PASS regression' "$T/lane.out")" = 0
check lane_says 'PASS loss check'
check lane_says '  PASS loss driver line'
check lane_says 'KNOWN F3 webrtc track'
check lane_says 'LANE codec passed'
check grep -qE '^LANE chrome passed=6 skipped=0 known=1 failed=0 elapsed=[0-9]+s$' "$T/lane.out"
check run_lane SRS_TEST_PORT_BASE=16000
check lane_slots 16000
# A failing or skipped case, or a failing unit suite, is counted; every case
# still runs.
run_lane FAKE_CASE_FAIL=loss FAKE_CASE_SKIP=http
check test "$(cat "$T/lane.code")" = 1
check lane_says 'LANE loss failed exit=1'
check lane_says 'LANE http skipped'
check grep -qE '^LANE chrome passed=4 skipped=1 known=1 failed=1 ' "$T/lane.out"
run_lane FAKE_TEST_PASS=1 FAKE_TEST_EXIT=1 FAKE_TEST_FAIL=2 FAKE_LIB_FAIL=1
check test "$(cat "$T/lane.code")" = 1
check grep -q '^FAIL unit driver: exit 1, 2 of 3 tests failed' "$T/lane.out"
check grep -q '^FAIL unit chrome-test-lib-test.sh: exit 1' "$T/lane.out"
check lane_says '  FAIL regression 7 x'
check grep -qE '^LANE chrome passed=4 skipped=0 known=1 failed=2 ' "$T/lane.out"
check lane_slots 15600
# No node: the driver's suite is a skip listed as unverified, not a failure.
run_lane CHROME_NODE=/nonexistent/node
check test "$(cat "$T/lane.code")" = 0
check lane_says 'SKIP driver: unverified: Chrome not available (no node)'
check grep -qE '^LANE chrome passed=5 skipped=1 known=1 failed=0 ' "$T/lane.out"
# The lane installs the skill's packages before the cases, so they never wait
# on each other's npm ci.
rm -rf "$T/skill-lane"
mkdir -p "$T/skill-lane"
touch "$T/skill-lane/package-lock.json"
run_lane CHROME_SKILL_DIR=$T/skill-lane
check test "$(cat "$T/lane.code")" = 0
check grep -q "^$T/skill-lane ci" "$T/npm.calls"
check test -d "$T/skill-lane/node_modules"
# --cases-only runs the four cases without the unit suites.
LANE_ARGS=--cases-only run_lane SRS_TEST_PORT_BASE=16000 FAKE_LIB_FAIL=1 FAKE_TEST_EXIT=1
check test "$(cat "$T/lane.code")" = 0
check lane_slots 16000
check test "$(grep -c 'unit' "$T/lane.calls")" = 0
check grep -qE '^LANE chrome passed=4 skipped=0 known=1 failed=0 ' "$T/lane.out"

# chrome-linux-test.sh in a fake checkout: the fake docker builds and the fake
# lane's cases run against SRS in Docker on 16000-16300.
W=$T/linuxws
mkdir -p "$W/trunk" "$W/skills/srs-develop/scripts"
cp "$L"/* "$W/skills/srs-develop/scripts/"
cp "$SCRIPT_DIR/chrome-linux-test.sh" "$W/skills/srs-develop/scripts/"
for name in webrtc loss http codec; do
  printf '%s\n' "echo \"env $name \${CHROME_SRS_DOCKER:-none} \${CHROME_TEST_ROOT:-none} \${CHROME_SRS_IMAGE:-none}\" >> /tmp/srs-chrome-lib-regression/lane.calls" \
    >> "$W/skills/srs-develop/scripts/chrome-$name-test.sh"
done
echo a > "$W/trunk/a.txt"
git -C "$W" init -q
git -C "$W" add trunk/a.txt
git -C "$W" -c user.name=t -c user.email=t@t commit -q -m t
run_linux() {
  rm -f "$T/lane.calls" "$T/docker.calls" "$T/docker.stdin"
  local status=0
  env SRS_TEST_PORT_BASE= SRS_TEST_CHROME= CHROME_SKILL_DIR=$T/skill-ok CHROME_DOCKER=$T/bin/docker \
    FAKE_DOCKER_INFO= FAKE_DOCKER_BUILD= "$@" bash "$W/skills/srs-develop/scripts/chrome-linux-test.sh" ${LINUX_ARGS:-} \
    > "$T/linux.out" 2>&1 || status=$?
  echo "$status" > "$T/linux.code"
}
linux_says() { grep -qxF -- "$1" "$T/linux.out"; }
run_linux SRS_TEST_CHROME=0
check test "$(cat "$T/linux.out") $(cat "$T/linux.code")" = 'SKIP SRS_TEST_CHROME=0 0'
check test ! -f "$T/docker.calls"
run_linux FAKE_DOCKER_INFO=1
check test "$(cat "$T/linux.out") $(cat "$T/linux.code")" = 'SKIP linux: unverified: Docker not available 0'
LINUX_ARGS=--bad run_linux
check test "$(cat "$T/linux.code")" = 3
run_linux SRS_TEST_PORT_BASE=80
check test "$(cat "$T/linux.code")" = 3
# --build copies the tracked trunk files into the volume and builds there with
# obs-linux-test.sh's Linux build, then stops.
LINUX_ARGS=--build run_linux
check test "$(cat "$T/linux.code")" = 0
check grep -qxF "run --rm -i --name srs-chrome-linux-build -v srs-chrome-linux-build:/build -v $W:/src:ro -v $W/skills/srs-develop/scripts:/scripts:ro ossrs/srs:ubuntu20 bash /scripts/obs-linux-test.sh --container-build" "$T/docker.calls"
check test "$(tr '\0' '\n' < "$T/docker.stdin")" = trunk/a.txt
check grep -qE "^BUILD linux $(git -C "$W" rev-parse --short HEAD) [0-9]+s$" "$T/linux.out"
check test ! -f "$T/lane.calls"
LINUX_ARGS=--build run_linux FAKE_DOCKER_BUILD=1
check test "$(cat "$T/linux.code")" = 1
check grep -q '^FAIL Linux build of ' "$T/linux.out"
# Default: the build, then the four cases at once on 16000-16300, each with
# SRS in a container of the build volume and its logs under the Linux folder.
run_linux
check test "$(cat "$T/linux.code")" = 0
check grep -q -- '--container-build' "$T/docker.calls"
check lane_slots 16000
check test "$(grep -c 'unit' "$T/lane.calls")" = 0
check grep -qx "env codec srs-chrome-linux-build $T/srs-chrome-linux ossrs/srs:ubuntu20" "$T/lane.calls"
check grep -qE '^LANE chrome passed=4 skipped=0 known=1 failed=0 ' "$T/linux.out"
run_linux SRS_TEST_PORT_BASE=17000 FAKE_CASE_FAIL=http
check test "$(cat "$T/linux.code")" = 1
check lane_slots 17000
check grep -qE '^LANE chrome passed=3 skipped=0 known=1 failed=1 ' "$T/linux.out"

# chrome-windows-test.sh in a fake host checkout: the fake runner builds
# srs.exe, and the fake lane's cases run against it on 15600-15900.
WH=$T/winhost
mkdir -p "$WH/trunk/objs/windows" "$WH/skills/srs-develop/scripts"
cp "$L"/* "$WH/skills/srs-develop/scripts/"
cp "$SCRIPT_DIR/chrome-windows-test.sh" "$WH/skills/srs-develop/scripts/"
for name in webrtc loss http codec; do
  printf '%s\n' "echo \"env $name \${CHROME_SRS_BIN:-none} \${CHROME_TEST_ROOT:-none}\" >> /tmp/srs-chrome-lib-regression/lane.calls" \
    >> "$WH/skills/srs-develop/scripts/chrome-$name-test.sh"
done
printf '%s\n' '#!/bin/bash' 'echo "runner $*" >> /tmp/srs-chrome-lib-regression/lane.calls' 'echo "runner output"' \
  '[[ -z "${FAKE_BUILD_FAIL:-}" ]] || exit 1' 'touch trunk/objs/windows/srs.exe; chmod +x trunk/objs/windows/srs.exe' \
  > "$WH/skills/srs-develop/scripts/srs-windows-runner.sh"
# The Mac side hands the --host step to windows-host-run.sh, which exits 2
# after its own SKIP line.
printf '%s\n' '#!/bin/bash' 'echo "host-run $*" >> /tmp/srs-chrome-lib-regression/lane.calls' \
  'echo "SKIP windows: cannot ssh to fake"; exit ${FAKE_HOST_RUN_EXIT:-2}' > "$WH/skills/srs-develop/scripts/windows-host-run.sh"
git -C "$WH" init -q
git -C "$WH" -c user.name=t -c user.email=t@t commit -q --allow-empty -m one
run_windows() {
  rm -f "$T/lane.calls" "$WH/trunk/objs/windows/srs.exe"
  local status=0
  (cd "$WH" && env SRS_TEST_PORT_BASE= SRS_TEST_CHROME= SRS_TEST_WINDOWS_HOST=fake CHROME_SKILL_DIR=$T/skill-ok \
    FAKE_BUILD_FAIL= FAKE_CASE_FAIL= "$@" bash skills/srs-develop/scripts/chrome-windows-test.sh ${WINDOWS_ARGS:-}) \
    > "$T/windows.out" 2>&1 || status=$?
  echo "$status" > "$T/windows.code"
}
windows_out() { echo "$(cat "$T/windows.out") $(cat "$T/windows.code")"; }
run_windows SRS_TEST_CHROME=0
check test "$(windows_out)" = 'SKIP SRS_TEST_CHROME=0 0'
run_windows SRS_TEST_WINDOWS_HOST=
check test "$(windows_out)" = 'SKIP windows: SRS_TEST_WINDOWS_HOST is not set 0'
WINDOWS_ARGS=--wip run_windows SRS_TEST_WINDOWS_HOST=
check test "$(windows_out)" = 'SKIP windows: SRS_TEST_WINDOWS_HOST is not set 0'
for args in --bogus --build '--wip --wip' '--host extra' '--host-cases extra'; do
  WINDOWS_ARGS=$args run_windows
  check test "$(cat "$T/windows.code")" = 3
done
run_windows
check test "$(windows_out)" = 'SKIP windows: cannot ssh to fake 0'
check grep -qx "host-run $WH $WH/skills/srs-develop/scripts/chrome-windows-test.sh --host" "$T/lane.calls"
WINDOWS_ARGS=--wip run_windows FAKE_HOST_RUN_EXIT=1
check test "$(cat "$T/windows.code")" = 1
check grep -qx "host-run --wip $WH $WH/skills/srs-develop/scripts/chrome-windows-test.sh --host" "$T/lane.calls"
# On the host: the runner's build, then the four cases at once against
# srs.exe, with their logs under the Windows folder.
WINDOWS_ARGS=--host run_windows
check test "$(cat "$T/windows.code")" = 0
check test "$(head -n 1 "$T/lane.calls")" = 'runner build'
check grep -qE "^BUILD windows $(git -C "$WH" rev-parse HEAD) [0-9]+s$" "$T/windows.out"
check lane_slots 15600
check test "$(grep -c 'unit' "$T/lane.calls")" = 0
check grep -qx "env codec objs/windows/srs.exe $T/srs-chrome-windows" "$T/lane.calls"
check grep -qE '^LANE chrome passed=4 skipped=0 known=1 failed=0 ' "$T/windows.out"
WINDOWS_ARGS=--host run_windows FAKE_BUILD_FAIL=1
check test "$(cat "$T/windows.code")" = 1
check grep -q '^runner output$' "$T/windows.out"
check grep -q '^FAIL srs.exe build of ' "$T/windows.out"
check test "$(grep -c '^start ' "$T/lane.calls")" = 0
# --host-cases runs the cases against the srs.exe already built.
WINDOWS_ARGS=--host-cases run_windows
check test "$(windows_out)" = 'FAIL build trunk/objs/windows/srs.exe first 1'
WINDOWS_ARGS=--host run_windows FAKE_CASE_FAIL=http
check test "$(cat "$T/windows.code")" = 1
check grep -qE '^LANE chrome passed=3 skipped=0 known=1 failed=1 ' "$T/windows.out"

# The chrome-test job of the Test workflow: the unit tests first, then the
# Linux build and cases with the runner's Chrome, the logs kept on failure.
# A renamed script or log folder breaks it only after a push, so the job is
# checked against the checkout here.
REPO=$(cd "$SCRIPT_DIR/../../.." && pwd)
WORKFLOW=$REPO/.github/workflows/test.yml
check test -f "$WORKFLOW"
awk '/^  chrome:$/ { found = 1; print; next } found && /^  [a-z0-9-]+:$/ { exit } found' "$WORKFLOW" > "$T/ci-job.yml"
check grep -qx '    name: chrome-test' "$T/ci-job.yml"
check grep -qx '    runs-on: ubuntu-22.04' "$T/ci-job.yml"
# Without CHROME_PATH a runner with no Chrome would only SKIP; with it set
# to a missing path find-browser fails the job.
check grep -qx '      CHROME_PATH: /usr/bin/google-chrome' "$T/ci-job.yml"
# The driver needs Node 22.12 or later, newer than some runners' default.
check grep -qx "          node-version: '22'" "$T/ci-job.yml"
# Every action is pinned to a commit.
check test "$(grep -c 'uses: ' "$T/ci-job.yml")" -ge 3
check test "$(grep 'uses: ' "$T/ci-job.yml" | grep -vcE 'uses: [a-z/-]+@[0-9a-f]{40} # v[0-9.]+$')" = 0
# The run lines in order, and the scripts they name exist.
grep -E '^ +run: ' "$T/ci-job.yml" | sed 's/^ *run: //' > "$T/ci-runs.txt"
printf '%s\n' 'sudo apt-get update && sudo apt-get install -y ffmpeg' 'npm ci --ignore-scripts' 'npm test' \
  'bash skills/srs-develop/scripts/chrome-test-lib-test.sh' 'bash skills/srs-develop/scripts/chrome-linux-test.sh --build' \
  'bash skills/srs-develop/scripts/chrome-linux-test.sh' > "$T/ci-want.txt"
check diff "$T/ci-want.txt" "$T/ci-runs.txt"
check test "$(grep -c 'working-directory: skills/internal-chrome-control$' "$T/ci-job.yml")" = 2
check test -f "$REPO/skills/internal-chrome-control/package-lock.json"
for script in chrome-test-lib-test.sh chrome-linux-test.sh; do
  check test -f "$REPO/skills/srs-develop/scripts/$script"
done
# On failure it uploads the folder chrome-linux-test.sh logs to.
check grep -qx '        if: failure()' "$T/ci-job.yml"
check grep -qx "          path: $(sed -n 's/^LOG_DIR=\${CHROME_TEST_ROOT:-\(.*\)}\(.*\)$/\1\2/p' "$REPO/skills/srs-develop/scripts/chrome-linux-test.sh")" "$T/ci-job.yml"
# test-done waits for it like the other jobs.
check test "$(awk '/^  test-done:$/ { found = 1 } found && /^ +- chrome$/ { print }' "$WORKFLOW" | wc -l | tr -d ' ')" = 1

rm -rf /tmp/srs-chrome-lib-regression
echo "PASS $checks harness regressions"
