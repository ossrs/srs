#!/bin/bash
#
# Regression checks for srs-full-tier.sh: it runs a copy of the runner in a fake checkout, with fake
# layer scripts, binaries and tools on PATH, so nothing is built, no port is bound and the real lock
# and logs are untouched. Checks the step order, the OBS lane G and its builds, the Chrome lane I, the
# SRS_TEST_OBS, SRS_TEST_CHROME and SRS_TEST_WINDOWS_HOST gates, and the summary.
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
    # Only the lane's and the cases' own lines start at the beginning of a line.
    fake $FAKE_SCRIPTS/chrome-lane-test.sh 'sleep 1; printf "%s\n" "PASS unit driver: 86 tests" "PASS WHEP H264/Opus" "  PASS whep ready" "KNOWN F3 FLV of WHIP audio" "SKIP codec h265: unverified: no encoder" "LANE chrome passed=6"'
    fake $FAKE_SCRIPTS/chrome-linux-test.sh '[[ $1 == --build ]] && sleep 1 && echo "BUILD linux abc 1s" || { sleep 1; printf "%s\n" "PASS WHEP H264/Opus" "KNOWN F3 FLV of WHIP video" "LANE chrome passed=4"; }'
    fake $FAKE_SCRIPTS/obs-linux-test.sh '[[ $1 == --build ]] && sleep 1 && echo "BUILD linux abc 1s" || echo "SKIP linux: unverified: Docker not available"'
    # From the Mac it fails; with --host-cases, on the Windows host, it passes.
    fake $FAKE_SCRIPTS/obs-windows-test.sh 'sleep 1; if [[ $1 == --host-cases ]]; then printf "%s\n" "PASS API H264 AAC 640x360" "PASS whip" "KNOWN F2 API frames 0"; else printf "%s\r\n" "BUILD windows abc 1s" "PASS API H264 AAC 640x360" "KNOWN F2 API frames 0"; echo "FAIL http-flv no audio" >&2; status=1; fi'
    # From the Mac it prints the host's carriage returns; with --host-cases, on the Windows host, it passes.
    fake $FAKE_SCRIPTS/chrome-windows-test.sh 'sleep 2; if [[ $1 == --host-cases ]]; then printf "%s\n" "PASS WHEP H264/Opus" "SKIP codec h265: unverified: no encoder" "LANE chrome passed=4"; else printf "%s\r\n" "BUILD windows abc 1s" "PASS WHEP H264/Opus" "KNOWN F3 FLV of WHIP audio" "LANE chrome passed=4"; fi'
    # The Windows runner prints the lines of each step; the step named by STUB_WINDOWS_FAIL fails.
    fake $FAKE_SCRIPTS/srs-windows-runner.sh 'case "$*" in
        build) printf "%s\n" "CHECK openssl PASS: OpenSSL 3" "CHECK st PASS: slept" "START version PASS: 8.0.64" "START stop PASS";;
        "--alone utest") sleep 2; echo "[==========] 3 tests from 2 test suites ran. (5 ms total)"; echo "[  PASSED  ] 3 tests.";;
        "--alone integ") printf "%s\n" "srs-integ main: 3 cases, 2 passed, 0 failed, 1 skipped, 0 known bugs" "srs-integ timer: 2 cases, 2 passed, 0 failed, 0 skipped, 0 known bugs";;
        esac
        if [[ ${@: -1} == "$STUB_WINDOWS_FAIL" ]]; then echo "[  FAILED  ] KernelTest.A"; echo "RESULT windows-x64 $STUB_WINDOWS_FAIL FAIL"; status=1; fi'
    # uname -s gives STUB_UNAME, as Git Bash on Windows does, or this OS's.
    printf '%s\n' '#!/bin/bash' '[[ -n $STUB_UNAME ]] && echo "$STUB_UNAME" && exit 0' 'exec /usr/bin/uname "$@"' >$FAKE_BIN/uname
    chmod +x $FAKE_BIN/uname
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
    env PATH="$FAKE_BIN:$PATH" SRS_FULL_TIER_LOGS=$WORK/logs SRS_TEST_PORT_BASE= SRS_TEST_CHROME= "$@" \
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
check says 'step 2: lanes A-G, I'
check not_called 'srs-windows-runner.sh'
# Lane I runs in Step 2 beside lanes A-G, before lane H; its SKIP and KNOWN lines are unverified.
check before 'end make [-j10 server]' 'start chrome-lane-test.sh ['
check before 'start proxy-utest.sh [' 'end chrome-lane-test.sh ['
check before 'start chrome-lane-test.sh [' 'end proxy-utest.sh ['
check before 'start obs-lane-test.sh [' 'end chrome-lane-test.sh ['
check before 'end chrome-lane-test.sh [' 'start make [integ-unit'
check row '^chrome-lane +0 +2 +0 '
check grep -qx 'unverified chrome-lane: KNOWN F3 FLV of WHIP audio' $OUT
check grep -qx 'unverified chrome-lane: SKIP codec h265: unverified: no encoder' $OUT
check not_called 'chrome-lane-test.sh [--'
# The Chrome Linux build overlaps the local build; its cases run after lane I's local ones.
check before 'start make [-j10 server]' 'end chrome-linux-test.sh [--build]'
check before 'start chrome-linux-test.sh [--build]' 'end make [-j10 server]'
check before 'end chrome-linux-test.sh [--build]' 'start proxy-utest.sh ['
check before 'end chrome-lane-test.sh [' 'start chrome-linux-test.sh []'
check before 'start proxy-utest.sh [' 'end chrome-linux-test.sh []'
check before 'end chrome-linux-test.sh []' 'start make [integ-unit'
check row '^chrome-linux +0 +1 +0 '
check row '^chrome-linux-build +0 +0 +0 '
check grep -qx 'unverified chrome-linux: KNOWN F3 FLV of WHIP video' $OUT
check not_called 'chrome-windows-test.sh'

