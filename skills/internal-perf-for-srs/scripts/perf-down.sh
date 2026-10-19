#!/bin/bash
set -euo pipefail

name="${SRS_PERF_NAME:-srs-perf}"
docker rm -f "$name-load" "$name-srs" >/dev/null 2>&1 || true
