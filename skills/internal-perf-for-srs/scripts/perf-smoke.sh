#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")/../../.."
# Every scenario of the registry; perf-skill-test.sh checks this list against it.
scenarios=(rtmp-rtmp rtmp-flv rtmp-ts rtmp-rtsp srt-srt rtmp-whep whip-whep srt-rtmp srt-whep whip-rtmp whip-flv rtmp-hls rtmp-dash rtmp-pub whip-pub many-streams delays)

# With no scenario, smoke each one in turn, each in its own containers, and
# fail when any fails, after running them all.
if [[ $# -eq 0 || -z "$1" ]]; then
    # A string, not an array: Bash 3.2 calls an empty array unbound under set -u.
    failed=""
    passed=0
    for s in "${scenarios[@]}"; do
        mkdir -p "cmd/perf/out/smoke-$s"
        log="cmd/perf/out/smoke-$s/smoke.log"
        start=$SECONDS
        if bash "$0" "$s" > "$log" 2>&1; then
            echo "PASS $s ($((SECONDS - start))s)"
            passed=$((passed + 1))
        else
            echo "FAIL $s ($((SECONDS - start))s): $(tail -1 "$log"), log $log"
            failed="$failed $s"
        fi
    done
    echo "Smoke all: $passed of ${#scenarios[@]} passed${failed:+, failed:$failed}"
    [[ -z "$failed" ]] || exit 1
    exit 0
fi

scenario="$1"
known=false
for s in "${scenarios[@]}"; do
    [[ "$s" == "$scenario" ]] && known=true
done
if [[ "$known" != true ]]; then
    echo "Unknown smoke scenario: $scenario" >&2
    exit 1
fi
# A stream plays 10s with frames every second. A segmented scenario plays
# three 10s segments, its frames a segment at a time, and each of its 100
# players gets at least one segment.
seconds=10
segmented=false
if [[ "$scenario" == rtmp-hls || "$scenario" == rtmp-dash ]]; then
    seconds=30
    segmented=true
fi
# SRS converts WebRTC to RTMP only after two sender reports of each track, one
# second apart, so a WHIP-to-RTMP stream starts in the second second.
first=0
if [[ "$scenario" == whip-rtmp || "$scenario" == whip-flv ]]; then
    first=2
fi

out="$PWD/cmd/perf/out/smoke-$scenario"
mkdir -p "$out"
arch="$(docker info --format '{{.Architecture}}')"
case "$arch" in
    aarch64|arm64) arch=arm64 ;;
    x86_64|amd64) arch=amd64 ;;
    *) echo "Unsupported Docker CPU: $arch" >&2; exit 1 ;;
esac
GOOS=linux GOARCH="$arch" go -C cmd/perf build -o "../../cmd/perf/out/srs-perf-linux" .
cleanup() {
    make perf-down >/dev/null
}
trap cleanup EXIT

SRS_PERF_SCENARIO="$scenario" SRS_PERF_OUT="$out" make perf-up
docker cp cmd/perf/out/srs-perf-linux srs-perf-load:/usr/local/bin/srs-perf
docker exec srs-perf-load srs-perf smoke -scenario "$scenario" > "$out/result.json"
# The delay probes settle 5s and count 10s: pauses answered, and a lag on each
# path: the origin, the forward target, and the edge's play and publish.
if [[ "$scenario" == delays ]]; then
    jq -e '.scenario.name == "delays" and .health.healthy == true and .setup.source_flatness.flat == true and .steps[0].clients == 5 and .delays.pause_millis.samples >= 3 and .delays.unpause_millis.samples >= 3 and .cpu_per_client == 0' "$out/result.json" >/dev/null
    jq -e 'all(.delays | (.origin_lag_millis, .forward_lag_millis, .edge_play_lag_millis, .edge_publish_lag_millis); .samples > 0)' "$out/result.json" >/dev/null
    jq -e '(.steps[0].samples | length) == 15 and all(.steps[0].samples[]; .srs_cpu_usage_usec > 0 and .srs_rss_bytes > 0) and all(.steps[0].samples[5:][]; .frames > 0)' "$out/result.json" >/dev/null
    jq -c '.delays | with_entries(if (.value | type) == "object" then .value |= {samples, mean: (.mean | round), max: (.max | round)} else .value |= round end)' "$out/result.json"
    echo "Smoke $scenario passed: $out/result.json"
    exit 0
fi
jq -e --arg scenario "$scenario" '.scenario.name == $scenario and .health.healthy == true and .setup.source_flatness.flat == true and .steps[0].clients == 100' "$out/result.json" >/dev/null
if [[ "$segmented" == true ]]; then
    jq -e '([.steps[0].samples[].frames] | add) > 0 and ([.steps[0].samples[].segments // 0] | add) >= 100' "$out/result.json" >/dev/null
else
    jq -e --argjson first "$first" '.steps[0].samples[$first].frames > 0 and all(.steps[0].samples[$first:][]; .frames > 0)' "$out/result.json" >/dev/null
    # Lag is measured on video only, so a stream whose players get only audio
    # reads 0 in every second.
    jq -e '([.steps[0].samples[].lag_millis] | max) > 0' "$out/result.json" >/dev/null || {
        echo "Smoke $scenario: no lag in any second, so no player got video" >&2
        exit 1
    }
fi
# One sample a second, SRS CPU from its cgroup, and the window's mean and sd.
jq -e --argjson seconds "$seconds" '(.steps[0].samples | length) == $seconds and all(.steps[0].samples[]; .srs_cpu_usage_usec > 0 and .srs_rss_bytes > 0) and .steps[0].window.samples >= 3 and .steps[0].window.srs_cpu_percent.mean > 0 and .steps[0].window.srs_cpu_percent.mean < 100 and .steps[0].window.srs_cpu_percent.sd >= 0' "$out/result.json" >/dev/null
echo "Smoke $scenario passed: $out/result.json"
