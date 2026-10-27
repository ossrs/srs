#!/bin/bash
#
# Measure the line coverage of the C++ server by the unit tests, on Linux in Docker with the flags of the CI coverage
# job (trunk/Dockerfile.cov), so the numbers match Codecov. The build lives in a Docker volume named after this
# checkout, so later runs only rebuild what changed and the checkout's own objs is not touched.
#
# Usage:
#   bash skills/srs-develop/scripts/srs-utest-coverage.sh [--report] [--lines] [file ...]
#
#   --report    Do not build or run the tests; report from the last run.
#   --lines     Also print the source text of each missed line of the given files.
#   file        Report these files, such as src/core/srs_core_time.cpp, with their missed lines. Default: the
#               summary of every file under trunk/src, except utest.
#
# Every run writes trunk/objs/coverage/summary.txt (one line per file: percent, hit, lines, missed, file) and
# trunk/objs/coverage/missed.txt (every missed line of every file as file:line:source).
#
# A line counts as missed when no test ran it. Lines between LCOV_EXCL_START and LCOV_EXCL_STOP, or with
# LCOV_EXCL_LINE, are not counted, as Codecov does. A header's lines are merged over every object that includes it.
# Code run only in a forked child, such as srs_process_spawn after fork, always shows as missed: the child ends in
# execv or _exit, so it never writes its coverage data.
#
# Remove the build with: docker volume rm <checkout>-coverage-build
#
# A failed run says why: a failed configure or build prints its errors and keeps the make log in
# trunk/objs/coverage/make.log; failed tests print their [  FAILED  ] lines, or the end of the test log when srs_utest
# fails with no failed test, such as a crash or a sanitizer report.
#
# Exit code is 0 when the build and every test pass, 1 otherwise.

FLAGS="--rtsp=on --srt=on --gb28181=on --h265=on --utest=on --gcov=on --sanitizer=on"
IMAGE=ossrs/srs:ubuntu20-cache

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
VOLUME=$(basename "$ROOT")-coverage-build
OUT=$ROOT/trunk/objs/coverage
mkdir -p "$OUT"

RUN=yes
LINES=no
FILES=
for arg in "$@"; do
    case "$arg" in
        --report) RUN=no ;;
        --lines) LINES=yes ;;
        *) FILES="$FILES ${arg#trunk/}" ;;
    esac
done

