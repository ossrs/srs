#!/bin/bash
# Starts the measured server and the load container. -target srs (the
# default) is one SRS; -target proxy is the Go proxy with one to four SRS
# origins behind it (-origins 1-4, by default the scenario's count), all in
# the proxy's network namespace.
set -euo pipefail

source "$(dirname "$0")/perf-proxy-scenarios.sh"
cd "$(dirname "$0")/../../.."
out="${SRS_PERF_OUT:-$PWD/cmd/perf/out}"
mkdir -p "$out"
# perf-ab.sh starts two pairs side by side: each names its pair, its SRS image
# and the cpusets. The load container always runs srs-perf:local's tools.
name="${SRS_PERF_NAME:-srs-perf}"
srs_image="${SRS_PERF_SRS_IMAGE:-srs-perf:local}"
scenario="${SRS_PERF_SCENARIO:-rtmp-rtmp}"
target="${SRS_PERF_TARGET:-srs}"
origins="${SRS_PERF_ORIGINS:-}"

usage() {
    echo "Usage: perf-up.sh [-target srs|proxy] [-origins 1-4]" >&2
    exit 2
}
while [[ $# -gt 0 ]]; do
    case "$1" in
        -target) [[ $# -ge 2 ]] || usage; target="$2"; shift 2 ;;
        -origins) [[ $# -ge 2 ]] || usage; origins="$2"; shift 2 ;;
        *) usage ;;
    esac
done
case "$target" in
    srs) [[ -z "$origins" ]] || { echo "-origins needs -target proxy" >&2; exit 2; } ;;
    proxy)
        is_proxy_scenario "$scenario" || { echo "The proxy does not relay scenario $scenario" >&2; exit 2; }
        origins="${origins:-$(proxy_origins "$scenario")}"
        [[ "$origins" =~ ^[1-4]$ ]] || usage
        ;;
    *) usage ;;
esac

if [[ "$target" == proxy ]]; then
    # One core each (the proxy on 1, origin i on i+1), the load on the rest.
    owner="$name-proxy"
    proxy_image="${SRS_PERF_PROXY_IMAGE:-srs-perf:local}"
    proxy_cpus="${SRS_PERF_PROXY_CPUS:-1}"
    read -r -a origin_cpus <<<"${SRS_PERF_ORIGIN_CPUS:-2 3 4 5}"
    load_cpus="${SRS_PERF_LOAD_CPUS:-$((origins + 2))-9}"
else
    owner="$name-srs"
    srs_cpus="${SRS_PERF_SRS_CPUS:-1}"
    load_cpus="${SRS_PERF_LOAD_CPUS:-2-9}"
fi

containers=("$name-srs" "$name-load" "$name-forward" "$name-edge" "$name-proxy" "$name-origin1" "$name-origin2" "$name-origin3" "$name-origin4")
for container in "${containers[@]}"; do
    if docker inspect "$container" >/dev/null 2>&1; then
        echo "Performance containers $name-* already exist; run make perf-down first" >&2
        exit 1
    fi
done

cleanup() {
    docker rm -f "${containers[@]}" >/dev/null 2>&1 || true
}
trap cleanup ERR

for key in rmem_max wmem_max rmem_default wmem_default; do
    docker run --rm --privileged --network host srs-perf:local \
        sysctl -w "net.core.$key=16777216" >/dev/null
done

if [[ "$target" == proxy ]]; then
    # The proxy owns the network and pid namespaces: it listens on SRS's
    # default ports, so the clients are unchanged, and the load tool finds it
    # as pid 1 and the origins' SRS beside it.
    docker run -d --name "$owner" --cpuset-cpus="$proxy_cpus" \
        --ulimit nofile=1048576:1048576 \
        --sysctl net.ipv4.ip_local_port_range="10000 65535" \
        -v "$out:/out" \
        --env-file "$PWD/cmd/perf/env/proxy/proxy.env" \
        "$proxy_image" /srs/bin/srs-proxy >/dev/null
    for _ in $(seq 1 50); do
        docker exec "$owner" bash -c 'exec 3<>/dev/tcp/127.0.0.1/12025' 2>/dev/null && break
        sleep 0.2
    done

    # Each origin takes the scenario's env for what it serves, then its own
    # file for the alternate ports and the heartbeat to the proxy.
    for i in $(seq 1 "$origins"); do
        file=origin.env
        [[ "$i" == 1 ]] || file="origin$i.env"
        docker run -d --name "$name-origin$i" --cpuset-cpus="${origin_cpus[$((i - 1))]}" \
            --network "container:$owner" --pid "container:$owner" \
            --ulimit nofile=1048576:1048576 \
            --env-file "$PWD/cmd/perf/env/$scenario.env" \
            --env-file "$PWD/cmd/perf/env/proxy/$file" \
            "$srs_image" ./objs/srs -e >/dev/null
    done
else
    # The load container shares this network namespace, so its 40000 players dial
    # 127.0.0.1:1935 from this port range; the default has only 28232 ports.
    docker run -d --name "$owner" --privileged --cpuset-cpus="$srs_cpus" \
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
            --network "container:$owner" \
            --ulimit nofile=1048576:1048576 \
            --env-file "$PWD/cmd/perf/env/$scenario-$role.env" \
            "$srs_image" ./objs/srs -e >/dev/null
    done
fi

# The host cgroup namespace lets the load tool find the measured server's
# cgroup from /proc/1/cgroup and read its cpu.stat usage_usec each second.
docker run -d --name "$name-load" --cpuset-cpus="$load_cpus" \
    --pid "container:$owner" --network "container:$owner" \
    --cgroupns host \
    --ulimit nofile=1048576:1048576 \
    -v "$out:/out" \
    -v "$PWD/skills/internal-perf-for-srs/scripts:/perf-scripts:ro" \
    srs-perf:local sleep infinity >/dev/null

trap - ERR

# Each origin registers at startup and then every 9s; wait until the proxy
# has every one, so the first client finds an origin.
if [[ "$target" == proxy ]]; then
    for i in $(seq 1 "$origins"); do
        for _ in $(seq 1 30); do
            docker logs "$owner" 2>&1 | grep -q "device=origin$i" && break
            sleep 1
        done
        if ! docker logs "$owner" 2>&1 | grep -q "device=origin$i"; then
            echo "origin$i did not register with the proxy in 30s" >&2
            docker logs --tail 20 "$owner" >&2 2>&1 || true
            cleanup
            exit 1
        fi
    done
fi
