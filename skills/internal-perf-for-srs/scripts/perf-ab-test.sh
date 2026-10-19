#!/bin/bash
# Tests perf-ab.sh: its options, the baseline tree of -revert, and a short A/B of
# HEAD against itself, whose paired CI must contain 0, with no srs-perf-ab image
# or container left. Takes a few minutes: one SRS build and a 30s A/B window.
set -uo pipefail

cd "$(dirname "$0")/../../.."
ab=skills/internal-perf-for-srs/scripts/perf-ab.sh
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
check "a dry run leaves no image" test -z "$(docker images -q 'srs-perf-ab:p4.10-test-*')"

# A short A/B of HEAD against the worktree's image: the same SRS on both sides.
log="$(mktemp -t perf-ab-test)"
bash "$ab" -baseline HEAD -task p4.10-test -clients 1000 -window 30 2>&1 | tee "$log"
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
    check "the verdict is paired per second" grep -q 'paired per second (A/B' "$dir/verdict.txt"
    check "HEAD against itself: the CI contains 0" grep -q 'CI contains 0' "$dir/verdict.txt"
fi
nothing_left() { ! left_over; }
check "no srs-perf-ab image or container left" nothing_left
rm -f "$log"

exit "$failed"
