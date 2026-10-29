#!/bin/bash
# Sourced Chrome test harness, compatible with Bash 3.2. Call init, start, the
# driver runs and assertions, then finish. init installs owned-resource
# EXIT/signal cleanup.
#
# Slot offsets: RTMP +0, HTTP +1, API +2, RTC UDP +3, the driver's page
# server +9. A case that needs more configs runs up to four SRS: the second
# takes +4..+7, the third +10..+13, the fourth +14..+17. The WebRTC candidate
# is the host's own IPv4 (CHROME_EIP), never 127.0.0.1 while another address
# exists: with a loopback and a LAN pair SRS switches its send address between
# them (finding F1).
#
# Settings:
# - SRS_TEST_CHROME=0 skips the case.
# - CHROME_SRS_HOST: the caller already runs SRS on the slot at that address,
#   so start never spawns or kills SRS. CHROME_SRS_BIN names the SRS binary
#   under trunk (default objs/srs).
# - CHROME_SRS_DOCKER: a Docker volume with a Linux build in /build/srs/trunk.
#   Each SRS then runs in its own container of CHROME_SRS_IMAGE (default
#   ossrs/srs:ubuntu20), named srs-chrome-<case>-<srs>-<slot>, with its TCP
#   ports on 127.0.0.1 and its RTC port on the candidate address. CHROME_DOCKER
#   names the docker command.
# - CHROME_TEST_EIP overrides the candidate address.
# - CHROME_NODE and CHROME_NPM name the node and npm commands; CHROME_PATH
#   picks the browser (see the internal-chrome-control skill).

