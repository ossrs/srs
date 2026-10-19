#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")/../../.."

cleanup() {
    make perf-down >/dev/null
}
trap cleanup EXIT

make perf-up

test "$(docker inspect -f '{{.State.Running}}' srs-perf-srs)" = true
test "$(docker inspect -f '{{.State.Running}}' srs-perf-load)" = true
test "$(docker inspect -f '{{.HostConfig.CpusetCpus}}' srs-perf-srs)" = 1
test "$(docker inspect -f '{{.HostConfig.CpusetCpus}}' srs-perf-load)" = 2-9
srs_id=$(docker inspect -f '{{.Id}}' srs-perf-srs)
test "$(docker inspect -f '{{.HostConfig.PidMode}}' srs-perf-load)" = "container:$srs_id"
test "$(docker inspect -f '{{.HostConfig.NetworkMode}}' srs-perf-load)" = "container:$srs_id"
test "$(docker inspect -f '{{.HostConfig.Privileged}}' srs-perf-srs)" = true
test "$(docker inspect -f '{{range .Mounts}}{{if eq .Destination "/out"}}{{.Source}}{{end}}{{end}}' srs-perf-load)" = "$PWD/cmd/perf/out"

test "$(docker exec srs-perf-srs nproc)" = 1
test "$(docker exec srs-perf-load nproc)" = 8
test "$(docker exec srs-perf-srs bash -c 'ulimit -n')" = 1048576
test "$(docker exec srs-perf-load bash -c 'ulimit -n')" = 1048576

for key in rmem_max wmem_max rmem_default wmem_default; do
    test "$(docker exec srs-perf-srs sysctl -n net.core.$key)" = 16777216
    test "$(docker exec srs-perf-load sysctl -n net.core.$key)" = 16777216
done

# 40000 players dial 127.0.0.1:1935 from one network namespace; the default
# 32768-60999 range has only 28232 ports.
test "$(docker exec srs-perf-load sysctl -n net.ipv4.ip_local_port_range | awk '{print $1, $2}')" = "10000 65535"

make perf-down
test "$(docker inspect -f '{{.State.Running}}' srs-perf-srs 2>/dev/null || true)" = ''
test "$(docker inspect -f '{{.State.Running}}' srs-perf-load 2>/dev/null || true)" = ''
trap - EXIT
