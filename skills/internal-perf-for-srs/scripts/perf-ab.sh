#!/bin/bash
# The A/B run: a baseline SRS and the candidate, srs-perf:local built from the
# worktree, run side by side on their own single-core cpusets, each with its own
# load container on the shared load cpuset, started together on the CBR source
# and sampled each second, then compared paired per second.
#
#   perf-ab.sh -baseline <rev> | -revert <commit> -clients <n> [-target srs|proxy]
#              [-scenario rtmp-rtmp] [-settle 30] [-window 180] [-task ab] [-keep] [-dry-run]
#
# -baseline <rev> builds the baseline from that commit's tree; -revert <commit>
# from HEAD's tree with that commit's change taken out. Either way the tree is
# streamed from git into the image build, so the worktree is never touched. The
# baseline image srs-perf-ab:<task>-<rev> is removed at the end unless -keep.
# -dry-run prints the baseline's tree and image and builds nothing.
#
# -target proxy (also SRS_PERF_TARGET) compares two Go proxies instead: the
# baseline image is srs-perf:local with only the proxy rebuilt from the tree's
# Go module files, and -revert takes out only the commit's proxy change. Each
# side has its own proxy, its own SRS origin and its own load cores: the
# baseline's proxy on vCPU 1, origin on 2 and load on 5-6, the candidate's on 3,
# 4 and 7-8. The load cpusets are the same size, since the result's setup counts
# the load cores and compare refuses two setups that differ. Each side has one
# origin, many-streams too, so both fit the cores.
set -eo pipefail

source "$(dirname "$0")/perf-proxy-scenarios.sh"
cd "$(dirname "$0")/../../.."
lock="$PWD/skills/srs-develop/scripts/full-tier-lock.sh"
usage() { echo "$1" >&2; exit 2; }

baseline=
revert=
clients=0
scenario=rtmp-rtmp
settle=30
window=180
task=ab
keep=0
dry_run=0
target="${SRS_PERF_TARGET:-srs}"
while [[ $# -gt 0 ]]; do
    case "$1" in
        -baseline) baseline="$2"; shift 2 ;;
        -revert) revert="$2"; shift 2 ;;
        -clients) clients="$2"; shift 2 ;;
        -scenario) scenario="$2"; shift 2 ;;
        -settle) settle="$2"; shift 2 ;;
        -window) window="$2"; shift 2 ;;
        -task) task="$2"; shift 2 ;;
        -target) target="$2"; shift 2 ;;
        -keep) keep=1; shift ;;
        -dry-run) dry_run=1; shift ;;
        *) usage "Unknown option $1" ;;
    esac
done
[[ -n "$baseline" || -n "$revert" ]] || usage "Give -baseline <rev> or -revert <commit>"
[[ -z "$baseline" || -z "$revert" ]] || usage "Give -baseline or -revert, not both"
[[ "$task" =~ ^[a-z0-9][a-z0-9._-]*$ ]] || usage "Invalid -task, want lowercase such as p4.12"
[[ "$settle" =~ ^[0-9]+$ && "$window" =~ ^[1-9][0-9]*$ && "$clients" =~ ^[0-9]+$ ]] ||
    usage "Invalid -settle, -window or -clients"
[[ -f "cmd/perf/env/$scenario.env" ]] || usage "Unknown scenario $scenario"
case "$target" in
    srs) ;;
    proxy) is_proxy_scenario "$scenario" || usage "The proxy does not relay scenario $scenario" ;;
    *) usage "Invalid -target, want srs or proxy" ;;
esac

# The Go module files the proxy is built from.
proxy_paths=(go.mod go.sum cmd/proxy internal)

rev="${baseline:-$revert}"
commit="$(git rev-parse --verify --quiet "$rev^{commit}")" || usage "Unknown rev $rev"
if [[ "$target" == srs ]]; then
    # The baseline's trunk tree, since only trunk/ goes into the image: the commit's
    # own, or HEAD's with the commit's trunk change merged out (a three-way merge of
    # the trunk trees from the commit to its parent, onto HEAD). Merging only trunk
    # lets a commit that also changed tools, which later commits changed again, revert.
    if [[ -n "$baseline" ]]; then
        tree="$(git rev-parse --verify --quiet "$commit:trunk")" || usage "$rev has no trunk"
    else
        git rev-parse --verify --quiet "$commit^:trunk" >/dev/null || usage "$rev has no parent to revert to"
        tree="$(git merge-tree --write-tree --merge-base="$commit:trunk" HEAD:trunk "$commit^:trunk" | head -1)" ||
            usage "$rev does not revert cleanly from HEAD"
    fi
