#!/bin/bash
# Profiles the Go proxy in one profile hold. It runs in the load container,
# which shares the proxy's loopback, where Go pprof listens (GO_PPROF in
# cmd/perf/env/proxy/proxy.env). The CPU, allocation, mutex and block profiles
# record together for 60s, then a 10s runtime trace, then the heap and the
# goroutines; the GC counts are taken before and after. Then the text outputs.
set -euo pipefail

out="$1"
mkdir -p "$out"
pprof_url="${SRS_PERF_PPROF_URL:-http://127.0.0.1:6060/debug/pprof}"
# The test shortens both; a profile hold has room for the defaults.
seconds="${SRS_PERF_GO_PROFILE_SECONDS:-60}"
trace_seconds="${SRS_PERF_GO_TRACE_SECONDS:-10}"
# One in 5 mutex contention events, and one block sample per 10us blocked.
mutex_rate=5
block_rate=10000

fetch() { curl -sSf --max-time $((seconds + 30)) -o "$out/$2" "$pprof_url/$1"; }

# The GC count and pause total so far, from the proxy's /debug/pprof/gc.
gc_counts() { curl -sSf "$pprof_url/gc"; }

# Mutex and block sampling cost the proxy, so they are on only while the
# profiles record, and off again whatever happens.
rates_off() { curl -sSf "$pprof_url/rates?mutex=0&block=0" >/dev/null; }
trap 'rates_off || true' EXIT

gc_before="$(gc_counts)"
curl -sSf "$pprof_url/rates?mutex=$mutex_rate&block=$block_rate" >/dev/null
fetch "profile?seconds=$seconds" cpu.pb.gz &
cpu_pid=$!
fetch "allocs?seconds=$seconds" allocs.pb.gz &
allocs_pid=$!
fetch "mutex?seconds=$seconds" mutex.pb.gz &
mutex_pid=$!
fetch "block?seconds=$seconds" block.pb.gz &
block_pid=$!
wait "$cpu_pid"
wait "$allocs_pid"
wait "$mutex_pid"
wait "$block_pid"
rates_off

# The trace after the profiles, as perf trace follows perf record for SRS: it
# shows the scheduler and the syscalls, and would add its own cost to the CPU
# profile.
fetch "trace?seconds=$trace_seconds" trace.out
fetch heap heap.pb.gz
fetch "goroutine?debug=1" goroutine.txt
gc_after="$(gc_counts)"
printf '{"before": %s, "after": %s}\n' "$gc_before" "$gc_after" > "$out/gc.json"

pprof -top "$out/cpu.pb.gz" > "$out/top-symbols.txt" 2>/dev/null
pprof -raw "$out/cpu.pb.gz" 2>/dev/null | stackcollapse-go.pl | flamegraph.pl > "$out/flamegraph.svg"
test -s "$out/flamegraph.svg"
grep -q 'srsx/internal/proxy' "$out/top-symbols.txt"
