#!/bin/bash
# OBS on the Windows test host publishes RTMP, SRT and WHIP to the native srs.exe on the same host.
#
#   obs-windows-test.sh [--wip]
#
# Opt in with SRS_TEST_WINDOWS_HOST; SRS_TEST_OBS=0 skips. windows-host-run.sh syncs this checkout to
# the host, with --wip as a snapshot of the working tree, and runs the --host step in the host checkout:
#   - srs-windows-runner.sh build builds srs.exe, with its deps step first; both keep their builds.
#   - The --host-cases step runs obs-lane-test.sh there against trunk/objs/windows/srs.exe: the RTMP,
#     SRT and WHIP cases in parallel, each with its own isolated OBS and srs.exe on its own slot from
#     15100, as on macOS. srs-windows-runner.sh obs runs this step after its own build.
#
# The host needs Python 3.9+, FFmpeg, jq and curl in Git Bash, and OBS with obs-websocket. Prints the
# build time, each case's log and result, and the LANE obs line; the cases fail over 60 s. Exits 0
# when passed or skipped, 1 when a case or the build failed, and 3 for bad arguments or a failed host
# setup.
set -eu
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
LOG_DIR=/tmp/srs-obs-windows

usage() {
  echo "Usage: $0 [--wip]" >&2
  exit 3
}

# On the Mac: sync the checkout to the host and run the --host step there.
mac_main() {
  local wip=$1 checkout status=0
  [[ "${SRS_TEST_OBS:-1}" != 0 ]] || { echo 'SKIP SRS_TEST_OBS=0'; exit 0; }
  [[ -n "${SRS_TEST_WINDOWS_HOST:-}" ]] || { echo 'SKIP windows: SRS_TEST_WINDOWS_HOST is not set'; exit 0; }
  checkout=$(cd "$SCRIPT_DIR/../../.." && pwd)
  bash "$SCRIPT_DIR/windows-host-run.sh" $wip "$checkout" "$SCRIPT_DIR/obs-windows-test.sh" --host || status=$?
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

# In Git Bash on the host, in the host checkout: the OBS cases against the native srs.exe.
host_cases() {
  local here=$PWD/skills/srs-develop/scripts status=0
  [[ -f "$here/obs-lane-test.sh" ]] || { echo "SETUP_FAIL: $PWD is not an SRS checkout"; exit 3; }
  [[ -x trunk/objs/windows/srs.exe ]] || { echo 'FAIL build trunk/objs/windows/srs.exe first'; exit 1; }
  if [[ -z "${OBS_PYTHON:-}" ]]; then
    # The Windows python3 can be the Microsoft Store stub, which never runs.
    for OBS_PYTHON in python3 python ''; do
      [[ -z "$OBS_PYTHON" ]] || ! "$OBS_PYTHON" --version >/dev/null 2>&1 || break
    done
    [[ -n "$OBS_PYTHON" ]] || { echo 'SKIP windows: no Python'; exit 0; }
  fi
  # Python on Windows ends its lines with CRLF.
  OBS_PYTHON=$OBS_PYTHON OBS_SRS_BIN=objs/windows/srs.exe bash "$here/obs-lane-test.sh" 2>&1 | tr -d '\r'
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
