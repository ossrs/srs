#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")/../../.."
out="$PWD/cmd/perf/out/env-check"
mkdir -p "$out"
rm -f "$out/perf.data" "$out/flamegraph.svg" "$out/top-symbols.txt" "$out/core-calibration.txt"

cleanup() {
    make perf-down >/dev/null
}
trap cleanup EXIT

SRS_PERF_OUT="$out" make perf-up
sleep 2
test "$(docker inspect -f '{{.State.Running}}' srs-perf-srs)" = true
test "$(docker inspect -f '{{.State.Running}}' srs-perf-load)" = true
test "$(docker exec srs-perf-srs nproc)" = 1
test "$(docker exec srs-perf-load nproc)" = 8
test "$(docker exec srs-perf-srs bash -c 'ulimit -n')" = 1048576
test "$(docker exec srs-perf-load bash -c 'ulimit -n')" = 1048576
for key in rmem_max wmem_max rmem_default wmem_default; do
    test "$(docker exec srs-perf-srs sysctl -n "net.core.$key")" = 16777216
    test "$(docker exec srs-perf-load sysctl -n "net.core.$key")" = 16777216
done

docker exec -d srs-perf-load ffmpeg -nostdin -hide_banner -loglevel error \
    -re -stream_loop -1 -i /srs/perf/cbr.flv -t 20 -c copy -f flv \
    rtmp://127.0.0.1/live/env-check >/dev/null
sleep 2
docker exec srs-perf-load pgrep -x ffmpeg >/dev/null
docker exec srs-perf-srs bash /perf-scripts/perf-capture.sh
test -s "$out/perf.data"
test -s "$out/flamegraph.svg"
test -s "$out/top-symbols.txt"
grep -Eq 'Srs[A-Za-z_]+|srs_' "$out/top-symbols.txt"

idle=$(docker exec srs-perf-srs bash /perf-scripts/perf-cpu-loop.sh)
docker exec -d srs-perf-load bash /perf-scripts/perf-cpu-busy.sh >/dev/null
sleep 2
test "$(docker exec srs-perf-load pgrep -x yes | wc -l | tr -d ' ')" -ge 8
busy=$(docker exec srs-perf-srs bash /perf-scripts/perf-cpu-loop.sh)
docker restart srs-perf-load >/dev/null
gap=$(awk -v idle="$idle" -v busy="$busy" 'BEGIN { printf "%.2f", 100 * (busy - idle) / idle }')
printf 'idle_seconds=%s\nbusy_seconds=%s\ngap_percent=%s\n' "$idle" "$busy" "$gap" > "$out/core-calibration.txt"
echo "perf captured SRS samples and wrote $out/flamegraph.svg"
echo "Core calibration: idle ${idle}s, busy ${busy}s, gap ${gap}%"
