#!/bin/bash
set -euo pipefail

name="${SRS_PERF_NAME:-srs-perf}"
# The forward target and the edge exist only for the delay probes.
docker rm -f "$name-load" "$name-forward" "$name-edge" "$name-srs" >/dev/null 2>&1 || true