# With the Windows host: its build and run in lane G, none in Step 1; its failure fails the tier.
run_tier SRS_TEST_OBS=1 SRS_TEST_WINDOWS_HOST=host
check test "$(cat $WORK/code)" = 1
check not_called 'obs-windows-test.sh [--build]'
check before 'end make [-j10 server]' 'start obs-windows-test.sh []'
check before 'start obs-lane-test.sh [' 'end obs-windows-test.sh []'
check before 'end obs-windows-test.sh []' 'start make [integ-unit'
check row '^obs-windows +1 +1 +1 '
check test "$(grep -c '^obs-windows-build ' $OUT)" = 0
# On macOS and Linux there is no lane W, even with the Windows host.
check not_called 'srs-windows-runner.sh'
check test "$(grep -c '^windows-' $OUT)" = 0
# One unverified line per OBS SKIP or KNOWN line, without the carriage return of the Windows host.
check grep -qx 'unverified obs-lane: KNOWN F2 API frames 0' $OUT
check grep -qx 'unverified obs-windows: KNOWN F2 API frames 0' $OUT
check test "$(grep -c '^$' $OUT)" = 0
# Lane I also runs the Chrome cases on the Windows host, beside the local and Linux ones, before lane H.
check before 'end make [-j10 server]' 'start chrome-windows-test.sh []'
check before 'start chrome-lane-test.sh [' 'end chrome-windows-test.sh []'
check before 'start chrome-windows-test.sh []' 'end chrome-lane-test.sh ['
check before 'end chrome-windows-test.sh []' 'start make [integ-unit'
check row '^chrome-windows +0 +1 +0 '
check grep -qx 'unverified chrome-windows: KNOWN F3 FLV of WHIP audio' $OUT
check not_called 'chrome-windows-test.sh [--'

# SRS_TEST_OBS=0: no OBS build or run, the other lanes and lane H as before.
run_tier SRS_TEST_OBS=0 SRS_TEST_WINDOWS_HOST=host
check test "$(cat $WORK/code)" = 0
check not_called 'obs-'
check called 'start proxy-utest.sh ['
check called 'start make [integ'
check says 'lane G: skipped (SRS_TEST_OBS=0)'
check test "$(grep -c '^obs-' $OUT)" = 0
check says 'step 2: lanes A-F, I, lane G: skipped (SRS_TEST_OBS=0)'
check called 'start chrome-lane-test.sh ['
check called 'start chrome-linux-test.sh [--build]'
check called 'start chrome-linux-test.sh []'
check row '^chrome-lane +0 +2 +0 '

# SRS_TEST_CHROME=0: no lane I, the OBS lanes and lane H as before.
run_tier SRS_TEST_OBS= SRS_TEST_WINDOWS_HOST= SRS_TEST_CHROME=0
check test "$(cat $WORK/code)" = 0
check not_called 'chrome-'
check called 'start obs-lane-test.sh ['
check called 'start make [integ'
check says 'step 2: lanes A-G, lane I: skipped (SRS_TEST_CHROME=0)'
check test "$(grep -c '^lane I: skipped (SRS_TEST_CHROME=0)$' $OUT)" = 1
check test "$(grep -c '^chrome-' $OUT)" = 0
check test "$(grep -c '^unverified chrome-' $OUT)" = 0

# Both off: lanes A-F only, even with the Windows host.
run_tier SRS_TEST_OBS=0 SRS_TEST_WINDOWS_HOST=host SRS_TEST_CHROME=0
check test "$(cat $WORK/code)" = 0
check says 'step 2: lanes A-F, lane G: skipped (SRS_TEST_OBS=0), lane I: skipped (SRS_TEST_CHROME=0)'
check not_called 'chrome-'
check not_called 'obs-'

