#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")/../../.."
out="${SRS_PERF_OUT:-$PWD/cmd/perf/out}"
mkdir -p "$out"
# perf-ab.sh starts two pairs side by side: each names its pair, its SRS image
# and the cpusets. The load container always runs srs-perf:local's tools.
name="${SRS_PERF_NAME:-srs-perf}"
srs_image="${SRS_PERF_SRS_IMAGE:-srs-perf:local}"
srs_cpus="${SRS_PERF_SRS_CPUS:-1}"
load_cpus="${SRS_PERF_LOAD_CPUS:-2-9}"
scenario="${SRS_PERF_SCENARIO:-rtmp-rtmp}"

for container in "$name-srs" "$name-load" "$name-forward" "$name-edge"; do
    if docker inspect "$container" >/dev/null 2>&1; then
        echo "Performance containers $name-* already exist; run make perf-down first" >&2
        exit 1
    fi
done

cleanup() {
    docker rm -f "$name-load" "$name-forward" "$name-edge" "$name-srs" >/dev/null 2>&1 || true
}
trap cleanup ERR

for key in rmem_max wmem_max rmem_default wmem_default; do
    docker run --rm --privileged --network host srs-perf:local \
        sysctl -w "net.core.$key=16777216" >/dev/null
done

# The load container shares this network namespace, so its 40000 players dial
# 127.0.0.1:1935 from this port range; the default has only 28232 ports.
docker run -d --name "$name-srs" --privileged --cpuset-cpus="$srs_cpus" \
    --ulimit nofile=1048576:1048576 \
    --sysctl net.ipv4.ip_local_port_range="10000 65535" \
    -v "$out:/out" \
    --env-file "$PWD/cmd/perf/env/$scenario.env" \
    -v "$PWD/skills/internal-perf-for-srs/scripts:/perf-scripts:ro" \
    "$srs_image" ./objs/srs -e >/dev/null

# A scenario may add an SRS beside the measured one, such as the delay probes'
# forward target and edge: each role with an env <scenario>-<role>.env runs in
# the SRS container's network namespace, on the load cpuset, since the
# measured SRS keeps its own core and the probes' load is a few players.
for role in forward edge; do
    env="$PWD/cmd/perf/env/$scenario-$role.env"
    [[ -f "$env" ]] || continue
    docker run -d --name "$name-$role" --cpuset-cpus="$load_cpus" \
        --network "container:$name-srs" \
        --ulimit nofile=1048576:1048576 \
        --env-file "$PWD/cmd/perf/env/$scenario-$role.env" \
        "$srs_image" ./objs/srs -e >/dev/null
done

# The host cgroup namespace lets the load tool find SRS's cgroup from
# /proc/1/cgroup and read its cpu.stat usage_usec each second.
docker run -d --name "$name-load" --cpuset-cpus="$load_cpus" \
    --pid "container:$name-srs" --network "container:$name-srs" \
    --cgroupns host \
    --ulimit nofile=1048576:1048576 \
    -v "$out:/out" \
    -v "$PWD/skills/internal-perf-for-srs/scripts:/perf-scripts:ro" \
    srs-perf:local sleep infinity >/dev/null

trap - ERR
