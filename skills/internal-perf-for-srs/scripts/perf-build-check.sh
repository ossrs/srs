#!/bin/bash
# The build check of the profiling builds (C6): each build, one at a time, is
# configured and made from the worktree's trunk in its own image
# srs-perf-build:<name> (cmd/perf/Dockerfile.build on srs-perf:local), then
# started with ./objs/srs -e. It records for each whether it configures, builds
# and starts, and writes build-check.json with the logs beside it. A failing
# build is a result, not an error: the script fails only when it cannot check.
#
#   perf-build-check.sh [-only gperf|gprof|valgrind]
#
# Each image and container is removed when the check ends.
set -eo pipefail

cd "$(dirname "$0")/../../.."
lock="$PWD/skills/srs-develop/scripts/full-tier-lock.sh"
usage() { echo "$1" >&2; exit 2; }

# name|configure flags|packages its configure requires
builds=(
    "gperf|--gperf=on --gcp=on --gmp=on|"
    "gprof|--gprof=on|"
    "valgrind|--valgrind=on|valgrind"
)
only=
while [[ $# -gt 0 ]]; do
    case "$1" in
        -only) only="$2"; shift 2 ;;
        *) usage "Unknown option $1" ;;
    esac
done
if [[ -n "$only" ]]; then
    found=0
    for b in "${builds[@]}"; do [[ "${b%%|*}" == "$only" ]] && found=1; done
    [[ "$found" == 1 ]] || usage "Unknown build $only, want gperf, gprof or valgrind"
fi

for c in $(docker ps -a --format '{{.Names}}'); do
    case "$c" in
        srs-perf-build-*) echo "Build check container $c exists; remove it first" >&2; exit 1 ;;
    esac
done

token=
heartbeat_pid=
images=()
cleanup() {
    if [[ -n "$heartbeat_pid" ]]; then
        kill "$heartbeat_pid" 2>/dev/null || true
        wait "$heartbeat_pid" 2>/dev/null || true
    fi
    for b in "${builds[@]}"; do docker rm -f "srs-perf-build-${b%%|*}" >/dev/null 2>&1 || true; done
    for image in "${images[@]}"; do docker rmi "$image" >/dev/null 2>&1 || true; done
    if [[ -n "$token" ]]; then bash "$lock" release "$token" >/dev/null; fi
}
trap cleanup EXIT

make perf-image

# The builds use most of the Docker VM's cores, which would skew a measurement.
token=$(bash "$lock" acquire --wait 3600 --pid $$)
(
    while sleep 15; do
        bash "$lock" refresh "$token" || exit 1
    done
) &
heartbeat_pid=$!

run="$(date -u +%Y%m%dT%H%M%SZ)-build-check"
out="$PWD/cmd/perf/out/$run"
mkdir -p "$out"
results=()
for b in "${builds[@]}"; do
    IFS='|' read -r name flags packages <<< "$b"
    [[ -z "$only" || "$only" == "$name" ]] || continue
    image="srs-perf-build:$name"
    dir="$out/$name"
    mkdir -p "$dir"
    echo "Build check $name: $flags"
    start_time=$(date +%s)
    image_status=pass
    configure=skipped
    make_status=skipped
    started=skipped
    version=
    macros=()
    error=
    images+=("$image")
    if ! docker build -f cmd/perf/Dockerfile.build --build-arg "FLAGS=$flags" \
        --build-arg "PACKAGES=$packages" -t "$image" . > "$dir/docker-build.log" 2>&1; then
        image_status=fail
        error="$(tail -n 20 "$dir/docker-build.log")"
    else
        docker run --rm "$image" cat /srs/build/configure.log > "$dir/configure.log" 2>&1 || true
        docker run --rm "$image" cat /srs/build/make.log > "$dir/make.log" 2>/dev/null || true
        configure="$(docker run --rm "$image" cat /srs/build/configure.status)"
        make_status="$(docker run --rm "$image" cat /srs/build/make.status)"
        [[ "$configure" == 0 ]] && configure=pass || configure=fail
        case "$make_status" in
            0) make_status=pass ;;
            skipped) ;;
            *) make_status=fail ;;
        esac
        if [[ "$configure" == fail ]]; then error="$(tail -n 20 "$dir/configure.log")"; fi
        if [[ "$make_status" == fail ]]; then error="$(tail -n 20 "$dir/make.log")"; fi
        # Evidence the flags took effect: the macros configure defined, and -pg
        # when objs/Makefile compiles with gprof (which defines no macro).
        if [[ "$configure" == pass ]]; then
            while read -r m; do macros+=("$m"); done < <(docker run --rm "$image" \
                grep -oE '^#define SRS_(GPERF|GPERF_MC|GPERF_MD|GPERF_MP|GPERF_CP|VALGRIND)$' objs/srs_auto_headers.hpp |
                sed 's/^#define //' || true)
            if docker run --rm "$image" grep -qE -- '(^| )-pg( |$)' objs/Makefile; then macros+=("-pg"); fi
        fi
    fi
    # Started: the main server is up within 10s, and SRS still runs 3s later.
    if [[ "$make_status" == pass ]]; then
        started=fail
        version="$(docker run --rm "$image" ./objs/srs -v 2>&1 | tail -n 1)"
        docker run -d --name "srs-perf-build-$name" "$image" ./objs/srs -e >/dev/null
        for _ in $(seq 1 20); do
            if docker logs "srs-perf-build-$name" 2>&1 | grep -q "server main cid="; then started=up; break; fi
            sleep 0.5
        done
        if [[ "$started" == up ]]; then
            sleep 3
            if [[ "$(docker inspect -f '{{.State.Running}}' "srs-perf-build-$name")" == true ]]; then
                started=pass
            else
                started=fail
            fi
        fi
        docker logs "srs-perf-build-$name" > "$dir/start.log" 2>&1 || true
        docker rm -f "srs-perf-build-$name" >/dev/null
        if [[ "$started" == fail ]]; then error="$(tail -n 20 "$dir/start.log")"; fi
    fi
    docker rmi "$image" >/dev/null 2>&1 || true
    seconds=$(( $(date +%s) - start_time ))
    echo "Build check $name: image $image_status, configure $configure, build $make_status, start $started (${seconds}s)"
    results+=("$(jq -n --arg name "$name" --arg flags "$flags" --arg packages "$packages" \
        --arg image "$image_status" --arg configure "$configure" --arg build "$make_status" \
        --arg start "$started" --arg version "$version" --arg error "$error" \
        --argjson seconds "$seconds" --arg dir "$name" \
        --argjson macros "$(printf '%s\n' "${macros[@]}" | jq -R . | jq -s 'map(select(. != ""))')" '{
            name: $name, flags: $flags, packages: $packages, image: $image,
            configure: $configure, build: $build, start: $start, version: $version,
            macros: $macros, seconds: $seconds, logs: $dir, error: $error
        }')")
done

printf '%s\n' "${results[@]}" | jq -s --arg run "$run" --arg worktree "$(git describe --always --dirty)" \
    --arg docker_version "$(docker version --format '{{.Server.Version}}')" \
    --arg arch "$(docker info --format '{{.Architecture}}')" '{
        run: $run, worktree: $worktree, docker_version: $docker_version, arch: $arch,
        base_image: "srs-perf:local", builds: .
    }' > "$out/build-check.json"
cat "$out/build-check.json"
echo "Build check output $out/build-check.json"
