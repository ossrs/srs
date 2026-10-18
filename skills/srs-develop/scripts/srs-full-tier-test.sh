#!/bin/bash
#
# Regression checks for srs-full-tier.sh: it runs a copy of the runner in a fake checkout, with fake
# layer scripts, binaries and tools on PATH, so nothing is built, no port is bound and the real lock
# and logs are untouched. Checks the step order, the OBS lane G and its builds, the SRS_TEST_OBS and
# SRS_TEST_WINDOWS_HOST gates, and the summary.
#
# Usage:
#   bash skills/srs-develop/scripts/srs-full-tier-test.sh

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
WORK=/tmp/srs-full-tier-regression
FAKE_ROOT=$WORK/root
FAKE_SCRIPTS=$FAKE_ROOT/skills/srs-develop/scripts
FAKE_BIN=$WORK/bin
CALLS=$WORK/calls
OUT=$WORK/out
FLAGS="--srt=on --gb28181=on --rtsp=on --utest=on --sanitizer=on --ffmpeg-opus=off --simulator=on"

checks=0
check() {
    checks=$((checks + 1))
    if "$@"; then echo "PASS regression $checks"; else echo "FAIL regression $checks: $*"; tail -n 40 $OUT; exit 1; fi
}

# A fake that logs its name and arguments, and its start and end when it sleeps.
fake() {
    local path=$1 body=${2:-}
    mkdir -p "$(dirname "$path")"
    printf '%s\n' '#!/bin/bash' "echo \"start \$(basename \$0) [\$*]\" >>$CALLS" "$body" \
        "echo \"end \$(basename \$0) [\$*]\" >>$CALLS" 'exit ${status:-0}' >"$path"
    chmod +x "$path"
}

setup() {
    rm -rf $WORK
    mkdir -p $FAKE_SCRIPTS $FAKE_BIN $FAKE_ROOT/trunk/objs $FAKE_ROOT/trunk/conf $FAKE_ROOT/trunk/3rdparty/srs-bench/objs
    cp $SCRIPT_DIR/srs-full-tier.sh $FAKE_SCRIPTS/
    printf '%s\n' '#!/bin/bash' 'case $1 in status) echo free;; acquire) echo token;; esac' >$FAKE_SCRIPTS/full-tier-lock.sh
    local s
    for s in proxy-e2e-test proxy-e2e-cluster-test proxy-e2e-edge-test proxy-e2e-redis-test proxy-e2e-transmux-test \
        proxy-e2e-srt-test proxy-e2e-whip-test proxy-e2e-bearer-auth-test srs-config-file-test gb28181-api-timeout-test \
        gb28181-tcp-disconnect-test srs-reload-test install-target-test rtc-rtx-whip-publish-test \
        rtc-rtx-whep-play-test rtc-plain-whep-play-test rtc-plain-whip-publish-test; do
        fake $FAKE_SCRIPTS/$s.sh
    done
    touch $FAKE_SCRIPTS/browser-page-url-test.js
    # Lane A's first layer overlaps lane G.
    fake $FAKE_SCRIPTS/proxy-utest.sh 'sleep 2'
    fake $FAKE_SCRIPTS/obs-lane-test.sh 'sleep 1; printf "%s\n" "PASS API H264 AAC 640x360" "PASS http-flv video audio" "KNOWN F2 API frames 0" "LANE obs passed=3"'
    fake $FAKE_SCRIPTS/obs-linux-test.sh '[[ $1 == --build ]] && sleep 1 && echo "BUILD linux abc 1s" || echo "SKIP linux: unverified: Docker not available"'
    fake $FAKE_SCRIPTS/obs-windows-test.sh '[[ $1 == --build ]] && sleep 1 && echo "BUILD wsl abc 1s" || { printf "%s\r\n" "PASS API H264 AAC 640x360" "KNOWN F2 API frames 0"; echo "FAIL http-flv no audio" >&2; status=1; }'
    # The Mac build sleeps, so the OBS builds start while it runs.
    fake $FAKE_BIN/make '[[ " $* " == *" server "* ]] && sleep 1; true'
    for s in go ffmpeg ffprobe jq node curl; do fake $FAKE_BIN/$s; done
    fake $FAKE_ROOT/trunk/configure
    printf '%s\n' "#define SRS_CONFIGURE \"$FLAGS\"" >$FAKE_ROOT/trunk/objs/srs_auto_headers.hpp
    touch $FAKE_ROOT/trunk/objs/Makefile
    fake $FAKE_ROOT/trunk/objs/srs 'echo $$ >$SRS_PID'
    fake $FAKE_ROOT/trunk/objs/srs_utest
    fake $FAKE_ROOT/trunk/3rdparty/srs-bench/objs/srs_blackbox_test
    fake $FAKE_ROOT/trunk/3rdparty/srs-bench/objs/srs_test
}

