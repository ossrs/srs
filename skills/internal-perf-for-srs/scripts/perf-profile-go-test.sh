#!/bin/bash
# Tests perf-profile-go.sh on a short proxy hold: the proxy and its origins
# from perf-up.sh -target proxy, 200 RTMP players through the proxy in a
# profile hold, and the Go profiles taken from the load container with short
# durations. Every file must exist, the CPU profile must show the proxy's own
# symbols, and the mutex and block rates must be off again afterwards. Needs
# srs-perf:local from make perf.
set -uo pipefail

cd "$(dirname "$0")/../../.."
scripts=skills/internal-perf-for-srs/scripts
failed=0
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; failed=1; }
check() {
    local name="$1"
    shift
    if "$@" >/dev/null 2>&1; then pass "$name"; else fail "$name"; fi
}
no_containers() { test -z "$(docker ps -a --filter name=srs-perf- --format '{{.Names}}')"; }

cleanup() { bash "$scripts/perf-down.sh" >/dev/null 2>&1 || true; }
trap cleanup EXIT

check "no perf container before the test" no_containers

arch="$(docker info --format '{{.Architecture}}')"
case "$arch" in
    aarch64|arm64) arch=arm64 ;;
    x86_64|amd64) arch=amd64 ;;
    *) echo "Unsupported Docker CPU: $arch" >&2; exit 1 ;;
esac
GOOS=linux GOARCH="$arch" go -C cmd/perf build -o ../../cmd/perf/out/srs-perf-linux . || exit 1

if SRS_PERF_SCENARIO=rtmp-rtmp bash "$scripts/perf-up.sh" -target proxy; then
    pass "perf-up -target proxy"
else
    fail "perf-up -target proxy"
    exit 1
fi
docker cp cmd/perf/out/srs-perf-linux srs-perf-load:/usr/local/bin/srs-perf >/dev/null

name="profile-go-test-$(date -u +%Y%m%dT%H%M%SZ)"
host="cmd/perf/out/$name/profile"
mkdir -p "cmd/perf/out/$name"
docker exec srs-perf-load srs-perf run -scenario rtmp-rtmp -target proxy -mode profile \
    -clients 200 -settle 5 -window 25 -output "/out/$name/hold" > "cmd/perf/out/$name/hold-path.txt" &
hold_pid=$!
sleep 8
if docker exec -e SRS_PERF_GO_PROFILE_SECONDS=10 -e SRS_PERF_GO_TRACE_SECONDS=2 \
    srs-perf-load bash /perf-scripts/perf-profile-go.sh "/out/$name/profile"; then
    pass "perf-profile-go.sh on a 200-player proxy hold"
else
    fail "perf-profile-go.sh on a 200-player proxy hold"
fi
wait "$hold_pid"

for file in cpu.pb.gz allocs.pb.gz heap.pb.gz goroutine.txt mutex.pb.gz block.pb.gz trace.out \
    gc.json top-symbols.txt flamegraph.svg; do
    check "$file is written" test -s "$host/$file"
done
check "top-symbols.txt has the proxy's symbols" grep -q 'srsx/internal/proxy' "$host/top-symbols.txt"
check "goroutine.txt is the text profile" grep -q '^goroutine profile: total' "$host/goroutine.txt"
check "flamegraph.svg is an SVG" grep -q '<svg' "$host/flamegraph.svg"
check "gc.json has the counts before" \
    grep -Eq '"before": *\{"num_gc": *[0-9]+, *"pause_total_ns": *[0-9]+\}' "$host/gc.json"
check "gc.json has the counts after" \
    grep -Eq '"after": *\{"num_gc": *[0-9]+, *"pause_total_ns": *[0-9]+\}' "$host/gc.json"
rates="$(docker exec srs-perf-load curl -s http://127.0.0.1:6060/debug/pprof/rates)"
check "mutex and block rates are off after the profiles" test "$rates" = '{"mutex":0,"block":0}'
hold_result="cmd/perf/out$(sed 's|^/out||' "cmd/perf/out/$name/hold-path.txt")"
check "the profile hold ran through the proxy" grep -q '"target": *"proxy"' "$hold_result"

bash "$scripts/perf-down.sh"
check "perf-down leaves no container" no_containers

if [[ "$failed" == 0 ]]; then echo "All checks passed"; else echo "Some checks failed"; fi
exit "$failed"
