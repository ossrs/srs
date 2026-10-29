#!/bin/bash
# The Chrome cases of lane I on the Windows test host: its Edge publishes and plays against the native srs.exe
# on the same host.
#
#   chrome-windows-test.sh [--wip]
#
# Opt in with SRS_TEST_WINDOWS_HOST; SRS_TEST_CHROME=0 skips. windows-host-run.sh syncs this checkout to
# the host, with --wip as a snapshot of the working tree, and runs the --host step in the host checkout:
#   - srs-windows-runner.sh build builds srs.exe, with its deps step first; both keep their builds.
#   - The --host-cases step runs chrome-lane-test.sh --cases-only there against trunk/objs/windows/srs.exe:
#     the WebRTC, loss, HTTP and codec cases in parallel on SRS_TEST_PORT_BASE (default 15600), 15700, 15800
#     and 15900, each with its own browser and srs.exe, as on macOS. Logs are in /tmp/srs-chrome-windows.
#
# The host needs Node.js 22.12+, Edge or Chrome, FFmpeg, jq and curl in Git Bash. Prints the build time,
# each case's log and result, and the LANE chrome line; the cases fail over 60 s. Exits 0 when passed or
# skipped, 1 when a case or the build failed, and 3 for bad arguments or a failed host setup.
set -eu
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
LOG_DIR=${CHROME_TEST_ROOT:-/tmp}/srs-chrome-windows

usage() {
  echo "Usage: $0 [--wip]" >&2
  exit 3
}

# On the Mac: sync the checkout to the host and run the --host step there.
mac_main() {
  local wip=$1 checkout status=0
  [[ "${SRS_TEST_CHROME:-1}" != 0 ]] || { echo 'SKIP SRS_TEST_CHROME=0'; exit 0; }
  [[ -n "${SRS_TEST_WINDOWS_HOST:-}" ]] || { echo 'SKIP windows: SRS_TEST_WINDOWS_HOST is not set'; exit 0; }
  checkout=$(cd "$SCRIPT_DIR/../../.." && pwd)
  bash "$SCRIPT_DIR/windows-host-run.sh" $wip "$checkout" "$SCRIPT_DIR/chrome-windows-test.sh" --host || status=$?
  # windows-host-run.sh exits 2 after its own SKIP line.
  [[ $status -ne 2 ]] || exit 0
  exit "$status"
}

# In Git Bash on the host, in the host checkout: build srs.exe, then run the cases.
host_main() {
  local here=$PWD/skills/srs-develop/scripts started
  [[ -f "$here/srs-windows-runner.sh" ]] || { echo "SETUP_FAIL: $PWD is not an SRS checkout"; exit 3; }
  mkdir -p "$LOG_DIR"
  started=$SECONDS
  if ! bash "$here/srs-windows-runner.sh" build >"$LOG_DIR/build.log" 2>&1; then
    tail -n 20 "$LOG_DIR/build.log"
    echo "FAIL srs.exe build of $(git rev-parse HEAD) (log: $LOG_DIR/build.log)"
    exit 1
  fi
  echo "BUILD windows $(git rev-parse HEAD) $((SECONDS - started))s"
  host_cases
}

# In Git Bash on the host, in the host checkout: the Chrome cases against the native srs.exe.
host_cases() {
  local here=$PWD/skills/srs-develop/scripts status=0
  [[ -f "$here/chrome-lane-test.sh" ]] || { echo "SETUP_FAIL: $PWD is not an SRS checkout"; exit 3; }
  [[ -x trunk/objs/windows/srs.exe ]] || { echo 'FAIL build trunk/objs/windows/srs.exe first'; exit 1; }
  CHROME_SRS_BIN=objs/windows/srs.exe CHROME_TEST_ROOT=$LOG_DIR bash "$here/chrome-lane-test.sh" --cases-only 2>&1 \
    | tr -d '\r'
  status=${PIPESTATUS[0]}
  exit "$status"
}

case "${1:-}" in
  '') [[ $# -eq 0 ]] || usage; mac_main '';;
  --wip) [[ $# -eq 1 ]] || usage; mac_main --wip;;
  --host) [[ $# -eq 1 ]] || usage; host_main;;
  --host-cases) [[ $# -eq 1 ]] || usage; host_cases;;
  *) usage;;
esac
