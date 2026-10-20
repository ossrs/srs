#!/bin/bash
# Tests perf-verify.sh on fixture run directories in /tmp/srs-perf-verify-test:
# a good run passes, and each broken one fails on its own check.
set -uo pipefail

cd "$(dirname "$0")"
failed=0
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; failed=1; }

base=/tmp/srs-perf-verify-test
rm -rf /tmp/srs-perf-verify-test
mkdir -p "$base"

# step CLIENTS HEALTHY gives one step of a result.
step() {
    jq -n --argjson c "$1" --argjson h "$2" '{mode:"x", clients:$c,
        window:{samples:3, srs_cpu_percent:{mean:($c/100), sd:0.5}},
        health:{healthy:$h}, validity:{valid:true}}'
}

# make_run NAME: a good run of rtmp-flv, capacity 3200, three healthy holds
# and two profile holds with SRS symbols.
make_run() {
    local dir="$base/$1"
    mkdir -p "$dir/repeat-1/profile-50" "$dir/repeat-1/profile-90"
    local ramp holds
    ramp=$(jq -n --argjson s "[$(step 800 true),$(step 1600 true),$(step 3200 true),$(step 6400 false)]" \
        '{scenario:{name:"rtmp-flv", parameters:{mode:"ramp"}}, steps:$s,
          health:{healthy:false, reason:"cpu"}, validity:{valid:true}, capacity:3200, cpu_per_client:0.01}')
    holds=""
    for c in 1600 1600 2880; do
        holds="$holds,$(jq -n --argjson s "[$(step $c true)]" --argjson c $c \
            '{scenario:{name:"rtmp-flv", parameters:{mode:"hold", clients:($c|tostring)}}, steps:$s,
              health:{healthy:true}, validity:{valid:true}, capacity:0, cpu_per_client:0.005}')"
    done
    jq -n --argjson r "[$ramp$holds]" '{scenario:"rtmp-flv", run:"fixture", repeat:1,
        noise:[{clients:1600, cpu_per_client:[0.005, 0.00501], change:0.002}], results:$r}' \
        > "$dir/run.json"
    for p in 50 90; do
        printf '<svg><title>SrsLiveConsumer::enqueue (10 samples)</title></svg>\n' > "$dir/repeat-1/profile-$p/flamegraph.svg"
        # perf report's layout: a header, each symbol padded with its IPC
        # columns, and its call graph under it.
        {
            printf '# Samples: 27K of event '\''cpu-clock'\''\n#\n# Overhead  Symbol    IPC   [IPC Coverage]\n#\n'
            printf '    20.10%%  [k] __arch_copy_to_user                        -      -            \n            |\n            ---el0_svc\n\n'
            printf '    12.50%%  [.] _st_epoll_dispatch                         -      -            \n            |\n            ---_st_idle_thread_start\n\n'
            printf '     8.20%%  [.] SrsHttpFileServer::serve(ISrsHttpResponseWriter*, ISrsHttpMessage*)   -      -            \n\n'
            printf '     4.00%%  [.] srs_rtmp_write_chunk_header(x)             -      -            \n\n'
            printf '     3.00%%  [.] malloc                                     -      -            \n'
        } > "$dir/repeat-1/profile-$p/top-symbols.txt"
        printf '   writev     1000      0\n   epoll_pwait   10    0\n' > "$dir/repeat-1/profile-$p/syscalls.txt"
    done
}

# expect NAME WANT TEXT: perf-verify.sh on NAME exits WANT (0 or 1), and its
# output has TEXT.
expect() {
    local out code
    out=$(bash perf-verify.sh "$base/$1" 2>&1)
    code=$?
    if [[ "$code" == "$2" ]]; then pass "$1 exits $2"; else fail "$1 exits $2, got $code: $out"; fi
    if grep -qF -- "$3" <<< "$out"; then pass "$1 prints $3"; else fail "$1 prints $3: $out"; fi
}

make_run good
expect good 0 "| \`rtmp-flv\` | 3,200 | yes | yes |"
expect good 0 "SrsHttpFileServer::serve 8.20%"

expect good 0 "noise at 1600: +0.2%"

