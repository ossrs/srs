#!/bin/bash
# Run unit tests for the proxy server (cmd/ and internal/ packages).
set -e

SCRIPT_DIR="$(cd -P "$(dirname "$0")" && pwd)"
# Navigate: scripts/ -> srs-develop/ -> skills/ -> .openclaw/ -> srs
WORKSPACE="$(cd -P "$SCRIPT_DIR/../../../.." && pwd)"

if [[ ! -f "$WORKSPACE/go.mod" ]]; then
  echo "Error: go.mod not found in WORKSPACE: $WORKSPACE" >&2
  exit 1
fi

cd "$WORKSPACE"
echo "Running proxy unit tests in: $WORKSPACE"

go test ./cmd/... ./internal/... -v