if [[ $RUN == yes ]]; then
    echo "volume $VOLUME, flags $FLAGS"
    rm -f "$OUT/utest.log" "$OUT/make.log"
    docker run --rm -v "$ROOT/trunk:/host:ro" -v "$VOLUME:/srs" -v "$OUT:/out" -e FLAGS="$FLAGS" "$IMAGE" bash -c '
        # Exit 2 when the tests could not run, so the host tells a build failure from a test failure.
        mkdir -p /srs/trunk && tar -C /host --exclude=./objs --exclude=./Makefile -cf - . | tar -C /srs/trunk -xf - || { echo "sync of trunk into the volume failed"; exit 2; }
        cd /srs/trunk || exit 2
        HEADER=$(grep SRS_USER_CONFIGURE objs/srs_auto_headers.hpp 2>/dev/null | sed "s/.*\"\(.*\)\"/\1/")
        if [[ ! -f Makefile || "$HEADER" != "$FLAGS" || configure -nt objs/Makefile || auto/module_files.sh -nt objs/Makefile ]]; then
            echo "configure: $FLAGS"
            ./configure $FLAGS >/out/make.log 2>&1 || { tail -n 30 /out/make.log; echo "configure failed"; exit 2; }
            find objs -path "*/utest/*.o" -delete
        fi
        echo "build: make utest"
        if ! make utest -j$(nproc) >/out/make.log 2>&1; then
            ERRORS=$(grep -E "error:|Error [0-9]|undefined reference|ld returned" /out/make.log | head -n 30)
            [[ -n "$ERRORS" ]] && echo "$ERRORS" || tail -n 30 /out/make.log
            echo "build failed"
            exit 2
        fi
        rm -f /out/make.log
        find objs -name "*.gcda" -delete
        echo "test: srs_utest"
        ./objs/srs_utest >/out/utest.log 2>&1
        CODE=$?
        grep -E "^\[  (PASSED|FAILED)  \]" /out/utest.log
        if [[ $CODE -ne 0 ]] && ! grep -qE "^\[  FAILED  \]" /out/utest.log; then
            echo "srs_utest exited with $CODE and no failed test; end of the test log:"
            tail -n 30 /out/utest.log
        fi
        rm -rf objs/cov && mkdir -p objs/cov
        for gcda in objs/src/*/*.gcda; do
            dir=$(dirname $gcda); name=$(basename $gcda .gcda)
            src=$(ls src/${dir#objs/src/}/$name.cpp 2>/dev/null) || continue
            gcov -l -p -o $dir $src >/dev/null 2>&1
        done
        mv *.gcov objs/cov/ 2>/dev/null
        [[ $CODE -eq 0 ]] || exit 1
    '
    CODE=$?
    if [[ $CODE -eq 2 ]]; then
        [[ -f "$OUT/make.log" ]] && echo "make log trunk/objs/coverage/make.log"
        exit 1
    elif [[ $CODE -ge 125 ]]; then
        echo "docker run failed with $CODE"
        exit 1
    elif [[ $CODE -ne 0 ]]; then
        echo "tests failed, test log trunk/objs/coverage/utest.log"
        CODE=1
    fi
else
    CODE=0
fi

# Merge the gcov files of every object into one line table per source file under src/, except utest.
docker run --rm -v "$VOLUME:/srs:ro" "$IMAGE" bash -c '
    cd /srs/trunk/objs/cov 2>/dev/null || exit 1
    awk -F: "
        FNR == 1 { src = \"\"; excl = 0 }
        \$2 + 0 == 0 && \$3 == \"Source\" { src = \$4; if (src !~ /^src\\// || src ~ /^src\\/utest\\//) src = \"\"; next }
        src == \"\" { next }
        {
            count = \$1; gsub(/ /, \"\", count); line = \$2 + 0
            text = \$0; sub(/^[^:]*:[^:]*:/, \"\", text)
            if (text ~ /LCOV_EXCL_START/) excl = 1
            skip = excl || text ~ /LCOV_EXCL_LINE/
            if (text ~ /LCOV_EXCL_STOP/) excl = 0
            if (skip || count == \"-\") next
            key = src SUBSEP line
            if (!(key in state)) { state[key] = 0; source[key] = text; files[src] = 1 }
            if (count != \"#####\" && count != \"=====\") state[key] = 1
        }
        END {
            for (key in state) {
                split(key, k, SUBSEP)
                total[k[1]]++
                if (state[key]) hit[k[1]]++
                else print \"M:\" k[1] \":\" k[2] \":\" source[key]
            }
            for (f in files) printf \"S:%.2f %d %d %d %s\n\", 100 * hit[f] / total[f], hit[f], total[f], total[f] - hit[f], f
        }
    " *.gcov
' >"$OUT/merged.txt" || { echo "no coverage data in volume $VOLUME; run without --report first"; exit 1; }

grep '^S:' "$OUT/merged.txt" | cut -c3- | sort -k5 >"$OUT/summary.txt"
grep '^M:' "$OUT/merged.txt" | cut -c3- | sort -t: -k1,1 -k2,2n >"$OUT/missed.txt"
rm -f "$OUT/merged.txt"

awk '{ hit += $2; total += $3 } END { printf "total: %.2f%% (%d of %d lines, %d missed, %d files)\n", 100 * hit / total, hit, total, total - hit, NR }' "$OUT/summary.txt"

if [[ -z "$FILES" ]]; then
    echo "summary: trunk/objs/coverage/summary.txt, missed lines: trunk/objs/coverage/missed.txt"
    exit $CODE
fi

for f in $FILES; do
    LINE=$(awk -v f="$f" '$5 == f' "$OUT/summary.txt")
    if [[ -z "$LINE" ]]; then
        echo "$f: no coverage data (not compiled, or no executable line)"
        continue
    fi
    echo "$LINE" | awk '{ printf "%s: %s%% (%d of %d lines, %d missed)\n", $5, $1, $2, $3, $4 }'
    # Missed lines as ranges of consecutive missed lines, such as 1233-1246.
    awk -F: -v f="$f" '$1 == f { print $2 }' "$OUT/missed.txt" | awk '
        NR == 1 { start = $1; prev = $1; next }
        $1 != prev + 1 { out = out (out ? ", " : "") (start == prev ? start : start "-" prev); start = $1 }
        { prev = $1 }
        END { if (NR) { out = out (out ? ", " : "") (start == prev ? start : start "-" prev); print "  missed: " out } }
    '
    if [[ $LINES == yes ]]; then
        awk -F: -v f="$f" '$1 == f { line = $2; sub(/^[^:]*:[^:]*:/, ""); printf "  %6d: %s\n", line, $0 }' "$OUT/missed.txt"
    fi
done
exit $CODE
