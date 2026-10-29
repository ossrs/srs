#!/bin/bash
# Chrome on this host publishes and plays against SRS built and run for Linux in Docker: the WebRTC, loss,
# HTTP and codec cases of lane I.
#
#   chrome-linux-test.sh [--build]
#
# SRS_TEST_CHROME=0 skips; without a running Docker it prints SKIP linux: unverified: Docker not available.
#   - The build step copies the checkout's tracked trunk files, with their modification times, into the
#     named volume srs-chrome-linux-build and builds objs/srs there in the ossrs/srs:ubuntu20 image with
#     obs-linux-test.sh's Linux build, for Linux on the Docker host's CPU. make rebuilds only what changed,
#     and the local trunk/objs keeps its own flags. --build stops after this step.
#   - Then chrome-lane-test.sh --cases-only runs the four cases in parallel on SRS_TEST_PORT_BASE (default
#     16000), 16100, 16200 and 16300. Each SRS of a case runs in its own container of the build volume
#     (chrome-test-lib.sh CHROME_SRS_DOCKER): TCP ports on 127.0.0.1, the RTC UDP port on the host's own
#     IPv4, the WebRTC candidate. Logs are in /tmp/srs-chrome-linux.
#   - Google Chrome has no Linux arm64 build, so on the Mac Chrome runs on the host; on Linux, such as the
#     GitHub Actions job, the same script runs the host's Chrome against SRS in Docker.
#
# Exits 0 when passed or skipped, 1 when the build or a case failed, and 3 for bad arguments.
set -eu
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
LOG_DIR=${CHROME_TEST_ROOT:-/tmp}/srs-chrome-linux
IMAGE=ossrs/srs:ubuntu20
VOLUME=srs-chrome-linux-build
DOCKER=${CHROME_DOCKER:-docker}
SLOT=${SRS_TEST_PORT_BASE:-16000}

usage() {
  echo "Usage: $0 [--build]" >&2
  exit 3
}

check_slot() {
  case "$SLOT" in ''|*[!0-9]*) echo 'FAIL invalid port base' >&2; exit 3;; esac
  # The four cases take a 100-port slot each.
  [[ $SLOT -ge 11000 && $((SLOT + 399)) -lt 32768 ]] || { echo 'FAIL port slot outside test range' >&2; exit 3; }
}

# Copy the tracked trunk files into the volume and build SRS there.
build() {
  local checkout sha started
  checkout=$(cd "$SCRIPT_DIR/../../.." && pwd)
  sha=$(git -C "$checkout" rev-parse --short HEAD)
  git -C "$checkout" diff --quiet HEAD -- trunk || sha="$sha+dirty"
  mkdir -p "$LOG_DIR"
  started=$SECONDS
  if ! git -C "$checkout" ls-files -z trunk | "$DOCKER" run --rm -i --name srs-chrome-linux-build \
    -v $VOLUME:/build -v "$checkout":/src:ro -v "$SCRIPT_DIR":/scripts:ro $IMAGE \
    bash /scripts/obs-linux-test.sh --container-build >"$LOG_DIR/build.log" 2>&1; then
    tail -n 20 "$LOG_DIR/build.log"
    echo "FAIL Linux build of $sha (log: $LOG_DIR/build.log)"
    exit 1
  fi
  echo "BUILD linux $sha $((SECONDS - started))s"
}

case "${1:-}" in
  ''|--build) [[ $# -le 1 ]] || usage;;
  *) usage;;
esac
check_slot
[[ "${SRS_TEST_CHROME:-1}" != 0 ]] || { echo 'SKIP SRS_TEST_CHROME=0'; exit 0; }
"$DOCKER" info >/dev/null 2>&1 || { echo 'SKIP linux: unverified: Docker not available'; exit 0; }
build
[[ -z "${1:-}" ]] || exit 0
export CHROME_SRS_DOCKER=$VOLUME CHROME_SRS_IMAGE=$IMAGE CHROME_TEST_ROOT=$LOG_DIR SRS_TEST_PORT_BASE=$SLOT
exec bash "$SCRIPT_DIR/chrome-lane-test.sh" --cases-only
