#!/bin/bash
# Checks the load side admits the client cap: one hold at the cap with no
# publisher, so players connect and wait and SRS spends little CPU on them, and
# counts the players SRS has accepted each second. It fails when fewer than the
# cap connect, or a dial fails on a load-side limit such as ephemeral ports or
# file limits; max_connections shows as connections SRS closes. Player health is
# not checked: with no media every player fails the frame rule.
set -eo pipefail

cd "$(dirname "$0")/../../.."
lock="$PWD/skills/srs-develop/scripts/full-tier-lock.sh"
scenario=rtmp-rtmp
clients="${1:-40000}"
seconds=20
[[ "$clients" =~ ^[1-9][0-9]*$ ]] || { echo "Usage: $0 [clients]" >&2; exit 2; }

out="$PWD/cmd/perf/out/cap-check"
mkdir -p "$out"
arch="$(docker info --format '{{.Architecture}}')"
case "$arch" in
    aarch64|arm64) arch=arm64 ;;
    x86_64|amd64) arch=amd64 ;;
    *) echo "Unsupported Docker CPU: $arch" >&2; exit 1 ;;
esac
GOOS=linux GOARCH="$arch" go -C cmd/perf build -o ../../cmd/perf/out/srs-perf-linux .

token=$(bash "$lock" acquire --wait 3600 --pid $$)
containers_started=0
cleanup() {
    if [[ "$containers_started" == 1 ]]; then make perf-down >/dev/null; fi
    bash "$lock" release "$token" >/dev/null
}
trap cleanup EXIT

SRS_PERF_SCENARIO="$scenario" SRS_PERF_OUT="$out" make perf-up
containers_started=1
docker cp cmd/perf/out/srs-perf-linux srs-perf-load:/usr/local/bin/srs-perf

docker exec srs-perf-load srs-perf run -scenario "$scenario" -mode hold \
    -clients "$clients" -seconds "$seconds" -no-publisher -output /out \
    > "$out/path.txt" 2> "$out/stderr.txt" &
run_pid=$!
peak=0
: > "$out/connections.txt"
while kill -0 "$run_pid" 2>/dev/null; do
    # Established sockets on the SRS side of port 1935 (hex 078F, state 01).
    now=$(docker exec srs-perf-load awk '$2 ~ /:078F$/ && $4 == "01"' /proc/net/tcp | wc -l | tr -d ' ')
    echo "$(date +%s) $now" >> "$out/connections.txt"
    if [[ "$now" -gt "$peak" ]]; then peak=$now; fi
    sleep 1
done
wait "$run_pid"
bash "$lock" refresh "$token" >/dev/null

result="$out$(sed 's|^/out||' "$out/path.txt")"
jq '{clients: .steps[0].clients,
    srs_cpu_max: ([.steps[0].samples[].srs_cpu_percent] | max),
    srs_rss_max: ([.steps[0].samples[].srs_rss_bytes] | max),
    worker_cpu_max: ([.steps[0].samples[].worker_cpu_percent[]] | max),
    worker_cpu_total_max: ([.steps[0].samples[] | [.worker_cpu_percent[]] | add] | max)}' "$result"
echo "peak established on port 1935 = $peak of $clients players"
cat "$out/stderr.txt"
if grep -qiE 'cannot assign requested address|too many open files' "$out/stderr.txt"; then
    echo "FAIL: a player dial hit a load-side limit" >&2
    exit 1
fi
if [[ "$peak" -lt "$clients" ]]; then
    echo "FAIL: only $peak of $clients players connected" >&2
    exit 1
fi
echo "PASS: the load side admits $clients players"
