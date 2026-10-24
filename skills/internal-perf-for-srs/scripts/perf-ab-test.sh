#!/bin/bash
# Tests perf-ab.sh: its options, the baseline tree of -revert, and a short A/B of
# HEAD against itself, whose paired CI must contain 0, with no srs-perf-ab image
# or container left. Takes a few minutes: one SRS build and a 30s A/B window.
#
#   perf-ab-test.sh [-target srs|proxy]
#
# -target proxy tests the proxy A/B instead: the baseline proxy from a git tree,
# each side with its own proxy, origin and load cpusets, the same 30s window.
set -uo pipefail

cd "$(dirname "$0")/../../.."
ab=skills/internal-perf-for-srs/scripts/perf-ab.sh
target=srs
case "${1:-}" in
    "") ;;
    -target) target="${2:-}" ;;
    *) echo "Usage: perf-ab-test.sh [-target srs|proxy]" >&2; exit 2 ;;
esac
[[ "$target" == srs || "$target" == proxy ]] || { echo "Invalid -target, want srs or proxy" >&2; exit 2; }

failed=0
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; failed=1; }
check() {
    local name="$1"; shift
    if "$@"; then pass "$name"; else fail "$name"; fi
}
exit_code() {
    local want="$1"; shift
    bash "$ab" "$@" >/dev/null 2>&1
    [[ $? == "$want" ]]
}
left_over() {
    docker images --format '{{.Repository}}' | grep -qx 'srs-perf-ab' ||
        docker ps -a --format '{{.Names}}' | grep -q '^srs-perf-ab-'
}

check "script exists" test -f "$ab"
check "no baseline is refused" exit_code 2 -task p4.10-test
check "both -baseline and -revert are refused" exit_code 2 -baseline HEAD -revert HEAD
check "an unknown rev is refused" exit_code 2 -baseline no-such-rev-p4.10 -dry-run
check "a bad task name is refused" exit_code 2 -baseline HEAD -task 'Bad Name' -dry-run
check "a bad window is refused" exit_code 2 -baseline HEAD -window 0 -dry-run
check "an unknown target is refused" exit_code 2 -target edge -baseline HEAD -dry-run

if [[ "$target" == srs ]]; then
    # The baseline image builds the same SRS as srs-perf:local, from another tree.
    configure_line() { grep -A1 -F './configure ' "$1"; }
    check "Dockerfile.ab configures like the perf image" \
        test "$(configure_line cmd/perf/Dockerfile.ab)" = "$(configure_line cmd/perf/Dockerfile)"

    # -dry-run prints the baseline's trunk tree and image tag, and builds nothing.
    # Only trunk/ goes into the image, so the baseline is a trunk tree.
    dry="$(bash "$ab" -revert HEAD -task p4.10-test -dry-run 2>&1)"
    check "-revert HEAD builds the parent's trunk" grep -qx "baseline tree $(git rev-parse 'HEAD^:trunk')" <<<"$dry"
    check "the image is srs-perf-ab:<task>-<rev>" \
        grep -qx "baseline image srs-perf-ab:p4.10-test-$(git rev-parse --short HEAD)" <<<"$dry"
    dry="$(bash "$ab" -baseline HEAD -task p4.10-test -dry-run 2>&1)"
    check "-baseline HEAD builds HEAD's trunk" grep -qx "baseline tree $(git rev-parse 'HEAD:trunk')" <<<"$dry"

    # -revert of the latest commit that changed trunk/: only its trunk change is
    # taken out, even when later commits changed its other files, such as a squashed
    # commit of tools and an SRS fix.
    fix="$(git log -1 --format=%H -- trunk)"
    dry="$(bash "$ab" -revert "$fix" -task p4.10-test -dry-run 2>&1)"
    reverted="$(sed -n 's/^baseline tree //p' <<<"$dry")"
    check "-revert of the last trunk commit succeeds" test -n "$reverted"
    trunk_change_out() {
        [[ -n "$reverted" ]] &&
            diff <(git diff "$reverted" 'HEAD:trunk') <(git diff "$fix^:trunk" "$fix:trunk") >/dev/null
    }
    check "-revert takes out exactly that commit's trunk change" trunk_change_out
    task=p4.10-test
    ab_args=(-baseline HEAD -task "$task" -clients 1000 -window 30)
