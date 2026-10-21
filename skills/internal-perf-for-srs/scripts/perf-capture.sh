#!/bin/bash
set -euo pipefail

# 999 Hz like the hold profiles: one publisher of the flat 200 kbps source keeps
# SRS almost idle, and 99 Hz caught only 4 to 7 samples in 10s.
perf record -e cpu-clock -F 999 -g -p 1 -o /out/perf.data -- sleep 10
perf report -i /out/perf.data --stdio --no-children --sort symbol \
    --percent-limit 0 > /out/top-symbols.txt
perf script -i /out/perf.data | stackcollapse-perf.pl | flamegraph.pl \
    > /out/flamegraph.svg