# before(d) and since(d) split the samples at the wall-clock time d (epoch
# ms, the samples' at), such as a drop; series(f) lists f over such a part.
# settled(s) keeps the samples of s from a second after the first decoded
# frame: under load, such as on the Windows test host, a player can freeze
# once for about 0.3 s in its first second of play, with no packet lost.
CHROME_JQ_DEFS='def latest(f): [.samples[]? | f | select(. != null)] | last;
def settled(s): [s[] | select((.inboundVideo.framesDecoded // 0) > 0)] as $p | [$p[] | select(.t >= $p[0].t + 1000)];
def earliest(f): [.samples[]? | f | select(. != null)] | first;
def rise(f): (latest(f) // 0) - (earliest(f) // 0);
def before(d): [.samples[]? | select(.at != null and .at < d)];
def since(d): [.samples[]? | select(.at != null and .at >= d)];
def series(f): [.[] | f | select(. != null)];'

chrome_test_ports() {
  CHROME_PORT_BASE=$1
  CHROME_PAGE_PORT=$(($1 + 9))
  chrome_test_srs 0
}

# chrome_test_srs <0-3> picks the SRS the next calls use: its ports, its URLs,
# and its log and pid file, srs.log or srs-<n>.log in the case's directory.
# The third and fourth skip the page port: +10..+13 and +14..+17.
chrome_test_srs() {
  local base
  case "$1" in
    0|1) base=$((CHROME_PORT_BASE + 4 * $1));;
    2|3) base=$((CHROME_PORT_BASE + 2 + 4 * $1));;
    *) chrome_test_fail "invalid SRS index $1"; return 1;;
  esac
  CHROME_SRS_NAME=srs
  [[ $1 -eq 0 ]] || CHROME_SRS_NAME=srs-$1
  CHROME_RTMP_PORT=$base
  CHROME_HTTP_PORT=$((base + 1))
  CHROME_API_PORT=$((base + 2))
  CHROME_RTC_PORT=$((base + 3))
  CHROME_API_URL="http://${CHROME_SRS_HOST:-127.0.0.1}:$CHROME_API_PORT"
  CHROME_HTTP_URL="http://${CHROME_SRS_HOST:-127.0.0.1}:$CHROME_HTTP_PORT"
  CHROME_RTMP_URL="rtmp://${CHROME_SRS_HOST:-127.0.0.1}:$CHROME_RTMP_PORT"
}

chrome_test_fail() { echo "FAIL $*" >&2; return 1; }
chrome_test_pass() { CHROME_PASS_COUNT=$((CHROME_PASS_COUNT + 1)); echo "PASS $*"; }
chrome_test_known() { CHROME_KNOWN_COUNT=$((CHROME_KNOWN_COUNT + 1)); echo "KNOWN $*"; }
chrome_test_skip() { echo "SKIP $*"; exit 0; }
# A part the browser cannot run, such as a codec it lacks: listed as
# unverified, and the case goes on.
chrome_test_skipped() { CHROME_SKIP_COUNT=$((CHROME_SKIP_COUNT + 1)); echo "SKIP $*"; }
# No usable Node or browser: the case is listed as unverified, not failed.
chrome_test_unavailable() { chrome_test_skip "$CHROME_CASE: unverified: Chrome not available ($1)"; }

# With a loopback candidate the play checks show F1, not the stream's health.
chrome_test_loopback_known() {
  [[ -n "${CHROME_LOOPBACK:-}" ]] || return 1
  chrome_test_known F1 "$1 (loopback candidate)"
}

chrome_test_budget() {
  local elapsed=$((SECONDS - CHROME_STARTED))
  [[ $elapsed -le 60 ]] || chrome_test_fail "budget ${elapsed}s > 60s"
}

# Bound each external command by both its own timeout and the remaining budget.
chrome_test_command() {
  chrome_test_spawn "$@"
  chrome_test_reap
}

# chrome_test_spawn <limit> <command...> starts the command in the background;
# chrome_test_reap waits for it, killing it past the limit or the budget.
# Only the shell that spawned it can reap it, not a subshell.
chrome_test_spawn() {
  CHROME_COMMAND_DEADLINE=$((SECONDS + $1))
  shift
  "$@" &
  CHROME_COMMAND_PID=$!
}

chrome_test_reap() {
  local status=0
  while kill -0 "$CHROME_COMMAND_PID" 2>/dev/null; do
    if [[ $SECONDS -ge $CHROME_COMMAND_DEADLINE || $((SECONDS - CHROME_STARTED)) -ge 60 ]]; then
      kill "$CHROME_COMMAND_PID" 2>/dev/null || true
      sleep 0.1
      kill -KILL "$CHROME_COMMAND_PID" 2>/dev/null || true
      wait "$CHROME_COMMAND_PID" 2>/dev/null || true
      CHROME_COMMAND_PID=
      chrome_test_fail 'command timed out'
      return 1
    fi
    sleep 0.1
  done
  wait "$CHROME_COMMAND_PID" || status=$?
  CHROME_COMMAND_PID=
  return "$status"
}

# Node 22.12 or later, as the skill's package.json needs.
chrome_test_node_ok() {
  [[ "$1" =~ ^v([0-9]+)\.([0-9]+)\. ]] || return 1
  local major=${BASH_REMATCH[1]} minor=${BASH_REMATCH[2]}
  [[ $major -gt 22 || ( $major -eq 22 && $minor -ge 12 ) ]]
}

chrome_test_npm_install() (cd "$1" && exec "${CHROME_NPM:-npm}" ci --ignore-scripts --no-audit --no-fund)

# Install the skill's pinned packages when node_modules is missing or older
# than the lockfile. Parallel cases share one lock; a lock whose owner is gone
# is taken over.
chrome_test_npm_ci() {
  local dir=$1 lock=${CHROME_NPM_LOCK:-/tmp/srs-chrome-npm-ci.lock} owner status=0
  local deadline=$((SECONDS + ${CHROME_NPM_WAIT:-40}))
  until mkdir "$lock" 2>/dev/null; do
    owner=$(cat "$lock/pid" 2>/dev/null || true)
    if [[ -n "$owner" ]] && ! kill -0 "$owner" 2>/dev/null; then
      rm -f "$lock/pid"
      rmdir "$lock" 2>/dev/null || true
      continue
    fi
    [[ $SECONDS -lt $deadline ]] || { chrome_test_fail "npm ci lock held by pid ${owner:-unknown} ($lock)"; return 1; }
    sleep 0.1
  done
  echo "$$" > "$lock/pid"
  if [[ ! -d "$dir/node_modules" || "$dir/package-lock.json" -nt "$dir/node_modules" ]]; then
    if ! command -v "${CHROME_NPM:-npm}" >/dev/null; then
      status=2
    elif chrome_test_command 40 chrome_test_npm_install "$dir" >"$CHROME_TEST_DIR/npm-ci.log" 2>&1; then
      touch "$dir/node_modules"
    else
      status=1
      chrome_test_fail "npm ci in $dir (see $CHROME_TEST_DIR/npm-ci.log)"
    fi
  fi
  rm -f "$lock/pid"
  rmdir "$lock"
  [[ $status -ne 2 ]] || chrome_test_unavailable 'no npm'
  return "$status"
}

# The host's own IPv4 on the default route, or nothing.
chrome_test_host_ip() {
  local iface addr=
  case "$(uname)" in
    Darwin)
      iface=$(route -n get default 2>/dev/null | sed -n 's/^ *interface: *//p')
      [[ -z "$iface" ]] || addr=$(ipconfig getifaddr "$iface" 2>/dev/null || true)
      ;;
    Linux)
      addr=$(ip -4 route get 1.1.1.1 2>/dev/null | sed -n 's/.* src \([0-9.]*\).*/\1/p')
      ;;
    MINGW*|MSYS*)
      # The interface column of the active default route with the lowest metric.
      addr=$(route print -4 0.0.0.0 2>/dev/null | tr -d '\r' | awk '/^Active Routes:/ { on = 1; next }
        on && /^=/ { exit }
        on && $1 == "0.0.0.0" && $2 == "0.0.0.0" && (best == "" || $5 < metric) { best = $4; metric = $5 }
        END { print best }')
      ;;
  esac
  [[ "$addr" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ && "$addr" != 127.* ]] || return 1
  echo "$addr"
}

