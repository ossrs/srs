#!/bin/bash
# Run unit tests for the proxy server (cmd/ and internal/ packages).
set -e

usage() {
  cat <<'EOF'
Run unit tests for the proxy server (cmd/ and internal/ packages).

Usage:
  proxy-utest.sh              # run tests
  proxy-utest.sh --coverage   # run tests and print per-function coverage
  proxy-utest.sh -c           # short form of --coverage
EOF
}

COVERAGE=0
for arg in "$@"; do
  case "$arg" in
    -c|--coverage) COVERAGE=1 ;;
    -h|--help) usage; exit 0 ;;
    *)
      echo "Error: unknown argument: $arg" >&2
      usage >&2
      exit 2
      ;;
  esac
done

SCRIPT_DIR="$(cd -P "$(dirname "$0")" && pwd)"
# Navigate: scripts/ -> srs-develop/ -> skills/ -> .openclaw/ -> srs
WORKSPACE="$(cd -P "$SCRIPT_DIR/../../../.." && pwd)"

if [[ ! -f "$WORKSPACE/go.mod" ]]; then
  echo "Error: go.mod not found in WORKSPACE: $WORKSPACE" >&2
  exit 1
fi

cd "$WORKSPACE"

PACKAGES=(./cmd/... ./internal/...)

if [[ $COVERAGE -eq 1 ]]; then
  COVER_FILE="$(mktemp -t proxy-utest-coverage.XXXXXX.out)"
  trap 'rm -f "$COVER_FILE"' EXIT
  echo "Running proxy unit tests with coverage in: $WORKSPACE"
  go test "${PACKAGES[@]}" -v -coverprofile="$COVER_FILE" -coverpkg=./cmd/...,./internal/...
  echo
  echo "=== Coverage (per function) ==="
  go tool cover -func="$COVER_FILE"
else
  echo "Running proxy unit tests in: $WORKSPACE"
  go test "${PACKAGES[@]}" -v
fi
