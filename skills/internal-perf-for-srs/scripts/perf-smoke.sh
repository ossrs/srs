#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")/../../.."
scenario="${1:-rtmp-rtmp}"
if [[ "$scenario" != rtmp-rtmp ]]; then
    echo "Unknown smoke scenario: $scenario" >&2
    exit 1
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
jq -e '.health.healthy == true and .steps[0].clients == 100 and .steps[0].samples[0].frames > 0' "$out/result.json" >/dev/null
echo "RTMP smoke passed: $out/result.json"