# CHROME_EIP is the candidate; with no address but loopback, CHROME_LOOPBACK
# makes the driver allow loopback candidates.
chrome_test_eip() {
  CHROME_LOOPBACK=
  CHROME_EIP=${CHROME_TEST_EIP:-}
  [[ -n "$CHROME_EIP" ]] || CHROME_EIP=$(chrome_test_host_ip) || CHROME_EIP=
  [[ -n "$CHROME_EIP" ]] || CHROME_EIP=127.0.0.1
  [[ "$CHROME_EIP" != 127.* ]] || CHROME_LOOPBACK=1
}

chrome_test_cleanup() {
  local pid attempt owned name status=0
  if [[ -n "${CHROME_COMMAND_PID:-}" ]]; then
    kill "$CHROME_COMMAND_PID" 2>/dev/null || true
    wait "$CHROME_COMMAND_PID" 2>/dev/null || true
    CHROME_COMMAND_PID=
  fi
  if [[ -n "${CHROME_WATCH_PID:-}" ]]; then
    kill "$CHROME_WATCH_PID" 2>/dev/null || true
    wait "$CHROME_WATCH_PID" 2>/dev/null || true
    CHROME_WATCH_PID=
  fi
  chrome_test_api_record_stop
  for pid in ${CHROME_FFMPEG_PIDS:-}; do
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
  done
  CHROME_FFMPEG_PIDS=
  for owned in ${CHROME_SRS_OWNED:-}; do
    name=${owned%%:*} pid=${owned##*:}
    [[ -f "$CHROME_TEST_DIR/$name.pid" ]] || continue
    # Only the process spawned by this shell, confirmed by its pid file.
    if [[ "$(cat "$CHROME_TEST_DIR/$name.pid")" != "$pid" ]]; then
      chrome_test_fail "SRS pid file $name.pid no longer matches owned process"
      status=1
      continue
    fi
    kill "$pid" 2>/dev/null || true
    for attempt in 1 2 3 4 5 6 7 8 9 10; do
      kill -0 "$pid" 2>/dev/null || break
      sleep 0.1
    done
    kill -KILL "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
    # The docker client forwards the stop to SRS; a container it left behind
    # is removed by its name.
    [[ -z "${CHROME_SRS_DOCKER:-}" ]] || "${CHROME_DOCKER:-docker}" rm -f "$(chrome_test_container "$name")" >/dev/null 2>&1 || true
    rm -f "$CHROME_TEST_DIR/$name.pid"
  done
  CHROME_SRS_OWNED= CHROME_SRS_PID=
  return "$status"
}

chrome_test_exit() {
  local status=$?
  trap - EXIT
  chrome_test_cleanup || status=1
  chrome_test_budget || status=1
  [[ $status -eq 0 ]] || echo "FAIL $CHROME_CASE (logs: $CHROME_TEST_DIR)" >&2
  exit "$status"
}

chrome_test_init() {
  [[ "${SRS_TEST_CHROME:-1}" != 0 ]] || chrome_test_skip SRS_TEST_CHROME=0
  CHROME_STARTED=$SECONDS
  CHROME_CASE=$1
  local base=${SRS_TEST_PORT_BASE:-$2} dep version script_dir status=0
  case "$CHROME_CASE" in ''|*[!a-zA-Z0-9_-]*) chrome_test_fail 'invalid case name'; return 1;; esac
  case "$base" in ''|*[!0-9]*) chrome_test_fail 'invalid port base'; return 1;; esac
  [[ $base -ge 11000 && $((base + 99)) -lt 32768 ]] || { chrome_test_fail 'port slot outside test range'; return 1; }
  script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
  CHROME_WORKSPACE=$(cd "$script_dir/../../.." && pwd)
  CHROME_SKILL_DIR=${CHROME_SKILL_DIR:-$CHROME_WORKSPACE/skills/internal-chrome-control}
  CHROME_JS="$CHROME_SKILL_DIR/scripts/chrome.js"
  CHROME_TEST_DIR="${CHROME_TEST_ROOT:-/tmp}/srs-chrome-$CHROME_CASE"
  chrome_test_ports "$base"
  CHROME_PASS_COUNT=0 CHROME_KNOWN_COUNT=0 CHROME_SKIP_COUNT=0
  CHROME_SRS_PID= CHROME_SRS_OWNED= CHROME_FFMPEG_PIDS= CHROME_COMMAND_PID= CHROME_WATCH_PID= CHROME_RECORD_PID=
  CHROME_RUN_NAME=
  for dep in jq curl; do
    command -v "$dep" >/dev/null || { chrome_test_fail "missing dependency $dep"; return 1; }
  done
  command -v "${CHROME_NODE:-node}" >/dev/null || chrome_test_unavailable 'no node'
  version=$("${CHROME_NODE:-node}" --version 2>/dev/null) || version=
  chrome_test_node_ok "$version" || chrome_test_unavailable "node ${version:-unknown}, needs 22.12"
  mkdir -p "$CHROME_TEST_DIR/html"
  # Reject another live owner; never terminate a listener to acquire a slot.
  if [[ -z "${CHROME_SRS_HOST:-}" && -f "$CHROME_TEST_DIR/srs.pid" ]] && kill -0 "$(cat "$CHROME_TEST_DIR/srs.pid")" 2>/dev/null; then
    chrome_test_fail 'case already running'; return 1
  fi
  chrome_test_npm_ci "$CHROME_SKILL_DIR" || return 1
  CHROME_BROWSER=$("${CHROME_NODE:-node}" "$CHROME_JS" find-browser 2>"$CHROME_TEST_DIR/find-browser.log") || status=$?
  case $status in
    0) ;;
    2) chrome_test_unavailable 'no browser';;
    *) chrome_test_fail "find-browser exit $status: $(cat "$CHROME_TEST_DIR/find-browser.log")"; return 1;;
  esac
  [[ -n "${CHROME_SRS_HOST:-}${CHROME_SRS_DOCKER:-}" || -x "$CHROME_WORKSPACE/trunk/${CHROME_SRS_BIN:-objs/srs}" ]] \
    || { chrome_test_fail "build trunk/${CHROME_SRS_BIN:-objs/srs} first"; return 1; }
  trap chrome_test_exit EXIT
  trap 'exit 130' INT
  trap 'exit 143' TERM
  chrome_test_eip
  echo "BROWSER $CHROME_BROWSER, candidate $CHROME_EIP${CHROME_LOOPBACK:+ (loopback)}"
}

