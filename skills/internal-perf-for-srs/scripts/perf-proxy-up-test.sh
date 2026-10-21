#!/bin/bash
# Tests perf-up.sh -target proxy: the proxy on vCPU 1 with GOMAXPROCS=1 and Go
# pprof on loopback, each origin on its own core and registered with the
# proxy, one RTMP player through the proxy, and perf-down.sh leaving no
# container. It starts the containers four times: with the default three
# origins, with one, with four, and with two from SRS_PERF_ORIGINS.
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
refuses() {
    local name="$1"
    shift
    if bash "$scripts/perf-up.sh" "$@" >/dev/null 2>&1; then
        fail "$name"
        bash "$scripts/perf-down.sh" >/dev/null 2>&1
    else
        pass "$name"
    fi
}
up() {
    local name="$1"
    shift
    if bash "$scripts/perf-up.sh" "$@"; then pass "$name"; else fail "$name"; fi
}
inspect() { docker inspect -f "$2" "$1" 2>/dev/null; }
equals() { test "$1" = "$2"; }
no_containers() { test -z "$(docker ps -a --filter name=srs-perf- --format '{{.Names}}')"; }
# A TCP listener on 127.0.0.1, read from /proc/net/tcp in the shared namespace.
loopback_listener() {
    local hex
    hex=$(printf '0100007F:%04X' "$1")
    docker exec srs-perf-load awk -v a="$hex" '$2 == a && $4 == "0A" {f = 1} END {exit !f}' /proc/net/tcp
}
connects() { docker exec srs-perf-load bash -c "exec 3<>/dev/tcp/127.0.0.1/$1"; }
registered() { docker logs srs-perf-proxy 2>&1 | grep -q "device=$1"; }
pprof_ok() {
    docker exec srs-perf-load bash -c \
        'exec 3<>/dev/tcp/127.0.0.1/6060; printf "GET /debug/pprof/ HTTP/1.0\r\n\r\n" >&3; head -1 <&3' \
        | grep -q ' 200 '
}
# origins_are <label> <n>: origins 1 to n each on vCPU i+1 and registered, no
# origin n+1, and the load on vCPUs n+2 to 9.
origins_are() {
    local label="$1" n="$2" i
    for i in $(seq 1 "$n"); do
        check "$label: origin$i on vCPU $((i + 1))" equals "$(inspect "srs-perf-origin$i" '{{.HostConfig.CpusetCpus}}')" "$((i + 1))"
        check "$label: origin$i registered" registered "origin$i"
    done
    check "$label: no origin$((n + 1))" equals "$(inspect "srs-perf-origin$((n + 1))" '{{.State.Running}}')" ''
    check "$label: load on vCPUs $((n + 2))-9" equals "$(inspect srs-perf-load '{{.HostConfig.CpusetCpus}}')" "$((n + 2))-9"
}

cleanup() { bash "$scripts/perf-down.sh" >/dev/null 2>&1 || true; }
trap cleanup EXIT

check "no perf container before the test" no_containers

refuses "an unknown target is refused" -target foo
refuses "zero origins are refused" -target proxy -origins 0
refuses "five origins are refused" -target proxy -origins 5
refuses "-origins without -target proxy is refused" -origins 2
SRS_PERF_SCENARIO=rtmp-rtsp refuses "a scenario the proxy does not relay is refused" -target proxy
SRS_PERF_SCENARIO=delays refuses "the delay probes are refused" -target proxy
check "no container after the refusals" no_containers

# Three origins by default, for every proxy scenario.
up "perf-up -target proxy" -target proxy
proxy_id=$(inspect srs-perf-proxy '{{.Id}}')
check "proxy on vCPU 1" equals "$(inspect srs-perf-proxy '{{.HostConfig.CpusetCpus}}')" 1
origins_are "three origins" 3
check "proxy sees one CPU" equals "$(docker exec srs-perf-proxy nproc 2>/dev/null)" 1
check "proxy GOMAXPROCS=1" equals "$(docker exec srs-perf-proxy bash -c 'tr "\0" "\n" < /proc/1/environ' 2>/dev/null | grep '^GOMAXPROCS=')" GOMAXPROCS=1
check "proxy is the srs-proxy binary" equals "$(docker exec srs-perf-proxy cat /proc/1/comm 2>/dev/null)" srs-proxy
check "no SRS container" equals "$(inspect srs-perf-srs '{{.State.Running}}')" ''
for c in origin1 origin2 origin3 load; do
    check "$c in the proxy's network namespace" \
        equals "$(inspect "srs-perf-$c" '{{.HostConfig.NetworkMode}}')" "container:$proxy_id"
    check "$c in the proxy's pid namespace" \
        equals "$(inspect "srs-perf-$c" '{{.HostConfig.PidMode}}')" "container:$proxy_id"
done
check "load in the host cgroup namespace" equals "$(inspect srs-perf-load '{{.HostConfig.CgroupnsMode}}')" host
check "load port range" equals "$(docker exec srs-perf-load sysctl -n net.ipv4.ip_local_port_range 2>/dev/null | awk '{print $1, $2}')" "10000 65535"
check "Go pprof listens on loopback 6060" loopback_listener 6060
check "Go pprof answers" pprof_ok
for port in 1935 8080 1985 12025; do
    check "proxy listens on $port" connects "$port"
done
check "origin1 RTMP on 19351" connects 19351
check "origin2 RTMP on 19361" connects 19361
check "origin3 RTMP on 19371" connects 19371

# One RTMP publisher and one player, both through the proxy.
stream="live/proxy-up-test-$$"
docker exec -d srs-perf-load ffmpeg -nostdin -re -stream_loop -1 -i /srs/perf/cbr.flv \
    -c copy -f flv "rtmp://127.0.0.1:1935/$stream"
sleep 3
probe=$(docker exec srs-perf-load ffprobe -v error -rw_timeout 10000000 -show_streams \
    "rtmp://127.0.0.1:1935/$stream" 2>&1)
check "player gets video through the proxy" grep -q codec_type=video <<<"$probe"
check "player gets audio through the proxy" grep -q codec_type=audio <<<"$probe"

bash "$scripts/perf-down.sh"
check "three origins: perf-down leaves no container" no_containers

up "perf-up -target proxy -origins 1" -target proxy -origins 1
origins_are "one origin" 1
bash "$scripts/perf-down.sh"
check "one origin: perf-down leaves no container" no_containers

up "perf-up -target proxy -origins 4" -target proxy -origins 4
origins_are "four origins" 4
check "four origins: origin4 RTMP on 19381" connects 19381
bash "$scripts/perf-down.sh"
check "four origins: perf-down leaves no container" no_containers

# SRS_PERF_ORIGINS overrides the default, many-streams too.
SRS_PERF_SCENARIO=many-streams SRS_PERF_ORIGINS=2 up "perf-up -target proxy with SRS_PERF_ORIGINS=2" -target proxy
origins_are "SRS_PERF_ORIGINS=2" 2
bash "$scripts/perf-down.sh"
check "perf-down leaves no container" no_containers
trap - EXIT

if [[ $failed != 0 ]]; then
    echo "perf-proxy-up-test: FAILED"
    exit 1
fi
echo "perf-proxy-up-test: all passed"