# Noise of 10% or more between the two 50% holds does not fail the run, but
# the check says so, since a later comparison on it needs an A/B or a rerun.
make_run noisy
jq '.noise[0].change = 0.1009' "$base/noisy/run.json" > "$base/noisy/r" && mv "$base/noisy/r" "$base/noisy/run.json"
expect noisy 0 "NOTE noise at 1600: +10.1%"
make_run quiet-down
jq '.noise[0].change = -0.099' "$base/quiet-down/run.json" > "$base/quiet-down/r" && mv "$base/quiet-down/r" "$base/quiet-down/run.json"
out=$(bash perf-verify.sh "$base/quiet-down" 2>&1)
if grep -qF "noise at 1600: -9.9%" <<< "$out" && ! grep -q "NOTE noise" <<< "$out"; then
    pass "quiet-down prints its noise without a note"
else
    fail "quiet-down prints its noise without a note: $out"
fi
make_run no-noise
jq '.noise = []' "$base/no-noise/run.json" > "$base/no-noise/r" && mv "$base/no-noise/r" "$base/no-noise/run.json"
expect no-noise 1 "FAIL noise"

make_run no-capacity
jq '.results[0].capacity = 0' "$base/no-capacity/run.json" > "$base/no-capacity/r" && mv "$base/no-capacity/r" "$base/no-capacity/run.json"
expect no-capacity 1 "FAIL capacity"

make_run unhealthy-step
jq '.results[0].steps[1].health.healthy = false' "$base/unhealthy-step/run.json" > "$base/unhealthy-step/r" && mv "$base/unhealthy-step/r" "$base/unhealthy-step/run.json"
expect unhealthy-step 1 "FAIL ramp step 1600 healthy"

make_run unhealthy-hold
jq '.results[3].health.healthy = false' "$base/unhealthy-hold/run.json" > "$base/unhealthy-hold/r" && mv "$base/unhealthy-hold/r" "$base/unhealthy-hold/run.json"
expect unhealthy-hold 1 "FAIL hold 2880 healthy"

make_run invalid
jq '.results[1].validity.valid = false' "$base/invalid/run.json" > "$base/invalid/r" && mv "$base/invalid/r" "$base/invalid/run.json"
expect invalid 1 "FAIL hold 1600 valid"

make_run bad-json
printf '{"results": [' > "$base/bad-json/run.json"
expect bad-json 1 "FAIL run.json"

make_run no-srs-symbols
printf '    50.00%%  [k] __arch_copy_to_user\n    10.00%%  [.] malloc\n' > "$base/no-srs-symbols/repeat-1/profile-90/top-symbols.txt"
expect no-srs-symbols 1 "FAIL profile-90 SRS symbols"

make_run no-flamegraph
rm "$base/no-flamegraph/repeat-1/profile-50/flamegraph.svg"
expect no-flamegraph 1 "FAIL profile-50 flamegraph"

make_run no-syscalls
: > "$base/no-syscalls/repeat-1/profile-90/syscalls.txt"
expect no-syscalls 1 "FAIL profile-90 syscalls"

make_run reached-cap
jq '.results[0].health = {healthy:true} | .results[0].steps[3].health.healthy = true | .results[0].capacity = 6400' \
    "$base/reached-cap/run.json" > "$base/reached-cap/r" && mv "$base/reached-cap/r" "$base/reached-cap/run.json"
expect reached-cap 0 "| \`rtmp-flv\` | 6,400 (cap) | yes | yes |"

# A ramp stopped by SRS's 90% CPU rule is healthy too, but its last step is
# above the capacity, so it did not reach the cap.
make_run cpu-stop
jq '.results[0].health = {healthy:true} | .results[0].steps[3].health.healthy = true
    | .results[0].steps[3].window.srs_cpu_percent.mean = 95' \
    "$base/cpu-stop/run.json" > "$base/cpu-stop/r" && mv "$base/cpu-stop/r" "$base/cpu-stop/run.json"
expect cpu-stop 0 "| \`rtmp-flv\` | 3,200 | yes | yes |"

# A ramp whose failed step is load-bound is invalid as a whole, but its
# capacity comes from the valid steps below; the row says the stop was the
# load side's.
make_run load-bound-stop
jq '.results[0].validity = {valid:false, reason:"load-bound"}
    | .results[0].steps[3].validity = {valid:false, reason:"load-bound"}' \
    "$base/load-bound-stop/run.json" > "$base/load-bound-stop/r" && mv "$base/load-bound-stop/r" "$base/load-bound-stop/run.json"