# Extra NAME=value arguments override the settings, such as
# SRS_VHOST_RTC_NACK_PREFER_RTX=on.
chrome_test_start_srs() {
  local setting docker
  set -- SRS_RTMP_LISTEN="$CHROME_RTMP_PORT" SRS_HTTP_SERVER_ENABLED=on SRS_HTTP_SERVER_LISTEN="$CHROME_HTTP_PORT" \
    SRS_HTTP_SERVER_DIR="$CHROME_TEST_DIR/html" SRS_HTTP_API_ENABLED=on SRS_HTTP_API_LISTEN="$CHROME_API_PORT" \
    SRS_RTC_SERVER_ENABLED=on SRS_RTC_SERVER_LISTEN="$CHROME_RTC_PORT" SRS_RTC_SERVER_CANDIDATE="$CHROME_EIP" \
    SRS_VHOST_HTTP_REMUX_ENABLED=on SRS_VHOST_HLS_ENABLED=on SRS_VHOST_HLS_HLS_FRAGMENT=1 \
    SRS_VHOST_HLS_HLS_WINDOW=4 SRS_VHOST_HLS_HLS_PATH="$CHROME_TEST_DIR/html" \
    SRS_VHOST_RTC_ENABLED=on SRS_VHOST_RTC_RTMP_TO_RTC=on SRS_VHOST_RTC_RTC_TO_RTMP=on SRS_LOG_TANK=console "$@"
  mkdir -p "$CHROME_TEST_DIR/html"
  if [[ -n "${CHROME_SRS_DOCKER:-}" ]]; then
    # The case's directory is mounted at the same path, so the settings are
    # the same as for a local SRS.
    docker=(run --rm --name "$(chrome_test_container)" -p "127.0.0.1:$CHROME_RTMP_PORT:$CHROME_RTMP_PORT"
      -p "127.0.0.1:$CHROME_HTTP_PORT:$CHROME_HTTP_PORT" -p "127.0.0.1:$CHROME_API_PORT:$CHROME_API_PORT"
      -p "$CHROME_EIP:$CHROME_RTC_PORT:$CHROME_RTC_PORT/udp" -v "$CHROME_SRS_DOCKER:/build:ro"
      -v "$CHROME_TEST_DIR:$CHROME_TEST_DIR" -w /build/srs/trunk)
    for setting in "$@"; do docker+=(-e "$setting"); done
    "${CHROME_DOCKER:-docker}" "${docker[@]}" "${CHROME_SRS_IMAGE:-ossrs/srs:ubuntu20}" "./${CHROME_SRS_BIN:-objs/srs}" -e \
      </dev/null >"$CHROME_TEST_DIR/$CHROME_SRS_NAME.log" 2>&1 &
  else
    (cd "$CHROME_WORKSPACE/trunk" && exec env "$@" "./${CHROME_SRS_BIN:-objs/srs}" -e) \
      >"$CHROME_TEST_DIR/$CHROME_SRS_NAME.log" 2>&1 &
  fi
  CHROME_SRS_PID=$!
  printf '%s\n' "$CHROME_SRS_PID" > "$CHROME_TEST_DIR/$CHROME_SRS_NAME.pid"
  CHROME_SRS_OWNED="${CHROME_SRS_OWNED:+$CHROME_SRS_OWNED }$CHROME_SRS_NAME:$CHROME_SRS_PID"
}

