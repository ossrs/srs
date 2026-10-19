#!/bin/bash
# The A/B run: a baseline SRS and the candidate, srs-perf:local built from the
# worktree, run side by side on their own single-core cpusets, each with its own
# load container on the shared load cpuset, started together on the CBR source
# and sampled each second, then compared paired per second.
#
#   perf-ab.sh -baseline <rev> | -revert <commit> -clients <n>
#              [-scenario rtmp-rtmp] [-settle 30] [-window 180] [-task ab] [-keep] [-dry-run]
#
# -baseline <rev> builds the baseline from that commit's tree; -revert <commit>
# from HEAD's tree with that commit's change taken out. Either way the tree is
# streamed from git into the image build, so the worktree is never touched. The
# baseline image srs-perf-ab:<task>-<rev> is removed at the end unless -keep.
# -dry-run prints the baseline's tree and image and builds nothing.
set -eo pipefail

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
while [[ $# -gt 0 ]]; do
    case "$1" in
        -baseline) baseline="$2"; shift 2 ;;
        -revert) revert="$2"; shift 2 ;;
        -clients) clients="$2"; shift 2 ;;
        -scenario) scenario="$2"; shift 2 ;;
        -settle) settle="$2"; shift 2 ;;
        -window) window="$2"; shift 2 ;;
        -task) task="$2"; shift 2 ;;
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

# The baseline's trunk tree, since only trunk/ goes into the image: the commit's
# own, or HEAD's with the commit's trunk change merged out (a three-way merge of
# the trunk trees from the commit to its parent, onto HEAD). Merging only trunk
# lets a commit that also changed tools, which later commits changed again, revert.
rev="${baseline:-$revert}"
commit="$(git rev-parse --verify --quiet "$rev^{commit}")" || usage "Unknown rev $rev"
if [[ -n "$baseline" ]]; then
    tree="$(git rev-parse --verify --quiet "$commit:trunk")" || usage "$rev has no trunk"
else
    git rev-parse --verify --quiet "$commit^:trunk" >/dev/null || usage "$rev has no parent to revert to"
    tree="$(git merge-tree --write-tree --merge-base="$commit:trunk" HEAD:trunk "$commit^:trunk" | head -1)" ||
        usage "$rev does not revert cleanly from HEAD"
fi
image="srs-perf-ab:$task-$(git rev-parse --short "$commit")"
echo "baseline tree $tree"
echo "baseline image $image"
if [[ "$dry_run" == 1 ]]; then exit 0; fi
[[ "$clients" -gt 0 ]] || usage "Give -clients, such as the baseline's 90% hold count"

for c in $(docker ps -a --format '{{.Names}}'); do
    case "$c" in
        srs-perf-srs|srs-perf-load|srs-perf-ab-*) echo "Performance container $c exists; remove it first" >&2; exit 1 ;;
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
git archive --format=tar --add-file=cmd/perf/Dockerfile.ab --add-file=.dockerignore --prefix=trunk/ "$tree" |
    docker build -f Dockerfile.ab -t "$image" -

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
# Each SRS on its own core; both load containers share the load cores.
SRS_PERF_NAME=srs-perf-ab-baseline SRS_PERF_SRS_IMAGE="$image" SRS_PERF_SRS_CPUS=1 \
    SRS_PERF_LOAD_CPUS=3-9 SRS_PERF_SCENARIO="$scenario" SRS_PERF_OUT="$out" \
    bash skills/internal-perf-for-srs/scripts/perf-up.sh
SRS_PERF_NAME=srs-perf-ab-candidate SRS_PERF_SRS_IMAGE=srs-perf:local SRS_PERF_SRS_CPUS=2 \
    SRS_PERF_LOAD_CPUS=3-9 SRS_PERF_SCENARIO="$scenario" SRS_PERF_OUT="$out" \
    bash skills/internal-perf-for-srs/scripts/perf-up.sh
for side in baseline candidate; do
    docker cp cmd/perf/out/srs-perf-linux "srs-perf-ab-$side-load:/usr/local/bin/srs-perf"
done

# Both holds start together and share the -ab-run id, so compare pairs their
# seconds by wall-clock time.
pids=()
for side in baseline candidate; do
    docker exec "srs-perf-ab-$side-load" srs-perf run -scenario "$scenario" -mode hold \
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
    --arg candidate "$(git describe --always --dirty)" \
    --argjson clients "$clients" --argjson settle "$settle" --argjson window "$window" '{
        ab_run: $run, clients: $clients, settle: $settle, window: $window,
        baseline: {mode: $mode, rev: $rev, commit: $commit, tree: $tree, image: $image, cpuset: "1"},
        candidate: {worktree: $candidate, image: "srs-perf:local", cpuset: "2"},
        load_cpuset: "3-9",
        results: [$a[0], $b[0]]
    }' > "$out/$run/ab.json"
bin/srs-perf compare "$old" "$new" > "$out/$run/verdict.txt"
cat "$out/$run/verdict.txt"
echo "A/B output $out/$run"
