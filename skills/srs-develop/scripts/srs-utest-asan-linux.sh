#!/bin/bash
#
# Build and run the C++ unit tests on Linux in Docker, with AddressSanitizer and LeakSanitizer on, so leaks are
# reported too: LeakSanitizer does not run on macOS, and the server's __asan_default_options turns it off, which
# ASAN_OPTIONS here overrides. Nothing is written to this checkout except the report, and no image is left behind.
#
# Usage:
#   bash skills/srs-develop/scripts/srs-utest-asan-linux.sh [--disabled] [--each] [gtest-filter] [report-file]
#
#   --disabled      Also run DISABLED_ tests, such as a proof of a known leak that is not fixed yet.
#   --each          Run every matching test in its own process, after one build. Use it for proofs: a
#                   use-after-free or overflow report ends the process, so in one process it hides the tests after it.
#   gtest-filter    Run only these tests, such as 'ForwarderTest.*'. Default: all tests.
#   report-file     Where to write the full output. Default: trunk/objs/asan-linux-utest.log.
#
# The report keeps the gtest summary and every sanitizer report. Exit code is 0 when every test passes and no
# sanitizer report was printed, 1 otherwise. With --each, each test's output starts with a "=== TEST <name>" line
# and ends with a "=== EXIT <name> <code>" line, and a table of every test's result is printed at the end.

FLAGS="--srt=on --gb28181=on --rtsp=on --utest=on --sanitizer=on --ffmpeg-opus=off --simulator=on --build-cache=on"
IMAGE=ossrs/srs:ubuntu20-cache

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
EXTRA=
EACH=
while [[ $1 == --* ]]; do
    case $1 in
    --disabled) EXTRA=--gtest_also_run_disabled_tests ;;
    --each) EACH=1 ;;
    *) echo "unknown option $1"; exit 1 ;;
    esac
    shift
done
FILTER=${1:-*}
REPORT=${2:-$ROOT/trunk/objs/asan-linux-utest.log}
mkdir -p "$(dirname "$REPORT")"

echo "image $IMAGE, filter '$FILTER', each '${EACH:-no}', report $REPORT"
tar -C "$ROOT" --exclude=trunk/objs --exclude='trunk/3rdparty/srs-bench' -cf - trunk |
    docker run --rm -i -e FILTER="$FILTER" -e EXTRA="$EXTRA" -e EACH="$EACH" -e FLAGS="$FLAGS" "$IMAGE" bash -c '
        mkdir -p /srs && tar -C /srs -xf - && cd /srs/trunk || exit 1
        ./configure $FLAGS >/tmp/configure.log 2>&1 || { tail -n 30 /tmp/configure.log; exit 1; }
        make utest -j$(nproc) >/tmp/make.log 2>&1 || { tail -n 50 /tmp/make.log; exit 1; }
        export ASAN_OPTIONS=detect_leaks=1:halt_on_error=0:alloc_dealloc_mismatch=0:detect_container_overflow=0
        if [[ -z $EACH ]]; then
            ./objs/srs_utest --gtest_filter="$FILTER" $EXTRA 2>&1
            exit $?
        fi
        ./objs/srs_utest --gtest_filter="$FILTER" $EXTRA --gtest_list_tests |
            awk "/^[^ ]/ {suite=\$1; next} /^  / {print suite \$1}" >/tmp/tests.txt
        if [[ ! -s /tmp/tests.txt ]]; then echo "no test matches the filter"; exit 1; fi
        CODE=0
        while read -r NAME; do
            echo "=== TEST $NAME"
            ./objs/srs_utest --gtest_filter="$NAME" $EXTRA 2>&1 </dev/null
            RET=$?
            echo "=== EXIT $NAME $RET"
            [[ $RET -ne 0 ]] && CODE=1
        done </tmp/tests.txt
        exit $CODE
    ' >"$REPORT" 2>&1
CODE=$?

# The report can hold raw bytes, such as a crashed test's garbage frame name, which macOS awk stops at in a UTF-8
# locale, so read it byte by byte.
export LC_ALL=C

if [[ -n $EACH ]]; then
    # One line per test: the exit code, whether gtest saw it pass, and the first sanitizer summary in its output.
    awk '
        /^=== TEST / { name = $3; result = "no-result"; san = "" ; next }
        /^\[       OK \]/ { result = "passed" }
        /^\[  FAILED  \] / && result != "failed" && $0 !~ /listed below|tests?, listed/ { result = "failed" }
        /^SUMMARY: (AddressSanitizer|LeakSanitizer)/ && san == "" { san = $0; sub(/^SUMMARY: /, "", san) }
        /^=== EXIT / { printf "exit %-3s %-9s %s %s\n", $4, result, name, san }
    ' "$REPORT"
else
    grep -E '^\[  (PASSED|FAILED)  \]|tests ran' "$REPORT" | tail -n 5
fi
SANITIZER=$(grep -cE '^==[0-9]+==ERROR: (AddressSanitizer|LeakSanitizer)' "$REPORT")
echo "sanitizer reports: $SANITIZER"
grep -E '^SUMMARY: (AddressSanitizer|LeakSanitizer)' "$REPORT" | sort | uniq -c | head -n 20

if [[ $CODE -ne 0 || $SANITIZER -ne 0 ]]; then
    exit 1
fi
exit 0