# The container of the current SRS, or of the one named by $1, in Docker mode.
chrome_test_container() { echo "srs-chrome-$CHROME_CASE-${1:-$CHROME_SRS_NAME}-$CHROME_PORT_BASE"; }

chrome_test_start() {
  [[ -n "${CHROME_SRS_HOST:-}" ]] || chrome_test_start_srs "$@"
  local deadline=$((SECONDS + 10))
  until curl -fsS --max-time 1 "$CHROME_API_URL/api/v1/versions" >"$CHROME_TEST_DIR/version.json" 2>/dev/null; do
    [[ -n "${CHROME_SRS_HOST:-}" ]] || kill -0 "$CHROME_SRS_PID" 2>/dev/null \
      || { chrome_test_fail "SRS exited during startup (see $CHROME_TEST_DIR/$CHROME_SRS_NAME.log)"; return 1; }
    [[ $SECONDS -lt $deadline ]] || { chrome_test_fail 'SRS API startup timeout'; return 1; }
    sleep 0.1
  done
}

# A new stream name per call: a killed browser's session keeps its stream busy
# for 30 s.
chrome_test_stream() {
  local seq=0 file="$CHROME_TEST_DIR/stream.seq"
  [[ ! -f "$file" ]] || seq=$(cat "$file")
  seq=$((seq + 1))
  echo "$seq" > "$file"
  echo "$CHROME_CASE-$(date +%s)-$$-$seq"
}

