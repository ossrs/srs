#!/bin/bash
# Checks one perf-run.sh run of a scenario the way a verification loop needs:
# run.json is valid with a capacity, the ramp's steps up to the capacity and
# every hold were healthy and valid, and each profile hold has a flame graph,
# syscall counts and SRS symbols in its symbol table. It prints the scenario's
# verification row and the top SRS symbols of each profile hold, for ranking.
# A delays run has no capacity or profile: its probes are checked instead.
#
# Usage: perf-verify.sh <run dir>, such as cmd/perf/out/<UTC>-rtmp-flv.
# Exits 0 when every check passes, 1 when one fails, 2 on bad usage.
set -uo pipefail

dir="${1:-}"
if [[ -z "$dir" || ! -d "$dir" ]]; then
    echo "Usage: $0 <run dir>" >&2
    exit 2
fi
failed=0
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; failed=1; }
# sick fails a health check: the capacity, a ramp step or a hold.
healthy=yes
sick() { fail "$1"; healthy=no; }

run="$dir/run.json"
if ! jq -e '.results | type == "array"' "$run" >/dev/null 2>&1; then
    fail "run.json is valid JSON with results"
    exit 1
fi
scenario=$(jq -r '.scenario' "$run")
pass "run.json of $scenario"

# The delays scenario has no capacity, holds or profiles: each repeat is one
# probe, which must be healthy and valid with samples of every delay. Print
# the delays and the lag each path adds, and a row with no capacity.
if [[ "$scenario" == delays ]]; then
    probes=$(jq -r '[.results[] | select(.scenario.parameters.mode == "probe")] | length' "$run")
    if [[ "$probes" -ge 1 ]]; then pass "probe results: $probes"; else sick "probe results"; fi
    for ((i = 0; i < probes; i++)); do
        n=$((i + 1))
        probe=$(jq -c "[.results[] | select(.scenario.parameters.mode == \"probe\")][$i]" "$run")
        if [[ $(jq -r '.health.healthy' <<< "$probe") == true ]]; then pass "probe $n healthy"; else sick "probe $n healthy"; fi
        if [[ $(jq -r '.validity.valid' <<< "$probe") == true ]]; then pass "probe $n valid"; else sick "probe $n valid"; fi
        for k in pause_millis unpause_millis origin_lag_millis forward_lag_millis edge_play_lag_millis edge_publish_lag_millis; do
            if [[ $(jq -r ".delays.$k.samples // 0" <<< "$probe") -gt 0 ]]; then
                pass "probe $n $k samples"
            else
                sick "probe $n $k samples"
            fi
        done
        echo "Delays of probe $n (mean, max, samples):"
        # f prints a value in milliseconds with one decimal.
        jq -r 'def f: . * 10 | round / 10 | tostring | if test("\\.") then . else . + ".0" end;
            .delays | ([["pause", .pause_millis], ["unpause", .unpause_millis],
                ["origin lag", .origin_lag_millis], ["forward lag", .forward_lag_millis],
                ["edge play lag", .edge_play_lag_millis], ["edge publish lag", .edge_publish_lag_millis]][]
                | "  \(.[0]) \(.[1].mean // 0 | f) ms (max \(.[1].max // 0 | f), \(.[1].samples // 0) samples)"),
            "  added: forward \(.forward_added_millis // 0 | f) ms, edge play \(.edge_play_added_millis // 0 | f) ms, edge publish \(.edge_publish_added_millis // 0 | f) ms"' <<< "$probe"
    done
    echo "| \`$scenario\` | probe, no capacity | $healthy | no profile |"
    if [[ "$failed" == 0 ]]; then echo "Verified $scenario"; else echo "Not verified $scenario"; fi
    exit "$failed"
fi

# The ramp: a capacity, valid, and every step up to the capacity healthy.
ramp=$(jq -c '[.results[] | select(.scenario.parameters.mode == "ramp")][0] // empty' "$run")
capacity=0
if [[ -z "$ramp" ]]; then
    fail "ramp result"
else
    capacity=$(jq -r '.capacity // 0' <<< "$ramp")
    if [[ "$capacity" =~ ^[1-9][0-9]*$ ]]; then pass "capacity $capacity"; else sick "capacity"; capacity=0; fi
    # The ramp's own validity is its stop step's; only the steps up to the
    # capacity must be valid, and a load-bound stop is noted in the row.
    while read -r clients ok valid; do
        if [[ "$clients" -le "$capacity" ]]; then
            if [[ "$ok" == true ]]; then pass "ramp step $clients healthy"; else sick "ramp step $clients healthy"; fi
            if [[ "$valid" == true ]]; then pass "ramp step $clients valid"; else fail "ramp step $clients valid"; fi
        fi
    done < <(jq -r '.steps[] | "\(.clients) \(.health.healthy) \(.validity.valid)"' <<< "$ramp")
