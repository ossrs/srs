#!/bin/bash
set -euo pipefail

name="${SRS_PERF_NAME:-srs-perf}"
# The forward target and the edge exist only for the delay probes; the proxy
# and its origins only for -target proxy.
docker rm -f "$name-load" "$name-forward" "$name-edge" "$name-srs" \
    "$name-origin1" "$name-origin2" "$name-origin3" "$name-origin4" "$name-proxy" >/dev/null 2>&1 || true