chrome_test_whip_url() { echo "$CHROME_API_URL/rtc/v1/whip/?app=live&stream=$1&eip=$CHROME_EIP"; }
chrome_test_whep_url() { echo "$CHROME_API_URL/rtc/v1/whep/?app=live&stream=$1&eip=$CHROME_EIP"; }
chrome_test_flv_url() { echo "$CHROME_HTTP_URL/live/$1.flv"; }
chrome_test_ts_url() { echo "$CHROME_HTTP_URL/live/$1.ts"; }
chrome_test_hls_url() { echo "$CHROME_HTTP_URL/live/$1.m3u8"; }

# Wait until SRS serves the playlist of live/<stream>: hls.js fails at once
# on a missing one. SRS answers 404 until the first segment is written, then
# a playlist that names the media playlist with an hls_ctx session, so this
# never opens a session itself.
chrome_test_wait_hls() {
  local deadline=$((SECONDS + ${CHROME_API_WAIT:-8}))
  until curl -fsS --max-time 1 "$(chrome_test_hls_url "$1")" 2>/dev/null | grep -q '^#EXTM3U'; do
    if [[ $SECONDS -ge $deadline ]]; then
      chrome_test_fail "HLS live/$1.m3u8 not served in ${CHROME_API_WAIT:-8}s"
      return 1
    fi
    chrome_test_budget || return 1
    sleep 0.1
  done
}

# Publish trunk/doc/source.flv (H.264/AAC) in a loop over RTMP; cleanup stops it.
chrome_test_publish_ffmpeg() {
  command -v ffmpeg >/dev/null || { chrome_test_fail 'missing dependency ffmpeg'; return 1; }
  ffmpeg -hide_banner -loglevel error -re -stream_loop -1 -i "$CHROME_WORKSPACE/trunk/doc/source.flv" \
    -c copy -f flv "$CHROME_RTMP_URL/live/$1" </dev/null >"$CHROME_TEST_DIR/ffmpeg-$1.log" 2>&1 &
  CHROME_FFMPEG_PIDS="${CHROME_FFMPEG_PIDS:+$CHROME_FFMPEG_PIDS }$!"
}

# Run the driver: chrome_test_run <name> <chrome.js run options>. It writes
# <name>.log and <name>-stats.json in the case's directory, and gets the rest
# of the budget.
chrome_test_run() {
  chrome_test_run_start "$@" && chrome_test_run_wait
}

# The same run in the background, for a case that acts while the browser
# plays: chrome_test_run_ready waits for a job, chrome_test_run_wait for the end.
chrome_test_run_start() {
  local left
  CHROME_RUN_NAME=$1
  shift
  left=$((60 - (SECONDS - CHROME_STARTED)))
  [[ $left -gt 0 ]] || { chrome_test_budget; return 1; }
  rm -f "$CHROME_TEST_DIR/$CHROME_RUN_NAME-stats.json"
  chrome_test_spawn "$left" "${CHROME_NODE:-node}" "$CHROME_JS" run --page-port "$CHROME_PAGE_PORT" \
    --out "$CHROME_TEST_DIR/$CHROME_RUN_NAME-stats.json" ${CHROME_LOOPBACK:+--allow-loopback} "$@" \
    >"$CHROME_TEST_DIR/$CHROME_RUN_NAME.log" 2>&1
}

# Wait until the running driver says the job is ready.
chrome_test_run_ready() {
  local log="$CHROME_TEST_DIR/$CHROME_RUN_NAME.log"
  until grep -q "^PASS $1 ready " "$log" 2>/dev/null; do
    if ! kill -0 "$CHROME_COMMAND_PID" 2>/dev/null; then
      grep -q "^PASS $1 ready " "$log" 2>/dev/null && return 0
      chrome_test_fail "chrome $CHROME_RUN_NAME: job $1 not ready (see $log)"
      return 1
    fi
    chrome_test_budget || return 1
    sleep 0.1
  done
}

# The SRS session username of a ready WHIP or WHEP job, from the session
# query of its LOCATION line; empty when there is none.
chrome_test_session() {
  sed -n "s/^LOCATION $1 //p" "$CHROME_TEST_DIR/$CHROME_RUN_NAME.log" 2>/dev/null | head -n 1 \
    | sed -n 's/.*[?&]session=\([^&]*\).*/\1/p'
}