fi

# The measuring holds: three, each healthy and valid.
holds=$(jq -r '[.results[] | select(.scenario.parameters.mode == "hold")] | length' "$run")
if [[ "$holds" -ge 3 ]]; then pass "holds: $holds"; else sick "holds: $holds of 3"; fi
while read -r clients ok valid; do
    if [[ "$ok" == true ]]; then pass "hold $clients healthy"; else sick "hold $clients healthy"; fi
    if [[ "$valid" == true ]]; then pass "hold $clients valid"; else sick "hold $clients valid"; fi
done < <(jq -r '.results[] | select(.scenario.parameters.mode == "hold")
    | "\(.steps[0].clients) \(.health.healthy) \(.validity.valid)"' "$run")

# The noise: the CPU per client change between the two 50% holds. It does
# not fail a run, but at 10% or more a comparison on this scenario needs an
# A/B or a rerun with a 180s window, so the check notes it.
noise=$(jq -r '(.noise // [])[] | "\(.clients) \(.change)"' "$run")
if [[ -z "$noise" ]]; then
    fail "noise between the two 50% holds"
fi
while read -r clients change; do
    [[ -z "$clients" ]] && continue
    text=$(awk -v c="$change" 'BEGIN { printf "%+.1f%%", c * 100 }')
    if awk -v c="$change" 'BEGIN { exit !(c >= 0.1 || c <= -0.1) }'; then
        echo "NOTE noise at $clients: $text, 10% or more; compare this scenario by an A/B or a 180s rerun"
    else
        echo "noise at $clients: $text"
    fi
done <<< "$noise"

# srs_symbols FILE prints "<symbol> <share>" for the SRS and State Threads
# symbols of a perf report symbol table, in its order, arguments left out.
srs_symbols() {
    awk 'match($0, /^ +[0-9.]+% +\[[.k]\] /) {
        share = $1; name = substr($0, RLENGTH + 1)
        sub(/  +.*$/, "", name); sub(/\(.*$/, "", name)
        if (name ~ /^(Srs|srs_|_st_|st_)/) print name " " share
    }' "$1"
}

# The profile holds: a flame graph, syscall counts and SRS symbols each.
srs_ok=1
for p in 50 90; do
    prof="$dir/repeat-1/profile-$p"
    if [[ -s "$prof/flamegraph.svg" ]]; then pass "profile-$p flamegraph"; else fail "profile-$p flamegraph"; fi
    if grep -qE '^ +[a-z_0-9]+ +[0-9]+ ' "$prof/syscalls.txt" 2>/dev/null; then
        pass "profile-$p syscalls"
    else
        fail "profile-$p syscalls"
    fi
    if [[ -n "$(srs_symbols "$prof/top-symbols.txt" 2>/dev/null)" ]]; then
        pass "profile-$p SRS symbols"
    else
        fail "profile-$p SRS symbols"
        srs_ok=0
    fi
done

for p in 50 90; do
    echo "Top SRS symbols at profile-$p (self share):"
    srs_symbols "$dir/repeat-1/profile-$p/top-symbols.txt" 2>/dev/null | head -10 | sed 's/^/  /'
done

# The verification row: scenario, capacity, healthy, SRS symbols. The top
# bottleneck and the task are written by the rank step.
# A ramp whose last step is its capacity stopped at the scenario's client
# cap; one stopped by a failure or the 90% CPU rule has a step above it.
cap_text=$(awk -v n="$capacity" 'BEGIN {
    s = ""
    while (length(n) > 3) { s = "," substr(n, length(n) - 2) s; n = substr(n, 1, length(n) - 3) }
    print n s
}')
if [[ -n "$ramp" && "$capacity" != 0 && $(jq -r '.steps[-1].clients' <<< "$ramp") == "$capacity" ]]; then
    cap_text="$cap_text (cap)"
elif [[ -n "$ramp" && $(jq -r '.validity.reason // ""' <<< "$ramp") == load-bound ]]; then
    cap_text="$cap_text (load-bound stop)"
fi
symbols=no
[[ "$srs_ok" == 1 ]] && symbols=yes
echo "| \`$scenario\` | $cap_text | $healthy | $symbols |"

if [[ "$failed" == 0 ]]; then echo "Verified $scenario"; else echo "Not verified $scenario"; fi
exit "$failed"
