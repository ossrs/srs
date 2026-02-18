#!/bin/bash
# Verify ST changes by building and running unit tests.
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# Navigate: scripts/ -> st-develop/ -> skills/ -> openclaw/ -> srs/
SRS_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
ST_DIR="$SRS_ROOT/trunk/3rdparty/st-srs"

echo "ST source: $ST_DIR"
cd "$ST_DIR" && make darwin-debug-utest && ./obj/st_utest