chrome_test_run_wait() {
  local name=$CHROME_RUN_NAME status=0
  chrome_test_reap || status=$?
  sed 's/^/  /' "$CHROME_TEST_DIR/$name.log"
  case $status in
    0) chrome_test_pass "chrome $name";;
    2) chrome_test_unavailable "$name";;
    6) chrome_test_fail "chrome $name: SRS did not answer"; return 1;;
    *) chrome_test_fail "chrome $name (exit $status, see $CHROME_TEST_DIR/$name.log)"; return 1;;
  esac
}

# chrome_test_job_assert <run name> <job> <jq filter> <description>: the filter
# runs on the job in the run's stats file, with latest(f), earliest(f) and
# rise(f) over its samples.
chrome_test_job_assert() {
  local name=$1 job=$2 filter=$3 desc=$4
  if jq -e --arg job "$job" "$CHROME_JQ_DEFS first(.jobs[] | select(.name == \$job)) | $filter" \
    "$CHROME_TEST_DIR/$name-stats.json" >/dev/null 2>&1; then
    chrome_test_pass "$desc"
  else
    chrome_test_fail "$desc (job $job in $name-stats.json)"
  fi
}

# chrome_test_startup_freeze <run name> <job> <label>: an INFO line when the
# player froze before its settled samples, which the freeze checks skip.
chrome_test_startup_freeze() {
  local n
  n=$(jq --arg job "$2" "$CHROME_JQ_DEFS first(.jobs[] | select(.name == \$job))
    | (settled([.samples[]?]) | series(.inboundVideo.freezeCount) | first // 0)
    - ([.samples[]?] | series(.inboundVideo.freezeCount) | first // 0)" "$CHROME_TEST_DIR/$1-stats.json" 2>/dev/null) || n=0
  [[ -z "$n" || "$n" == 0 ]] || echo "INFO $3 froze $n times in its first second of play"
}

# chrome_test_freeze_assert <run name> <job> <filter> <desc> <SRS log name>:
# chrome_test_job_assert for a freeze check, which is KNOWN F5 instead of a
# failure when the player's SRS logged that its own loop stalled, such as
# srs.exe on the Windows test host; the player then gets no packet for that
# long and freezes, with no loss or NACK.
chrome_test_freeze_assert() {
  local name=$1 job=$2 filter=$3 desc=$4 log="$CHROME_TEST_DIR/$5.log" stalls n
  if jq -e --arg job "$job" "$CHROME_JQ_DEFS first(.jobs[] | select(.name == \$job)) | $filter" \
    "$CHROME_TEST_DIR/$name-stats.json" >/dev/null 2>&1; then
    chrome_test_pass "$desc"
    return
  fi
  stalls=$(grep -F '[LARGE_TIMER]' "$log" 2>/dev/null) || stalls=
  [[ -n "$stalls" ]] || { chrome_test_fail "$desc (job $job in $name-stats.json)"; return; }
  n=$(printf '%s\n' "$stalls" | grep -c .)
  stalls=$(printf '%s\n' "$stalls" | head -n 1)
  chrome_test_known F5 "$desc: SRS stalled $n times, first $(printf '%s\n' "$stalls" \
    | sed 's/.*\[LARGE_TIMER\] //; s/, count=.*//') at $(printf '%s\n' "$stalls" \
    | grep -o '[0-9][0-9]:[0-9][0-9]:[0-9][0-9]\.[0-9]*' | head -n 1) ($5.log)"
}

# pass, fail or skip; empty when the job is not in the stats file.
chrome_test_job_result() {
  jq -r --arg job "$2" 'first(.jobs[] | select(.name == $job)) | .result // empty' \
    "$CHROME_TEST_DIR/$1-stats.json" 2>/dev/null || true
}

chrome_test_read_api() { curl -fsS --max-time 1 "$CHROME_API_URL$1"; }

# Poll /api/v1/streams/ until the jq filter holds, up to CHROME_API_WAIT
# seconds (default 8); the last answer is kept in streams.json.
chrome_test_poll_api() {
  local filter=$1 data deadline=$((SECONDS + ${CHROME_API_WAIT:-8}))
  while :; do
    data=$(chrome_test_read_api /api/v1/streams/ 2>/dev/null) || data='{}'
    echo "$data" > "$CHROME_TEST_DIR/streams.json"
    echo "$data" | jq -e "$filter" >/dev/null 2>&1 && return 0
    [[ $SECONDS -lt $deadline ]] || return 1
    chrome_test_budget || return 1
    sleep 0.1
  done
}

chrome_test_api_assert() {
  if chrome_test_poll_api "$1"; then
    chrome_test_pass "$2"
  else
    chrome_test_fail "$2 (see $CHROME_TEST_DIR/streams.json)"
  fi
}

# The browser's publisher is gone once the driver returns, so its API checks
# poll in the background during the run; chrome_test_api_watched reports them.
chrome_test_api_watch() {
  chrome_test_poll_api "$1" >/dev/null 2>&1 &
  CHROME_WATCH_PID=$!
}

chrome_test_api_watched() {
  local status=0
  wait "$CHROME_WATCH_PID" || status=$?
  CHROME_WATCH_PID=
  if [[ $status -eq 0 ]]; then
    chrome_test_pass "$1"
  else
    chrome_test_fail "$1 (see $CHROME_TEST_DIR/streams.json)"
  fi
}

# Record every /api/v1/streams/ answer, one compact JSON per line, in
# streams-record.json while the browser runs, for publishers that each live
# only part of the run; chrome_test_api_seen checks a filter held in any of
# them, and chrome_test_api_record_stop (or cleanup) ends the record.
chrome_test_api_record() {
  local file="$CHROME_TEST_DIR/streams-record.json"
  : > "$file"
  (
    while :; do
      data=$(chrome_test_read_api /api/v1/streams/ 2>/dev/null | jq -c . 2>/dev/null) || data=
      [[ -z "$data" ]] || printf '%s\n' "$data" >> "$file"
      sleep 0.2
    done
  ) &
  CHROME_RECORD_PID=$!
}

chrome_test_api_record_stop() {
  [[ -n "${CHROME_RECORD_PID:-}" ]] || return 0
  kill "$CHROME_RECORD_PID" 2>/dev/null || true
  wait "$CHROME_RECORD_PID" 2>/dev/null || true
  CHROME_RECORD_PID=
}

# A line cut off by the stop is ignored.
chrome_test_api_seen() {
  local file="$CHROME_TEST_DIR/streams-record.json"
  if jq -n -e -R "[inputs | fromjson?] | any(.[]; $1)" "$file" >/dev/null 2>&1; then
    chrome_test_pass "$2"
  else
    chrome_test_fail "$2 (see $file)"
  fi
}

# Wait until SRS has live/<stream> published, before a player opens it.
chrome_test_wait_stream() {
  chrome_test_poll_api "any(.streams[]?; .app == \"live\" and .name == \"$1\" and .publish.active == true)" \
    || chrome_test_fail "stream live/$1 not published in ${CHROME_API_WAIT:-8}s"
}

# The wall clock in epoch ms, the clock of the samples' at.
chrome_test_now_ms() { "${CHROME_NODE:-node}" -e 'console.log(Date.now())'; }

# chrome_test_drop <username> <n>: the NACK simulator drops the next n packets
# of that session: received ones for a publisher, sent ones for a player.
# SRS reads query values without percent-decoding, so the username goes as
# SRS wrote it in the Location, + and / included (finding F4).
chrome_test_drop() {
  local out
  if out=$(curl -fsS --max-time 2 "$CHROME_API_URL/rtc/v1/nack/?username=$1&drop=$2" 2>&1) \
    && echo "$out" | jq -e '.code == 0' >/dev/null 2>&1; then
    echo "DROP $2 packets of session $1"
  else
    chrome_test_fail "simulator drop $2 on $1: $out"
  fi
}

# The drop simulator is a build option (--simulator=on); the loss cases need it.
chrome_test_simulator() {
  chrome_test_read_api /api/v1/features 2>/dev/null | jq -e 'any(..; type == "object" and .simulator == true)' >/dev/null
}

chrome_test_srs_log_count() {
  local n
  n=$(grep -c -F -- "$1" "$CHROME_TEST_DIR/$CHROME_SRS_NAME.log" 2>/dev/null) || true
  echo "${n:-0}"
}

chrome_test_finish() {
  chrome_test_cleanup || return 1
  chrome_test_budget || return 1
  echo "RESULT $CHROME_CASE passed=$CHROME_PASS_COUNT known=$CHROME_KNOWN_COUNT skipped=$CHROME_SKIP_COUNT elapsed=$((SECONDS - CHROME_STARTED))s"
}