# Native Windows, in Git Bash: lane W only, under the lock. The Windows build first, then its utest beside
# the OBS cases on this host, then the Chrome cases there, then its integ alone; none of the Unix build, lanes or the
# Mac's OBS route.
run_tier SRS_TEST_OBS= SRS_TEST_WINDOWS_HOST=host STUB_UNAME=MINGW64_NT-10.0-26100
check test "$(cat $WORK/code)" = 0
check says 'lock: acquired after'
check not_called 'start configure'
check not_called 'start make ['
check not_called 'proxy-utest.sh'
check not_called 'srs_blackbox_test'
check not_called 'srs_test'
check not_called 'srs_utest'
check not_called 'srs-config-file-test.sh'
check not_called 'obs-lane-test.sh'
check not_called 'obs-linux-test.sh'
check not_called 'obs-windows-test.sh []'
check before 'end srs-windows-runner.sh [build]' 'start srs-windows-runner.sh [--alone utest]'
check before 'end srs-windows-runner.sh [build]' 'start obs-windows-test.sh [--host-cases]'
check before 'start obs-windows-test.sh [--host-cases]' 'end srs-windows-runner.sh [--alone utest]'
check before 'start srs-windows-runner.sh [--alone utest]' 'end obs-windows-test.sh [--host-cases]'
check before 'end srs-windows-runner.sh [--alone utest]' 'start srs-windows-runner.sh [--alone integ]'
check before 'end obs-windows-test.sh [--host-cases]' 'start srs-windows-runner.sh [--alone integ]'
check test "$(grep -c 'start srs-windows-runner.sh' $CALLS)" = 3
# The rows count the build's CHECK and START lines, the gtest summary and the srs-integ summaries.
check row '^windows-build +0 +4 +0 '
check row '^windows-utest +0 +3 +0 '
check row '^windows-integ +0 +4 +0 '
check row '^obs-windows +0 +2 +0 '
check says 'unverified obs-windows: KNOWN F2 API frames 0'
check test "$(grep -cE '^(build|proxy|blackbox|regression|utest|integ|obs-lane|obs-linux) ' $OUT)" = 0
check says 'step 2: lane W utest, lane G'
# Then the Chrome cases on this host alone, as lane I, before the integ; none of the Mac's Chrome lanes.
check says 'step 3: lane I'
check says 'step 4: lane W integ'
check not_called 'chrome-lane-test.sh'
check not_called 'chrome-linux-test.sh'
check not_called 'chrome-windows-test.sh []'
check before 'end srs-windows-runner.sh [--alone utest]' 'start chrome-windows-test.sh [--host-cases]'
check before 'end obs-windows-test.sh [--host-cases]' 'start chrome-windows-test.sh [--host-cases]'
check before 'end chrome-windows-test.sh [--host-cases]' 'start srs-windows-runner.sh [--alone integ]'
check row '^chrome-windows +0 +1 +0 '
check says 'unverified chrome-windows: SKIP codec h265: unverified: no encoder'

# Native Windows with SRS_TEST_OBS=0: no OBS case, lane W as before.
run_tier SRS_TEST_OBS=0 SRS_TEST_WINDOWS_HOST= STUB_UNAME=MSYS_NT-10.0-26100
check test "$(cat $WORK/code)" = 0
check not_called 'obs-'
check called 'start srs-windows-runner.sh [--alone integ]'
check says 'lane G: skipped (SRS_TEST_OBS=0)'
check before 'end srs-windows-runner.sh [--alone utest]' 'start chrome-windows-test.sh [--host-cases]'
check says 'step 3: lane I'

# Native Windows with SRS_TEST_CHROME=0: no Chrome case, lane W as before.
run_tier SRS_TEST_OBS= SRS_TEST_WINDOWS_HOST= STUB_UNAME=MINGW64_NT-10.0-26100 SRS_TEST_CHROME=0
check test "$(cat $WORK/code)" = 0
check not_called 'chrome-'
check called 'start obs-windows-test.sh [--host-cases]'
check called 'start srs-windows-runner.sh [--alone integ]'
check says 'step 3: lane I: skipped (SRS_TEST_CHROME=0)'
check test "$(grep -c '^lane I: skipped (SRS_TEST_CHROME=0)$' $OUT)" = 1

# Native Windows, the build fails: nothing else runs, exit 1.
run_tier SRS_TEST_OBS= SRS_TEST_WINDOWS_HOST= STUB_UNAME=MINGW64_NT-10.0-26100 STUB_WINDOWS_FAIL=build
check test "$(cat $WORK/code)" = 1
check not_called 'srs-windows-runner.sh [--alone'
check not_called 'obs-'
check says 'build failed'
check not_called 'chrome-'

# Native Windows, the utest fails: integ still runs, the row shows the failure, exit 1.
run_tier SRS_TEST_OBS=0 SRS_TEST_WINDOWS_HOST= STUB_UNAME=MINGW64_NT-10.0-26100 STUB_WINDOWS_FAIL=utest
check test "$(cat $WORK/code)" = 1
check called 'start srs-windows-runner.sh [--alone integ]'
check row '^windows-utest +1 +3 +1 '
check row '^windows-integ +0 +4 +0 '

rm -rf $WORK
echo "PASS $checks full tier regressions"