run_tier() {
    setup
    env PATH="$FAKE_BIN:$PATH" SRS_FULL_TIER_LOGS=$WORK/logs SRS_TEST_PORT_BASE= "$@" \
        bash $FAKE_SCRIPTS/srs-full-tier.sh >$OUT 2>&1
    echo $? >$WORK/code
}

# The line number of the first call that starts with the text.
at() { grep -n -m 1 -F -- "$1" $CALLS | cut -d: -f1; }
before() { local a b; a=$(at "$1"); b=$(at "$2"); [[ -n $a && -n $b && $a -lt $b ]]; }
called() { grep -q -F -- "$1" $CALLS; }
not_called() { ! called "$1"; }
row() { grep -qE "$1" $OUT; }
says() { grep -q -F -- "$1" $OUT; }

# Default: lane G and the Linux build run, the Windows ones do not.
run_tier SRS_TEST_OBS= SRS_TEST_WINDOWS_HOST=
check test "$(cat $WORK/code)" = 0
# The Linux build overlaps the local build, lane G overlaps lane A, and lane H comes after both.
check before 'start make [-j10 server]' 'end obs-linux-test.sh [--build]'
check before 'start obs-linux-test.sh [--build]' 'end make [-j10 server]'
check before 'end obs-linux-test.sh [--build]' 'start proxy-utest.sh ['
check before 'end make [-j10 server]' 'start proxy-utest.sh ['
check before 'start proxy-utest.sh [' 'end obs-lane-test.sh ['
check before 'start obs-lane-test.sh [' 'end proxy-utest.sh ['
check before 'start obs-lane-test.sh [' 'start obs-linux-test.sh []'
check before 'end obs-lane-test.sh [' 'start make [integ-unit'
check before 'end obs-linux-test.sh []' 'start make [integ-unit'
check not_called 'obs-windows-test.sh'
check row '^obs-lane +0 +2 +0 '
check row '^obs-linux +0 +0 +0 '
check row '^obs-linux-build +0 +0 +0 '
check says 'unverified obs-lane: KNOWN F2 API frames 0'
check says 'unverified obs-linux: SKIP linux: unverified: Docker not available'
check says 'step 2: lanes A-G'

# With the Windows host: its build in Step 1, its run in lane G; its failure fails the tier.
run_tier SRS_TEST_OBS=1 SRS_TEST_WINDOWS_HOST=host
check test "$(cat $WORK/code)" = 1
check before 'start obs-windows-test.sh [--build]' 'end make [-j10 server]'
check before 'start make [-j10 server]' 'end obs-windows-test.sh [--build]'
check before 'end make [-j10 server]' 'start obs-windows-test.sh []'
check before 'end obs-windows-test.sh []' 'start make [integ-unit'
check row '^obs-windows +1 +1 +1 '
check row '^obs-windows-build +0 +0 +0 '
# One unverified line per OBS SKIP or KNOWN line, without the carriage return of the Windows host.
check grep -qx 'unverified obs-lane: KNOWN F2 API frames 0' $OUT
check grep -qx 'unverified obs-windows: KNOWN F2 API frames 0' $OUT
check test "$(grep -c '^$' $OUT)" = 0

# SRS_TEST_OBS=0: no OBS build or run, the other lanes and lane H as before.
run_tier SRS_TEST_OBS=0 SRS_TEST_WINDOWS_HOST=host
check test "$(cat $WORK/code)" = 0
check not_called 'obs-'
check called 'start proxy-utest.sh ['
check called 'start make [integ'
check says 'lane G: skipped (SRS_TEST_OBS=0)'
check test "$(grep -c '^obs-' $OUT)" = 0

rm -rf $WORK
echo "PASS $checks full tier regressions"