else
    # The baseline image builds the proxy as srs-perf:local's proxy stage does.
    proxy_build() { grep -F 'go build -o /out/srs-proxy' "$1"; }
    check "Dockerfile.ab-proxy builds the proxy like the perf image" \
        test "$(proxy_build cmd/perf/Dockerfile.ab-proxy)" = "$(proxy_build cmd/perf/Dockerfile)"
    check "a scenario the proxy does not relay is refused" \
        exit_code 2 -target proxy -scenario rtmp-rtsp -baseline HEAD -dry-run

    # The proxy is built from the Go module's files of the baseline tree.
    proxy_paths=(go.mod go.sum cmd/proxy internal)
    dry="$(bash "$ab" -target proxy -baseline HEAD -task p7.5-test -dry-run 2>&1)"
    check "-baseline HEAD builds HEAD's tree" grep -qx "baseline tree $(git rev-parse 'HEAD^{tree}')" <<<"$dry"
    check "the proxy image is srs-perf-ab:<task>-<rev>" \
        grep -qx "baseline image srs-perf-ab:p7.5-test-$(git rev-parse --short HEAD)" <<<"$dry"
    dry="$(bash "$ab" -target proxy -revert HEAD -task p7.5-test -dry-run 2>&1)"
    reverted="$(sed -n 's/^baseline tree //p' <<<"$dry")"
    same_proxy_as_parent() {
        [[ -n "$reverted" ]] && git diff --quiet "$reverted" 'HEAD^' -- "${proxy_paths[@]}"
    }
    check "-revert HEAD builds the parent's proxy" same_proxy_as_parent

    # -revert of the latest commit that changed the proxy: only its proxy change
    # is taken out, even when later commits changed its tool files, which a
    # merge of the whole tree would conflict on.
    fix="$(git log -1 --format=%H -- "${proxy_paths[@]}")"
    dry="$(bash "$ab" -target proxy -revert "$fix" -task p7.5-test -dry-run 2>&1)"
    reverted="$(sed -n 's/^baseline tree //p' <<<"$dry")"
    check "-revert of the last proxy commit succeeds" test -n "$reverted"
    proxy_change_out() {
        [[ -n "$reverted" ]] &&
            diff <(git diff "$reverted" HEAD -- "${proxy_paths[@]}") \
                <(git diff "$fix^" "$fix" -- "${proxy_paths[@]}") >/dev/null
    }
    check "-revert takes out exactly that commit's proxy change" proxy_change_out
    task=p7.5-test
    ab_args=(-target proxy -baseline HEAD -task "$task" -clients 1000 -window 30)
fi
check "a dry run leaves no image" test -z "$(docker images -q "srs-perf-ab:$task-*")"

# A short A/B of HEAD against the worktree's image: the same server on both sides.
log="$(mktemp -t perf-ab-test)"
bash "$ab" "${ab_args[@]}" 2>&1 | tee "$log"
code=${PIPESTATUS[0]}
check "the A/B run passes" test "$code" = 0
dir="$(sed -n 's/^A\/B output //p' "$log" | tail -1)"
check "it prints its output directory" test -d "$dir"
if [[ -d "$dir" ]]; then
    check "ab.json has both sides with one ab_run" \
        jq -e '(.results | length) == 2 and .results[0].ab_run != "" and .results[0].ab_run == .results[1].ab_run' "$dir/ab.json"
    check "ab.json names the baseline and the candidate" \
        jq -e '.baseline.image != "" and .baseline.tree != "" and .candidate.image == "srs-perf:local"' "$dir/ab.json"
    check "both sides are healthy" jq -e '[.results[].health.healthy] == [true, true]' "$dir/ab.json"
    check "both sides count their 30s window" \
        jq -e '[.results[].window.samples] | all(. >= 29)' "$dir/ab.json"
    check "ab.json records the target" jq -e --arg t "$target" '.target == $t' "$dir/ab.json"
    if [[ "$target" == proxy ]]; then
        check "both sides measured the proxy with one origin" \
            jq -e '[.results[].setup | .target == "proxy" and .origins == 1] == [true, true]' "$dir/ab.json"
        check "each side has its own proxy, origin and load cores" \
            jq -e '.baseline.cpuset == "1" and .baseline.origin_cpuset == "2" and .baseline.load_cpuset == "5-6"
                and .candidate.cpuset == "3" and .candidate.origin_cpuset == "4" and .candidate.load_cpuset == "7-8"' "$dir/ab.json"
    fi
    # compare refuses two setups that differ, such as load cpusets of different sizes.
    check "both sides have the same setup" jq -e '.results[0].setup == .results[1].setup' "$dir/ab.json"
    check "the verdict is paired per second" grep -q 'paired per second (A/B' "$dir/verdict.txt"
    check "HEAD against itself: the CI contains 0" grep -q 'CI contains 0' "$dir/verdict.txt"
fi
nothing_left() { ! left_over; }
check "no srs-perf-ab image or container left" nothing_left
rm -f "$log"

exit "$failed"
