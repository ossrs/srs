#!/bin/bash
# Run every State Threads test on each platform this machine reaches, and report the results per OS.
#
#   st-test.sh [ST_DIR]     ST_DIR is the ST checkout, state-threads/ by default
#
# The platforms, detected, never assumed:
#   1. The local OS, macOS or Linux, from uname, then Linux in the SRS development image, when the
#      local OS is macOS, it passed, and docker info succeeds.
#   2. In parallel with them, Windows x64 on the host SRS_TEST_WINDOWS_HOST names, through
#      st-windows-test.sh, which tests only a committed checkout. Linux runs in Docker, so not in WSL
#      there.
# Docker and the local OS share the checkout's obj/, so they never run at the same time.
#
# The whole run holds the st product lock of full-tier-lock.sh, so ST runs of other agents never
# share obj/, the Windows checkout, or the logs; SRS tiers run in parallel.
#
# Prints the RESULT lines of each OS as it finishes, then a summary with one line per OS,
# the time of each OS and the total, and the platforms not tested and why. Logs are in
# /tmp/srs-st-test/.
# Exit 0 when every run passed, 1 when one failed, and 3 when Windows setup failed.

ST_DIR=${1:-state-threads/}
SCRIPTS=$(cd "$(dirname "$0")" && pwd)
TOP=$(git -C "$ST_DIR" rev-parse --show-toplevel 2>/dev/null)
if [[ -z $TOP ]]; then
  echo "$ST_DIR is not an ST checkout" >&2
  exit 2
fi

# The CPU name in the OS label: arm64 or x64.
cpu() {
  case $1 in
    arm64|aarch64) echo arm64 ;;
    x86_64|amd64) echo x64 ;;
    *) echo "$1" ;;
  esac
}

echo "lock: waiting, $(bash "$SCRIPTS/full-tier-lock.sh" --product st status)"
LOCK_START=$(date +%s)
if ! TOKEN=$(bash "$SCRIPTS/full-tier-lock.sh" --product st acquire --wait 600 --pid $$); then
  echo "lock: not acquired in 600s" >&2
  exit 2
fi
trap 'bash "$SCRIPTS/full-tier-lock.sh" --product st release "$TOKEN"' EXIT
echo "lock: acquired after $(( $(date +%s) - LOCK_START ))s"

mkdir -p /tmp/srs-st-test
rm -f /tmp/srs-st-test/local.out /tmp/srs-st-test/docker.out /tmp/srs-st-test/windows.out
NOT_TESTED=()
SETUP_FAILED=0
TOTAL_START=$(date +%s)
TIMES=()

case $(uname -s) in
  Darwin) LOCAL=macos-$(cpu "$(uname -m)") ;;
  *) LOCAL=linux-$(cpu "$(uname -m)") ;;
esac

# Windows tests its own checkout on the host, so it runs alongside the local OS and Docker.
echo "== windows: start $(date +%T)"
WINDOWS_START=$(date +%s)
SRS_ST_TEST_LOCKED=1 bash "$SCRIPTS/st-windows-test.sh" "$TOP" > /tmp/srs-st-test/windows.out 2>&1 &
WINDOWS_PID=$!

echo "== $LOCAL: start $(date +%T)"
START=$(date +%s)
bash "$SCRIPTS/st-test-runner.sh" "$LOCAL" "$TOP" > /tmp/srs-st-test/local.out 2>&1
LOCAL_RC=$?
SECS=$(( $(date +%s) - START ))
TIMES+=("$LOCAL ${SECS}s")
echo "== $LOCAL: done $(date +%T), ${SECS}s"
cat /tmp/srs-st-test/local.out

if [[ $LOCAL_RC != 0 ]]; then
  NOT_TESTED+=("docker: $LOCAL failed first")
elif [[ $LOCAL != macos-* ]]; then
  NOT_TESTED+=("docker: the local OS is already Linux")
elif ! DOCKER_ARCH=$(docker info --format '{{.Architecture}}' 2>/dev/null); then
  NOT_TESTED+=("docker: docker info failed; start Docker")
else
  DOCKER=linux-$(cpu "$DOCKER_ARCH")-docker
  echo "== $DOCKER: start $(date +%T)"
  START=$(date +%s)
  docker run --rm -v "$TOP":/st -v "$SCRIPTS":/scripts -w /st ossrs/srs:ubuntu20 \
    bash /scripts/st-test-runner.sh "$DOCKER" /st > /tmp/srs-st-test/docker.out 2>&1
  SECS=$(( $(date +%s) - START ))
  TIMES+=("$DOCKER ${SECS}s")
  echo "== $DOCKER: done $(date +%T), ${SECS}s"
  cat /tmp/srs-st-test/docker.out
fi

wait $WINDOWS_PID
WINDOWS_RC=$?
SECS=$(( $(date +%s) - WINDOWS_START ))
TIMES+=("windows-x64 ${SECS}s")
echo "== windows: done $(date +%T), ${SECS}s"
cat /tmp/srs-st-test/windows.out
while read -r line; do
  NOT_TESTED+=("${line#* }")
done < <(grep -E '^(SKIP|SETUP_FAIL)' /tmp/srs-st-test/windows.out)
[[ $WINDOWS_RC == 3 ]] && SETUP_FAILED=1

echo
echo "SUMMARY"
cat /tmp/srs-st-test/*.out 2>/dev/null | awk '
  /^RESULT / { os = $2; runs[os]++; if ($4 == "FAIL") { failed[os]++; names[os] = names[os] " " $3 } }
  END { for (os in runs) printf "  %-22s %s  %d runs%s\n", os, failed[os] ? "FAIL" : "PASS", runs[os],
                                 failed[os] ? ", failed:" names[os] : "" }' | sort
echo "TIME"
for line in "${TIMES[@]}"; do
  printf "  %-22s %s\n" ${line}
done
printf "  %-22s %s\n" total "$(( $(date +%s) - TOTAL_START ))s"
if [[ ${#NOT_TESTED[@]} -gt 0 ]]; then
  echo "NOT TESTED"
  for line in "${NOT_TESTED[@]}"; do
    echo "  $line"
  done
fi

if cat /tmp/srs-st-test/*.out 2>/dev/null | grep -q '^RESULT .* FAIL '; then exit 1; fi
[[ $SETUP_FAILED == 1 ]] && exit 3
exit 0