expect load-bound-stop 0 "| \`rtmp-flv\` | 3,200 (load-bound stop) | yes | yes |"

# A step at or below the capacity that is not valid fails the run.
make_run invalid-step
jq '.results[0].steps[2].validity = {valid:false, reason:"load-bound"}' \
    "$base/invalid-step/run.json" > "$base/invalid-step/r" && mv "$base/invalid-step/r" "$base/invalid-step/run.json"
expect invalid-step 1 "FAIL ramp step 3200 valid"

make_run no-holds
jq '.results |= [.[0]]' "$base/no-holds/run.json" > "$base/no-holds/r" && mv "$base/no-holds/r" "$base/no-holds/run.json"
expect no-holds 1 "FAIL holds"

# The delays scenario has no capacity, holds or profiles: a run is one probe
# per repeat, healthy and valid, with samples of every delay. The check
# prints each delay and the added lags, and a row with no capacity.
dstat() { jq -n --argjson n "$1" --argjson m "$2" --argjson x "$3" '{samples:$n, mean:$m, sd:1, min:0, max:$x}'; }
make_delays() {
    local dir="$base/$1"
    mkdir -p "$dir"
    local d
    d=$(jq -n --argjson p "$(dstat 13 0.3 0.4)" --argjson u "$(dstat 13 239.3 460)" \
        --argjson o "$(dstat 504 179.4 363)" --argjson f "$(dstat 496 397 861)" \
        --argjson ep "$(dstat 504 179.7 363)" --argjson eu "$(dstat 507 388.4 849)" \
        '{pause_millis:$p, unpause_millis:$u, origin_lag_millis:$o, forward_lag_millis:$f,
          edge_play_lag_millis:$ep, edge_publish_lag_millis:$eu,
          forward_added_millis:217.6, edge_play_added_millis:0.4, edge_publish_added_millis:209.0}')
    jq -n --argjson d "$d" '{scenario:"delays", run:"fixture", repeat:1, results:[
        {scenario:{name:"delays", parameters:{mode:"probe"}}, steps:[{clients:5}],
         health:{healthy:true}, validity:{valid:true}, capacity:0, delays:$d}]}' > "$dir/run.json"
}
make_delays delays
expect delays 0 "| \`delays\` | probe, no capacity | yes | no profile |"
expect delays 0 "unpause 239.3 ms (max 460.0, 13 samples)"
expect delays 0 "added: forward 217.6 ms, edge play 0.4 ms, edge publish 209.0 ms"
expect delays 0 "Verified delays"

make_delays delays-unhealthy
jq '.results[0].health = {healthy:false, reason:"no edge sample"}' "$base/delays-unhealthy/run.json" > "$base/delays-unhealthy/r" && mv "$base/delays-unhealthy/r" "$base/delays-unhealthy/run.json"
expect delays-unhealthy 1 "FAIL probe 1 healthy"

make_delays delays-invalid
jq '.results[0].validity = {valid:false, reason:"load-bound"}' "$base/delays-invalid/run.json" > "$base/delays-invalid/r" && mv "$base/delays-invalid/r" "$base/delays-invalid/run.json"
expect delays-invalid 1 "FAIL probe 1 valid"

make_delays delays-no-samples
jq '.results[0].delays.forward_lag_millis.samples = 0' "$base/delays-no-samples/run.json" > "$base/delays-no-samples/r" && mv "$base/delays-no-samples/r" "$base/delays-no-samples/run.json"
expect delays-no-samples 1 "FAIL probe 1 forward_lag_millis samples"

make_delays delays-no-probe
jq '.results = []' "$base/delays-no-probe/run.json" > "$base/delays-no-probe/r" && mv "$base/delays-no-probe/r" "$base/delays-no-probe/run.json"
expect delays-no-probe 1 "FAIL probe results"

out=$(bash perf-verify.sh 2>&1); code=$?
if [[ "$code" == 2 ]]; then pass "no argument exits 2"; else fail "no argument exits 2, got $code: $out"; fi

rm -rf /tmp/srs-perf-verify-test
[[ "$failed" == 0 ]] && echo "All perf-verify checks passed" || echo "Some perf-verify checks failed"
exit "$failed"
