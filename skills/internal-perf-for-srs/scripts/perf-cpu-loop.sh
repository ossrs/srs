#!/bin/bash
set -euo pipefail

start=$(date +%s%N)
awk 'BEGIN { for (i = 0; i < 50000000; i++) sum += i % 97; if (sum < 0) exit 1 }'
end=$(date +%s%N)
awk -v start="$start" -v end="$end" 'BEGIN { printf "%.3f\n", (end - start) / 1000000000 }'
