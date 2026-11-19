#!/bin/bash
# Measure Chrome's WebRTC setup time with and without SPED, at zero loss. One
# SRS with sped on; each run is a new browser that publishes over WHIP and
# plays it back over WHEP, with Chrome's field trial WebRTC-IceHandshakeDtls
# (on) or without it (off). Without the trial Chrome offers no sped, so SRS
# answers none, as with sped off. The runs alternate off and on.
#
# From the test page's events, per job: setRemoteDescription to
# connectionState connected, and for the player to its first video frame. It
# prints the host, the build, and one row per trial and job with the runs, the
# failures and the p50 and p95 in ms, and the DTLS versions negotiated per
# trial; each run's times are in runs.txt.
#
# Usage: chrome-sped-measure.sh [--runs <n>]   (20 runs of each by default)
#
# Environment: SRS_TEST_PORT_BASE (default 16700), and the settings of
# chrome-test-lib.sh.
set -eu
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

runs=20
while [[ $# -gt 0 ]]; do
  case "$1" in
    --runs)
      [[ "${2:-}" =~ ^[1-9][0-9]*$ ]] || { echo "Usage: $0 [--runs <n>]" >&2; exit 2; }
      runs=$2
      shift 2
      ;;
    *) echo "Usage: $0 [--runs <n>]" >&2; exit 2;;
  esac
done

source "$SCRIPT_DIR/chrome-test-lib.sh"
TRIAL=--force-fieldtrials=WebRTC-IceHandshakeDtls/Enabled/
CHROME_TEST_BUDGET=$((60 + 2 * runs * 15))
chrome_test_init sped-measure 16700

chrome_test_command 20 "${CHROME_NODE:-node}" "$CHROME_JS" launch --probe --extra-chrome-flags "$TRIAL" \
  >"$CHROME_TEST_DIR/probe.log" 2>&1 || chrome_test_fail "chrome launch --probe (see $CHROME_TEST_DIR/probe.log)"
options=$(sed -n 's/^offer ice-options: //p' "$CHROME_TEST_DIR/probe.log" | head -n 1)
echo "PROBE offer ice-options: ${options:-none}"
[[ " $options " == *' sped '* ]] || chrome_test_skip "sped-measure: the offer has no sped with $TRIAL"

chrome_test_start SRS_VHOST_RTC_SPED=on
echo "HOST $(uname -sm), $(sysctl -n machdep.cpu.brand_string 2>/dev/null || uname -p)"
echo "SRS $(jq -r '.data.version' "$CHROME_TEST_DIR/version.json"), commit $(git -C "$CHROME_WORKSPACE" rev-parse --short HEAD)"
echo "BUILD $(sed -n 's/^#define SRS_USER_CONFIGURE "\(.*\)"$/\1/p' "$CHROME_WORKSPACE/trunk/objs/srs_auto_headers.hpp")"

# Each line: trial, job, ms to connected, ms to the first frame, the DTLS
# version Chrome reports (FEFD 1.2, FEFC 1.3); null when not reached or the run
# failed.
RESULTS="$CHROME_TEST_DIR/runs.txt"
: > "$RESULTS"
for i in $(seq 1 "$runs"); do
  for trial in off on; do
    stream=$(chrome_test_stream)
    flags=()
    [[ $trial == off ]] || flags=(--extra-chrome-flags "$TRIAL")
    name=$trial-$i
    ok=ok
    chrome_test_run "$name" --seconds 1 ${flags[@]+"${flags[@]}"} \
      --mode whip --name pub --url "$(chrome_test_whip_url "$stream")" \
      --mode whep --name play --url "$(chrome_test_whep_url "$stream")" >"$CHROME_TEST_DIR/$name.out" 2>&1 || ok=
    for job in pub play; do
      if [[ -n $ok ]]; then
        jq -r --arg job "$job" --arg trial "$trial" \
          'first(.jobs[] | select(.name == $job))
           | "\($trial) \($job) \(.setup.connectedMs) \(.setup.firstFrameMs) \([.samples[]? | .dtls.tlsVersion | select(. != null)] | last)"' \
          "$CHROME_TEST_DIR/$name-stats.json" >>"$RESULTS"
      else
        echo "$trial $job null null null" >>"$RESULTS"
      fi
    done
    echo "RUN $name $(tail -n 2 "$RESULTS" | cut -d' ' -f2- | tr '\n' ' ')${ok:-FAILED, see $CHROME_TEST_DIR/$name.out}"
  done
done

# Nearest-rank percentiles over the runs that reached the point.
printf '%-5s %-5s %5s %7s %14s %14s %10s %10s\n' TRIAL JOB RUNS FAILED CONNECTED_P50 CONNECTED_P95 FRAME_P50 FRAME_P95
for trial in off on; do
  for job in pub play; do
    jq -nrR --arg trial "$trial" --arg job "$job" '
      def pct(p): if length == 0 then "-" else sort | .[(((p / 100) * length) | ceil) - 1] | tostring end;
      [inputs | split(" ") | select(.[0] == $trial and .[1] == $job)] as $r
      | [$r[] | .[2] | select(. != "null") | tonumber] as $c
      | [$r[] | .[3] | select(. != "null") | tonumber] as $f
      | [$r[] | select(.[2] != "null" and ($job == "pub" or .[3] != "null"))] as $done
      | [$trial, $job, ($r | length), ($r | length) - ($done | length),
         ($c | pct(50)), ($c | pct(95)), ($f | pct(50)), ($f | pct(95))]
      | @tsv' "$RESULTS" | while IFS=$'\t' read -r a b c d e f g h; do
      printf '%-5s %-5s %5s %7s %14s %14s %10s %10s\n' "$a" "$b" "$c" "$d" "$e" "$f" "$g" "$h"
    done
  done
done
for trial in off on; do
  echo "DTLS $trial: $(awk -v t="$trial" '$1 == t { print $5 }' "$RESULTS" | sort | uniq -c | awk '{ printf "%s%s x%s", s, $2, $1; s = ", " }')"
done
echo "SRS: $(grep -ac 'RTC init session,.* sped=1,' "$CHROME_TEST_DIR/srs.log" || true) sessions negotiated SPED and $(chrome_test_srs_log_count 'RTC: SPED complete,') completed it, want $((2 * runs))"
chrome_test_finish
