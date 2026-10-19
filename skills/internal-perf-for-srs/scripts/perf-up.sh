#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")/../../.."
out="${SRS_PERF_OUT:-$PWD/cmd/perf/out}"
mkdir -p "$out"

if docker inspect srs-perf-srs >/dev/null 2>&1 || docker inspect srs-perf-load >/dev/null 2>&1; then
    echo "Performance containers already exist; run make perf-down first" >&2
    exit 1
fi

cleanup() {
    docker rm -f srs-perf-load srs-perf-srs >/dev/null 2>&1 || true
}
trap cleanup ERR

for key in rmem_max wmem_max rmem_default wmem_default; do
    docker run --rm --privileged --network host srs-perf:local \
        sysctl -w "net.core.$key=16777216" >/dev/null
done

# The load container shares this network namespace, so its 40000 players dial
# 127.0.0.1:1935 from this port range; the default has only 28232 ports.
docker run -d --name srs-perf-srs --privileged --cpuset-cpus=1 \
    --ulimit nofile=1048576:1048576 \
    --sysctl net.ipv4.ip_local_port_range="10000 65535" \
    -v "$out:/out" \
    --env-file "$PWD/cmd/perf/env/${SRS_PERF_SCENARIO:-rtmp-rtmp}.env" \
    -v "$PWD/skills/internal-perf-for-srs/scripts:/perf-scripts:ro" \
    srs-perf:local ./objs/srs -e >/dev/null

docker run -d --name srs-perf-load --cpuset-cpus=2-9 \
    --pid container:srs-perf-srs --network container:srs-perf-srs \
    --ulimit nofile=1048576:1048576 \
    -v "$out:/out" \
    -v "$PWD/doc/source.flv:/srs/trunk/doc/source.flv:ro" \
    -v "$PWD/skills/internal-perf-for-srs/scripts:/perf-scripts:ro" \
    srs-perf:local sleep infinity >/dev/null

trap - ERR