else
    # The baseline's whole tree, of which only the proxy's Go module files go into
    # the image: the commit's own, or HEAD's with the commit's change merged out.
    # A conflict outside those files, such as in tools later commits changed
    # again, does not matter, since the conflicted files are not built.
    if [[ -n "$baseline" ]]; then
        tree="$(git rev-parse "$commit^{tree}")"
    else
        git rev-parse --verify --quiet "$commit^" >/dev/null || usage "$rev has no parent to revert to"
        merged="$(git merge-tree --write-tree --name-only --merge-base="$commit" HEAD "$commit^")" && code=0 || code=$?
        [[ "$code" == 0 || "$code" == 1 ]] || usage "$rev does not revert from HEAD"
        tree="$(head -1 <<<"$merged")"
        # The conflicted files follow the tree, up to the first empty line.
        while IFS= read -r file; do
            [[ -n "$file" ]] || break
            for path in "${proxy_paths[@]}"; do
                if [[ "$file" == "$path" || "$file" == "$path/"* ]]; then
                    usage "$rev's proxy change does not revert cleanly from HEAD: $file"
                fi
            done
        done < <(tail -n +2 <<<"$merged")
    fi
    git rev-parse --verify --quiet "$tree:cmd/proxy" >/dev/null || usage "$rev has no cmd/proxy"
fi
image="srs-perf-ab:$task-$(git rev-parse --short "$commit")"
echo "baseline tree $tree"
echo "baseline image $image"
if [[ "$dry_run" == 1 ]]; then exit 0; fi
[[ "$clients" -gt 0 ]] || usage "Give -clients, such as the baseline's 90% hold count"

for c in $(docker ps -a --format '{{.Names}}'); do
    case "$c" in
        srs-perf-srs|srs-perf-load|srs-perf-proxy|srs-perf-origin*|srs-perf-ab-*)
            echo "Performance container $c exists; remove it first" >&2; exit 1 ;;
    esac
done

token=
heartbeat_pid=
cleanup() {
    if [[ -n "$heartbeat_pid" ]]; then
        kill "$heartbeat_pid" 2>/dev/null || true
        wait "$heartbeat_pid" 2>/dev/null || true
    fi
    SRS_PERF_NAME=srs-perf-ab-baseline bash skills/internal-perf-for-srs/scripts/perf-down.sh
    SRS_PERF_NAME=srs-perf-ab-candidate bash skills/internal-perf-for-srs/scripts/perf-down.sh
    if [[ -n "$token" ]]; then bash "$lock" release "$token" >/dev/null; fi
    if [[ "$keep" == 0 ]]; then
        docker rmi "$image" >/dev/null 2>&1 || true
    else
        echo "Kept $image; remove it with docker rmi when its comparison is done" >&2
    fi
}
trap cleanup EXIT

make perf
if [[ "$target" == srs ]]; then
    git archive --format=tar --add-file=cmd/perf/Dockerfile.ab --add-file=.dockerignore --prefix=trunk/ "$tree" |
        docker build -f Dockerfile.ab -t "$image" -
else
    git archive --format=tar --add-file=cmd/perf/Dockerfile.ab-proxy --add-file=.dockerignore \
        "$tree" "${proxy_paths[@]}" | docker build -f Dockerfile.ab-proxy -t "$image" -
fi

arch="$(docker info --format '{{.Architecture}}')"
case "$arch" in
    aarch64|arm64) arch=arm64 ;;
    x86_64|amd64) arch=amd64 ;;
    *) echo "Unsupported Docker CPU: $arch" >&2; exit 1 ;;
esac
GOOS=linux GOARCH="$arch" go -C cmd/perf build -o ../../cmd/perf/out/srs-perf-linux .
machine="$(sysctl -n hw.model 2>/dev/null || uname -m)"
docker_version="$(docker version --format '{{.Server.Version}}')"

token=$(bash "$lock" acquire --wait 3600 --pid $$)
(
    while sleep 15; do
        bash "$lock" refresh "$token" || exit 1
    done
) &
heartbeat_pid=$!

