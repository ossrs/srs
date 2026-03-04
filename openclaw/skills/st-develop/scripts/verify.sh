#!/bin/bash
# Verify ST changes by building and running unit tests.
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# Navigate: scripts/ -> st-develop/ -> skills/ -> openclaw/ -> srs/
SRS_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
ST_DIR="$SRS_ROOT/trunk/3rdparty/st-srs"

if [[ ! -d "$ST_DIR" ]]; then
  echo "Error: ST_DIR does not exist: $ST_DIR" >&2
  exit 1
fi

echo "ST source: $ST_DIR"

CMAKE_DIR="$SRS_ROOT/cmake"
BUILD_DIR="$SRS_ROOT/cmake/build"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . --target st_utest

./st-build/st_utest
