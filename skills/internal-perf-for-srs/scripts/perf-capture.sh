#!/bin/bash
set -euo pipefail

perf record -e cpu-clock -F 99 -g -p 1 -o /out/perf.data -- sleep 10
perf report -i /out/perf.data --stdio --no-children --sort symbol \
    --percent-limit 0 > /out/top-symbols.txt
perf script -i /out/perf.data | stackcollapse-perf.pl | flamegraph.pl \
    > /out/flamegraph.svg
