#!/bin/bash
#
# Run the Full tier of references/integration-tests.md on this checkout: Step 1 builds alone, then lanes A-F in
# parallel, under the SRS global lock. Every log is in /tmp/srs-full-tier; the summary is one row per layer.
#
# Usage:
#   bash skills/srs-develop/scripts/srs-full-tier.sh
#
# Exit code is 0 only if every layer passes.

FLAGS="--srt=on --gb28181=on --rtsp=on --utest=on --sanitizer=on --ffmpeg-opus=off --simulator=on"
LOGS=/tmp/srs-full-tier

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
SCRIPTS="$ROOT/skills/srs-develop/scripts"
cd "$ROOT" || exit 1

for cmd in go ffmpeg ffprobe curl jq node; do
    if ! command -v $cmd >/dev/null 2>&1; then
        echo "no $cmd in PATH"
        exit 1
    fi
done

mkdir -p $LOGS
rm -f $LOGS/*.log $LOGS/*.result

echo "lock: waiting, $(bash $SCRIPTS/full-tier-lock.sh status)"
LOCK_START=$(date +%s)
TOKEN=$(bash $SCRIPTS/full-tier-lock.sh acquire --wait 600 --pid $$)
if [[ -z "$TOKEN" ]]; then
    echo "lock: not acquired"
    exit 1
fi
echo "lock: acquired after $(( $(date +%s) - LOCK_START ))s"
trap 'bash $SCRIPTS/full-tier-lock.sh release "$TOKEN"' EXIT

# Run one layer: name, then the command. Writes <name>.log and <name>.result with the exit code and duration.
layer() {
    local name=$1; shift
    local start=$(date +%s)
    "$@" >$LOGS/$name.log 2>&1
    local code=$?
    echo "$code $(( $(date +%s) - start ))" >$LOGS/$name.result
    return $code
}

# Step 1: build, alone.
build() {
    # The utest objects do not depend on the configure header, so remove them after a configure.
    if ! grep -q "\"$FLAGS\"" trunk/objs/srs_auto_headers.hpp 2>/dev/null || [[ trunk/configure -nt trunk/objs/Makefile ]]; then
        (cd trunk && ./configure $FLAGS && find objs -path '*/utest/*.o' -delete) || return 1
    fi
    (cd trunk && make -j10 server) || return 1
    make || return 1
    (cd trunk/3rdparty/srs-bench && make test) || return 1
}
echo "step 1: build"
if ! layer build build; then
    echo "build failed, log $LOGS/build.log"
    tail -n 30 $LOGS/build.log
    exit 1
fi

PROXY_E2E="proxy-e2e-test.sh proxy-e2e-cluster-test.sh proxy-e2e-edge-test.sh proxy-e2e-redis-test.sh
    proxy-e2e-transmux-test.sh proxy-e2e-srt-test.sh proxy-e2e-whip-test.sh proxy-e2e-bearer-auth-test.sh"
FEATURE_SH="gb28181-api-timeout-test.sh gb28181-tcp-disconnect-test.sh srs-reload-test.sh install-target-test.sh
    rtc-rtx-whip-publish-test.sh rtc-rtx-whep-play-test.sh rtc-plain-whep-play-test.sh rtc-plain-whip-publish-test.sh"
if [[ -f $SCRIPTS/forward-status-test.sh ]]; then
    FEATURE_SH="$FEATURE_SH forward-status-test.sh"
fi

BB="./objs/srs_blackbox_test -test.v -srs-ffmpeg $(command -v ffmpeg) -srs-ffprobe $(command -v ffprobe)"

lane_a() {
    layer proxy-utest bash $SCRIPTS/proxy-utest.sh --coverage
    for s in $PROXY_E2E; do layer ${s%.sh} bash $SCRIPTS/$s & done
    wait
}
lane_b() { (cd trunk/3rdparty/srs-bench && layer blackbox-fast $BB -test.run '^TestFast' -test.parallel 64); }
lane_c() { (cd trunk/3rdparty/srs-bench && layer blackbox-slow $BB -test.run '^TestSlow' -test.parallel 3); }
lane_d() {
    local base=${SRS_TEST_PORT_BASE:-13000}
    mkdir -p /tmp/srs-regression
    (cd trunk && env SRS_RTMP_LISTEN=$base SRS_HTTP_API_LISTEN=$((base + 1)) SRS_HTTP_SERVER_LISTEN=$((base + 2)) \
        SRS_RTC_SERVER_LISTEN=$((base + 3)) SRS_RTSP_SERVER_LISTEN=$((base + 4)) SRS_STREAM_CASTER_LISTEN=$((base + 5)) \
        SRS_STREAM_CASTER_OUTPUT="rtmp://127.0.0.1:$base/live/[stream]" \
        SRS_PID=/tmp/srs-regression/srs.pid SRS_SRS_LOG_FILE=/tmp/srs-regression/srs.log \
        ./objs/srs -c conf/regression-test.conf >$LOGS/regression-srs.log 2>&1 &)
    for i in 1 2 3 4 5 6 7 8 9 10; do
        curl -fs http://127.0.0.1:$((base + 1))/api/v1/versions >/dev/null && break
        sleep 1
    done
    local args="-test.v -srs-rtmp-port $base -srs-api-port $((base + 1)) -srs-rtsp-port $((base + 4)) -srs-http-server 127.0.0.1:$((base + 2))"
    (cd trunk/3rdparty/srs-bench && layer regression-dtls-passive ./objs/srs_test $args -test.run '^TestRtcDTLS_ClientPassive') &
    (cd trunk/3rdparty/srs-bench && layer regression-dtls-active ./objs/srs_test $args -test.run '^TestRtcDTLS_ClientActive') &
    (cd trunk/3rdparty/srs-bench && layer regression-rtx ./objs/srs_test $args -test.run '^TestRtx') &
    (cd trunk/3rdparty/srs-bench && layer regression-other ./objs/srs_test $args -test.skip '^(TestRtcDTLS_Client(Passive|Active)|TestRtx)') &
    wait
    kill $(cat /tmp/srs-regression/srs.pid) 2>/dev/null
}
lane_e() {
    layer srs-config-file bash $SCRIPTS/srs-config-file-test.sh &
    layer browser-page-url node $SCRIPTS/browser-page-url-test.js &
    for s in $FEATURE_SH; do SRS_GB_SKIP_BUILD=1 layer ${s%.sh} bash $SCRIPTS/$s & done
    wait
}
lane_f() { (cd trunk && layer utest-build make -j10 utest && layer utest ./objs/srs_utest); }

echo "step 2: lanes A-F"
START=$(date +%s)
lane_a & PA=$!
lane_b & PB=$!
lane_c & PC=$!
lane_d & PD=$!
lane_e & PE=$!
lane_f & PF=$!
wait $PA $PB $PC $PD $PE $PF

# Summary: exit code, pass and fail counts, duration. Go tests count --- PASS/FAIL, gtest counts [ OK ]/[ FAILED ].
FAILED=0
printf '%-32s %5s %6s %6s %6s\n' layer exit pass fail secs
for r in $LOGS/*.result; do
    name=$(basename $r .result)
    read code secs <$r
    clean=$(sed 's/\x1b\[[0-9;]*m//g' $LOGS/$name.log)
    pass=$(echo "$clean" | grep -cE -e '--- PASS|^\[       OK \]')
    fail=$(echo "$clean" | grep -cE -e '--- FAIL|^\[  FAILED  \] [A-Za-z]')
    printf '%-32s %5s %6s %6s %6s\n' $name $code $pass $fail $secs
    if [[ $code != 0 ]]; then FAILED=1; fi
done
echo "lanes: $(( $(date +%s) - START ))s, logs in $LOGS"
exit $FAILED
