#!/bin/bash
set -euo pipefail

out="$1"
mkdir -p "$out"
perf record -e cpu-clock -F 999 -g -p 1 -o "$out/perf.data" -- sleep 60

# Count syscalls right after the record, so the 10s fall inside the 75s hold and
# not in its teardown. perf trace stops with its sleep; killed by timeout, it
# prints no summary.
if ! perf trace -s -p 1 -- sleep 10 > "$out/syscalls.txt" 2>&1 \
    || ! grep -Eq 'calls' "$out/syscalls.txt"; then
    timeout 10 strace -c -f -p 1 > "$out/syscalls.txt" 2>&1 || test "$?" -eq 124
fi
test -s "$out/syscalls.txt"

perf report -i "$out/perf.data" --stdio --no-children --sort symbol \
    --percent-limit 0 > "$out/top-symbols.txt"
perf script -i "$out/perf.data" | stackcollapse-perf.pl | flamegraph.pl \
    > "$out/flamegraph.svg"
test -s "$out/flamegraph.svg"
grep -Eq 'Srs[A-Za-z_]+|srs_' "$out/top-symbols.txt"
