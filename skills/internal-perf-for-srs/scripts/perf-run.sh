#!/bin/bash
set -eo pipefail

cd "$(dirname "$0")/../../.."
lock="$PWD/skills/srs-develop/scripts/full-tier-lock.sh"
scenario=rtmp-rtmp
repeat=1
hold_clients=
pass_args=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        -scenario) scenario="$2"; shift 2 ;;
        -repeat) repeat="$2"; shift 2 ;;
        -hold-clients) hold_clients="$2"; shift 2 ;;
        *) pass_args+=("$1"); shift ;;
    esac
done
[[ "$scenario" == rtmp-rtmp && "$repeat" =~ ^[1-9][0-9]*$ ]] || {
    echo "Invalid scenario or repeat" >&2
    exit 2
}
# A fix is measured at the baseline's hold counts, "<50%>,<90%>", since its own capacity may differ.
[[ -z "$hold_clients" || "$hold_clients" =~ ^[1-9][0-9]*,[1-9][0-9]*$ ]] || {
    echo "Invalid -hold-clients, want <50%>,<90%> such as 6400,11520" >&2
    exit 2
}
for arg in "${pass_args[@]}"; do
    case "$arg" in
        -mode|-output|-srs-pid|-repeat|-scenario) echo "The run script controls $arg" >&2; exit 2 ;;
    esac
done

make perf
token=$(bash "$lock" acquire --wait 3600 --pid $$)
containers_started=0
heartbeat_pid=
cleanup() {
    if [[ -n "$heartbeat_pid" ]]; then
        kill "$heartbeat_pid" 2>/dev/null || true
        wait "$heartbeat_pid" 2>/dev/null || true
    fi
    if [[ "$containers_started" == 1 ]]; then make perf-down >/dev/null; fi
    bash "$lock" release "$token" >/dev/null
}
trap cleanup EXIT
(
    while sleep 15; do
        bash "$lock" refresh "$token" || exit 1
    done
) &
heartbeat_pid=$!

for port in 1935 1985 8080; do
    if lsof -nP -iTCP:"$port" -sTCP:LISTEN | grep -q .; then
        echo "SRS scenario port $port is already in use" >&2
        exit 1
    fi
done
make integ-prepare
bash "$lock" refresh "$token"

out="$PWD/cmd/perf/out"
run="$(date -u +%Y%m%dT%H%M%SZ)-$scenario"
mkdir -p "$out/$run"
arch="$(docker info --format '{{.Architecture}}')"
case "$arch" in
    aarch64|arm64) arch=arm64 ;;
    x86_64|amd64) arch=amd64 ;;
    *) echo "Unsupported Docker CPU: $arch" >&2; exit 1 ;;
esac
GOOS=linux GOARCH="$arch" go -C cmd/perf build -o ../../cmd/perf/out/srs-perf-linux .
machine="$(sysctl -n hw.model 2>/dev/null || uname -m)"
docker_version="$(docker version --format '{{.Server.Version}}')"

SRS_PERF_SCENARIO="$scenario" SRS_PERF_OUT="$out" make perf-up
containers_started=1
docker cp cmd/perf/out/srs-perf-linux srs-perf-load:/usr/local/bin/srs-perf

for ((i=1; i<=repeat; i++)); do
    repeat_out="$out/$run/repeat-$i"
    mkdir -p "$repeat_out"
    docker exec srs-perf-load srs-perf run -scenario "$scenario" -mode ramp \
        -output "/out/$run/repeat-$i" -machine "$machine" \
        -docker-version "$docker_version" "${pass_args[@]}" > "$repeat_out/ramp-path.txt"
    ramp_path="$(cat "$repeat_out/ramp-path.txt")"
    ramp_host="$out${ramp_path#/out}"
    capacity="$(jq -r '.capacity' "$ramp_host")"
    [[ "$capacity" =~ ^[1-9][0-9]*$ ]] || { echo "Ramp has no healthy capacity" >&2; exit 1; }

    # hold-50-noise repeats hold-50 at the same client count, so the change in
    # CPU per client between the two shows the noise of one run.
    for hold in 50 50-noise 90; do
        percent="${hold%-noise}"
        clients=$((capacity * percent / 100))
        if [[ -n "$hold_clients" && "$percent" == 50 ]]; then clients="${hold_clients%,*}"; fi
        if [[ -n "$hold_clients" && "$percent" == 90 ]]; then clients="${hold_clients#*,}"; fi
        if [[ "$clients" -lt 1 ]]; then clients=1; fi
        profile="/out/$run/repeat-$i/hold-$hold"
        docker exec srs-perf-srs bash /perf-scripts/perf-profile.sh "$profile" &
        profile_pid=$!
        docker exec srs-perf-load srs-perf run -scenario "$scenario" -mode hold \
            -clients "$clients" -seconds 75 -output "/out/$run/repeat-$i" \
            -machine "$machine" -docker-version "$docker_version" \
            > "$repeat_out/hold-$hold-path.txt"
        wait "$profile_pid"
    done
    first="$out$(sed 's|^/out||' "$repeat_out/hold-50-path.txt")"
    second="$out$(sed 's|^/out||' "$repeat_out/hold-50-noise-path.txt")"
    bin/srs-perf compare "$first" "$second" > "$repeat_out/noise.txt"
    jq -n --slurpfile a "$first" --slurpfile b "$second" '{
        clients: $a[0].steps[0].clients,
        cpu_per_client: [$a[0].cpu_per_client, $b[0].cpu_per_client],
        change: (if $a[0].cpu_per_client > 0
            then ($b[0].cpu_per_client - $a[0].cpu_per_client) / $a[0].cpu_per_client
            else null end)
    }' > "$repeat_out/noise.json"
done

jq -s --arg scenario "$scenario" --arg run "$run" \
    --argjson repeat "$repeat" \
    --argjson noise "$(jq -s . "$out/$run"/repeat-*/noise.json)" \
    '{scenario:$scenario,run:$run,repeat:$repeat,noise:$noise,results:.}' \
    "$out/$run"/repeat-*/*/result.json \
    > "$out/$run/run.json"
echo "$out/$run/run.json"
