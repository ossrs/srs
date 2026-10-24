#!/bin/bash
# The test of perf-build-check.sh: its builds and image match the profiling
# image, then a real check (or the build-check.json given) records all three
# builds consistently, and no build-check image or container is left.
#
#   perf-build-check-test.sh [build-check.json]
set -uo pipefail

cd "$(dirname "$0")/../../.."
failed=0
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; failed=1; }
check() { if eval "$2"; then pass "$1"; else fail "$1"; fi; }

script=skills/internal-perf-for-srs/scripts/perf-build-check.sh
dockerfile=cmd/perf/Dockerfile.build
check "$script parses" "bash -n $script"
for b in '"gperf|--gperf=on --gcp=on --gmp=on|"' '"gprof|--gprof=on|"' '"valgrind|--valgrind=on|valgrind"'; do
    check "$script builds $b" "grep -qF -- '$b' $script"
done
# The same SRS configure as the profiling image, plus the build's flags.
for text in "FROM srs-perf:local" "./configure --srt=on --rtsp=on --sanitizer=off --utest=off" \
    '--extra-flags="-g -fno-omit-frame-pointer -O2" $FLAGS'; do
    check "$dockerfile: $text" "grep -qF -- '$text' $dockerfile"
done
check "Dockerfile has the same configure" \
    "grep -qF -- '--extra-flags=\"-g -fno-omit-frame-pointer -O2\"' cmd/perf/Dockerfile"
check "make perf-build-check" "grep -qF 'bash skills/internal-perf-for-srs/scripts/perf-build-check.sh' Makefile"
check "$script removes its images" "grep -qF 'docker rmi \"\$image\"' $script"

json="${1:-}"
if [[ -z "$json" ]]; then
    output="$(bash "$script")" || fail "perf-build-check.sh exits 0"
    json="$(sed -n 's/^Build check output //p' <<< "$output")"
fi
check "build-check.json exists" "[[ -f '$json' ]]"
if [[ -f "$json" ]]; then
    check "three builds" "[[ \$(jq -r '[.builds[].name] | join(\",\")' '$json') == gperf,gprof,valgrind ]]"
    for name in gperf gprof valgrind; do
        b=".builds[] | select(.name == \"$name\")"
        check "$name statuses" "jq -e '$b | (.image | IN(\"pass\",\"fail\")) and
            (.configure | IN(\"pass\",\"fail\",\"skipped\")) and (.build | IN(\"pass\",\"fail\",\"skipped\")) and
            (.start | IN(\"pass\",\"fail\",\"skipped\"))' '$json' >/dev/null"
        # A step runs only after the one before it passed.
        check "$name order" "jq -e '$b | ((.image == \"pass\") == (.configure != \"skipped\")) and
            ((.configure == \"pass\") == (.build != \"skipped\")) and
            ((.build == \"pass\") == (.start != \"skipped\"))' '$json' >/dev/null"
        # A failure carries its log's tail; a start has a version.
        check "$name error" "jq -e '$b | ([.image, .configure, .build, .start] | index(\"fail\")) == null or (.error | length > 0)' '$json' >/dev/null"
        check "$name version" "jq -e '$b | .start != \"pass\" or (.version | test(\"^[0-9]+[.][0-9]+[.][0-9]+\"))' '$json' >/dev/null"
        check "$name logs" "[[ -f '$(dirname "$json")/$name/docker-build.log' ]]"
    done
    check "gperf took effect" "jq -e '.builds[] | select(.name == \"gperf\") | .configure != \"pass\" or
        ([\"SRS_GPERF\",\"SRS_GPERF_CP\",\"SRS_GPERF_MP\"] - .macros == [])' '$json' >/dev/null"
    check "gprof took effect" "jq -e '.builds[] | select(.name == \"gprof\") | .configure != \"pass\" or (.macros | index(\"-pg\"))' '$json' >/dev/null"
    # configure always defines the *_BOOL macros, so they show nothing.
    check "no _BOOL macros" "jq -e '[.builds[].macros[] | select(endswith(\"_BOOL\"))] == []' '$json' >/dev/null"
    check "only gperf has gperf macros" "jq -e '[.builds[] | select(.name != \"gperf\") | .macros[] | select(startswith(\"SRS_GPERF\"))] == []' '$json' >/dev/null"
    check "valgrind took effect" "jq -e '.builds[] | select(.name == \"valgrind\") | .configure != \"pass\" or (.macros | index(\"SRS_VALGRIND\"))' '$json' >/dev/null"
fi
check "no srs-perf-build image left" "[[ -z \$(docker images -q 'srs-perf-build') ]]"
check "no srs-perf-build container left" "! docker ps -a --format '{{.Names}}' | grep -q '^srs-perf-build-'"

if [[ "$failed" == 0 ]]; then echo "perf-build-check-test passed"; else echo "perf-build-check-test failed"; exit 1; fi