out="$PWD/cmd/perf/out"
run="$(date -u +%Y%m%dT%H%M%SZ)-$scenario-ab"
mkdir -p "$out/$run"
if [[ "$target" == srs ]]; then
    # Each SRS on its own core; both load containers share the load cores.
    base_cpus=1; cand_cpus=2; base_origin=; cand_origin=; base_load=3-9; cand_load=3-9
    SRS_PERF_NAME=srs-perf-ab-baseline SRS_PERF_TARGET=srs SRS_PERF_SRS_IMAGE="$image" \
        SRS_PERF_SRS_CPUS="$base_cpus" SRS_PERF_LOAD_CPUS="$base_load" \
        SRS_PERF_SCENARIO="$scenario" SRS_PERF_OUT="$out" \
        bash skills/internal-perf-for-srs/scripts/perf-up.sh
    SRS_PERF_NAME=srs-perf-ab-candidate SRS_PERF_TARGET=srs SRS_PERF_SRS_IMAGE=srs-perf:local \
        SRS_PERF_SRS_CPUS="$cand_cpus" SRS_PERF_LOAD_CPUS="$cand_load" \
        SRS_PERF_SCENARIO="$scenario" SRS_PERF_OUT="$out" \
        bash skills/internal-perf-for-srs/scripts/perf-up.sh
else
    # Each side's proxy, origin and load on their own cores; only the proxy
    # image differs, the origins are srs-perf:local's SRS on both sides.
    base_cpus=1; cand_cpus=3; base_origin=2; cand_origin=4; base_load=5-6; cand_load=7-8
    SRS_PERF_NAME=srs-perf-ab-baseline SRS_PERF_TARGET=proxy SRS_PERF_ORIGINS=1 \
        SRS_PERF_PROXY_IMAGE="$image" SRS_PERF_PROXY_CPUS="$base_cpus" \
        SRS_PERF_ORIGIN_CPUS="$base_origin" SRS_PERF_LOAD_CPUS="$base_load" \
        SRS_PERF_SCENARIO="$scenario" SRS_PERF_OUT="$out" \
        bash skills/internal-perf-for-srs/scripts/perf-up.sh
    SRS_PERF_NAME=srs-perf-ab-candidate SRS_PERF_TARGET=proxy SRS_PERF_ORIGINS=1 \
        SRS_PERF_PROXY_IMAGE=srs-perf:local SRS_PERF_PROXY_CPUS="$cand_cpus" \
        SRS_PERF_ORIGIN_CPUS="$cand_origin" SRS_PERF_LOAD_CPUS="$cand_load" \
        SRS_PERF_SCENARIO="$scenario" SRS_PERF_OUT="$out" \
        bash skills/internal-perf-for-srs/scripts/perf-up.sh
fi
for side in baseline candidate; do
    docker cp cmd/perf/out/srs-perf-linux "srs-perf-ab-$side-load:/usr/local/bin/srs-perf"
done

# Both holds start together and share the -ab-run id, so compare pairs their
# seconds by wall-clock time.
pids=()
for side in baseline candidate; do
    docker exec "srs-perf-ab-$side-load" srs-perf run -scenario "$scenario" -target "$target" -mode hold \
        -clients "$clients" -settle "$settle" -window "$window" -ab-run "$run" \
        -output "/out/$run/$side" -machine "$machine" -docker-version "$docker_version" \
        > "$out/$run/$side-path.txt" &
    pids+=($!)
done
for pid in "${pids[@]}"; do wait "$pid"; done

old="$out$(sed 's|^/out||' "$out/$run/baseline-path.txt")"
new="$out$(sed 's|^/out||' "$out/$run/candidate-path.txt")"
jq -n --slurpfile a "$old" --slurpfile b "$new" \
    --arg run "$run" --arg rev "$rev" --arg commit "$commit" --arg tree "$tree" \
    --arg image "$image" --arg mode "$([[ -n "$baseline" ]] && echo baseline || echo revert)" \
    --arg candidate "$(git describe --always --dirty)" --arg target "$target" \
    --arg bc "$base_cpus" --arg cc "$cand_cpus" --arg bo "$base_origin" --arg co "$cand_origin" \
    --arg bl "$base_load" --arg cl "$cand_load" \
    --argjson clients "$clients" --argjson settle "$settle" --argjson window "$window" '{
        ab_run: $run, target: $target, clients: $clients, settle: $settle, window: $window,
        baseline: ({mode: $mode, rev: $rev, commit: $commit, tree: $tree, image: $image, cpuset: $bc}
            + if $target == "proxy" then {origin_cpuset: $bo, load_cpuset: $bl} else {} end),
        candidate: ({worktree: $candidate, image: "srs-perf:local", cpuset: $cc}
            + if $target == "proxy" then {origin_cpuset: $co, load_cpuset: $cl} else {} end)
    } + (if $target == "srs" then {load_cpuset: $bl} else {} end)
      + {results: [$a[0], $b[0]]}' > "$out/$run/ab.json"
bin/srs-perf compare "$old" "$new" > "$out/$run/verdict.txt"
cat "$out/$run/verdict.txt"
echo "A/B output $out/$run"
