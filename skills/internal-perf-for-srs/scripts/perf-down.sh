#!/bin/bash
set -euo pipefail

docker rm -f srs-perf-load srs-perf-srs >/dev/null 2>&1 || true
