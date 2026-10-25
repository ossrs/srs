#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")/../../.."
make perf-env-check

out=cmd/perf/out/env-check
test -s "$out/perf.data"
test -s "$out/flamegraph.svg"
test -s "$out/top-symbols.txt"
grep -Eq 'Srs[A-Za-z_]+|srs_' "$out/top-symbols.txt"
