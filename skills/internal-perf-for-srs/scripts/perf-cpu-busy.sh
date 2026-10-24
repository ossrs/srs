#!/bin/bash
set -euo pipefail

for _ in 1 2 3 4 5 6 7 8; do
    yes >/dev/null &
done
wait
