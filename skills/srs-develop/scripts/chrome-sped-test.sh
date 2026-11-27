#!/bin/bash
# SPED (the DTLS handshake inside the ICE STUN checks) with Chrome's field
# trial WebRTC-IceHandshakeDtls. Chrome publishes over WHIP and plays it back
# over WHEP, in one browser, three ways:
# - on: an SRS with sped on; the clients API shows SPED complete for both.
# - drop: the same SRS, with the handshake simulator losing SRS's first STUN
#   response of each session; SPED still completes. Needs --simulator=on.
# - off: an SRS with sped off; the clients API shows no SPED.
# - last: the sped off SRS, with the handshake simulator losing SRS's DTLS 1.2
#   last flight of each session; Chrome resends its own and SRS answers it
#   again. Needs --simulator=on and DTLS 1.2.
# Each player decodes frames, and each job negotiates DTLS 1.3 when SRS reports
# dtls13 in /api/v1/features (a --ssl-4=on build), else DTLS 1.2. A browser
# whose offer has no sped with the trial is a SKIP.
set -eu
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
source "$SCRIPT_DIR/chrome-test-lib.sh"

TRIAL=--force-fieldtrials=WebRTC-IceHandshakeDtls/Enabled/
chrome_test_init sped 16500

# The trial works only where the browser offers sped with it.
chrome_test_command 20 "${CHROME_NODE:-node}" "$CHROME_JS" launch --probe --extra-chrome-flags "$TRIAL" \
  >"$CHROME_TEST_DIR/probe.log" 2>&1 || chrome_test_fail "chrome launch --probe (see $CHROME_TEST_DIR/probe.log)"
options=$(sed -n 's/^offer ice-options: //p' "$CHROME_TEST_DIR/probe.log" | head -n 1)
echo "PROBE offer ice-options: ${options:-none}"
[[ " $options " == *' sped '* ]] || chrome_test_skip "sped: unverified: the offer has no sped with $TRIAL"

chrome_test_srs 0
chrome_test_start SRS_VHOST_RTC_SPED=on
features=$(chrome_test_read_api /api/v1/features 2>/dev/null) || features='{}'
drop=
if echo "$features" | jq -e 'any(..; type == "object" and .handshake_drop == true)' >/dev/null; then
  drop=1
else
  chrome_test_skipped 'sped drop and last: unverified: SRS built without --simulator=on'
fi
# With dtls_version auto, SRS takes the newest DTLS both sides have: 1.3 when
# its OpenSSL has it, as Chrome does.
tls=FEFD dtls='DTLS 1.2'
if echo "$features" | jq -e 'any(..; type == "object" and .dtls13 == true)' >/dev/null; then
  tls=FEFC dtls='DTLS 1.3'
fi
echo "SRS ${dtls} expected"
# DTLS 1.3 has no ChangeCipherSpec, so the simulator loses only a 1.2 last flight.
last=
if [[ -n $drop && $tls == FEFD ]]; then
  last=1
elif [[ -n $drop ]]; then
  chrome_test_skipped "sped last: unverified: the simulator loses only a DTLS 1.2 last flight, SRS has $dtls"
fi
chrome_test_srs 1
chrome_test_start SRS_VHOST_RTC_SPED=off

on=$(chrome_test_stream)
lost=$(chrome_test_stream)
off=$(chrome_test_stream)
final=$(chrome_test_stream)
chrome_test_srs 0
jobs=(--mode whip --name on-pub --url "$(chrome_test_whip_url "$on")" --mode whep --name on-play --url "$(chrome_test_whep_url "$on")")
[[ -z $drop ]] || jobs+=(--mode whip --name drop-pub --url "$(chrome_test_whip_url "$lost")&drop-stun=1"
  --mode whep --name drop-play --url "$(chrome_test_whep_url "$lost")&drop-stun=1")
chrome_test_srs 1
jobs+=(--mode whip --name off-pub --url "$(chrome_test_whip_url "$off")" --mode whep --name off-play --url "$(chrome_test_whep_url "$off")")
[[ -z $last ]] || jobs+=(--mode whip --name last-pub --url "$(chrome_test_whip_url "$final")&drop-finished=1"
  --mode whep --name last-play --url "$(chrome_test_whep_url "$final")&drop-finished=1")

chrome_test_run_start sped --seconds 5 --extra-chrome-flags "$TRIAL" "${jobs[@]}"
for job in on-pub on-play ${drop:+drop-pub drop-play} off-pub off-play ${last:+last-pub last-play}; do
  chrome_test_run_ready "$job"
done

# poll_clients <filter> <desc>: poll the current SRS's clients API, while the
# browser still plays, until the jq filter holds; the last answer is kept in
# clients-<srs>.json.
poll_clients() {
  local file="$CHROME_TEST_DIR/clients-$CHROME_SRS_NAME.json" data deadline=$((SECONDS + ${CHROME_API_WAIT:-8}))
  while :; do
    data=$(chrome_test_read_api '/api/v1/clients/?count=100' 2>/dev/null) || data='{}'
    echo "$data" > "$file"
    if echo "$data" | jq -e "$1" >/dev/null 2>&1; then
      chrome_test_pass "$2"
      return
    fi
    if [[ $SECONDS -ge $deadline ]]; then
      chrome_test_fail "$2 (see $file)"
      return
    fi
    chrome_test_budget || return 1
    sleep 0.1
  done
}

# The publisher and the player of a stream, both in the API.
both() { echo "[.clients[]? | select(.name == \"$1\")] as \$c | (\$c | map(.type) | sort) == [\"rtc-play\", \"rtc-publish\"]"; }

# SPED is complete for both, and SRS got at least Chrome's first ClientHello
# fragment inside STUN.
complete='all($c[]; .sped.state == "complete" and .sped.embedded_recv >= 1 and .sped.handshake_ms >= 0)'
chrome_test_srs 0
poll_clients "$(both "$on") and $complete" 'sped on: SPED complete in the API for the publisher and the player'
if [[ -n $drop ]]; then
  # Chrome sends no raw DTLS before its first response, so after the lost one
  # its next check carries the rest of the ClientHello, and SRS's answer to it
  # carries SRS's first flight.
  poll_clients "$(both "$lost") and $complete and all(\$c[]; .sped.embedded_recv >= 2 and .sped.embedded_sent >= 1)" \
    'sped drop: SPED complete in the API after a lost first response, the ClientHello and the first flight embedded'
  n=$(chrome_test_srs_log_count 'handshake simulator #')
  if [[ $n -ge 2 ]]; then
    chrome_test_pass "sped drop: SRS lost a STUN response of each session ($n)"
  else
    chrome_test_fail "sped drop: SRS lost $n STUN responses, want one per session (see $CHROME_TEST_DIR/srs.log)"
  fi
fi
chrome_test_srs 1
poll_clients "$(both "$off") and all(\$c[]; has(\"sped\") | not)" 'sped off: no SPED in the API'
if [[ -n $last ]]; then
  poll_clients "$(both "$final") and all(\$c[]; has(\"sped\") | not)" 'sped last: the publisher and the player in the API after a lost last flight'
fi
chrome_test_run_wait

for kind in on ${drop:+drop} off ${last:+last}; do
  chrome_test_job_assert sped "$kind-pub" 'rise(.outboundVideo.framesEncoded) >= 30' "sped $kind: WHIP frames encoded +30"
  for job in pub play; do
    chrome_test_job_assert sped "$kind-$job" "latest(.dtls.tlsVersion) == \"$tls\"" "sped $kind: $job negotiated $dtls"
  done
  if chrome_test_loopback_known "WHEP sped $kind play"; then continue; fi
  chrome_test_job_assert sped "$kind-play" 'rise(.inboundVideo.framesDecoded) >= 30' "sped $kind: WHEP frames decoded +30"
done

# last_flight_gaps: for each session whose last flight the simulator lost, the
# ms from the loss until SRS got the client's resend, while SRS was done and
# the client was not, from the current SRS's log.
last_flight_gaps() {
  awk '
    function ms(line,    t, a) {
      if (!match(line, /[0-9][0-9]:[0-9][0-9]:[0-9][0-9]\.[0-9][0-9][0-9]\]/)) return -1
      t = substr(line, RSTART, 12); split(t, a, /[:.]/)
      return ((a[1] * 60 + a[2]) * 60 + a[3]) * 1000 + a[4]
    }
    function cid(line,    s, p) {
      if (!match(line, /\]\[[0-9]+\]\[[a-z0-9]+\]/)) return ""
      s = substr(line, RSTART + 2, RLENGTH - 3); split(s, p, /\]\[/); return p[2]
    }
    /handshake simulator #[0-9]+ drops a DTLS last flight/ { lost[cid($0)] = ms($0) }
    /DTLS: After done, got/ { c = cid($0); if (c in lost) { print ms($0) - lost[c]; delete lost[c] } }
  ' "$CHROME_TEST_DIR/$CHROME_SRS_NAME.log"
}

if [[ -n $last ]]; then
  chrome_test_srs 1
  gaps=$(last_flight_gaps | tr '\n' ' ')
  n=$(echo $gaps | wc -w | tr -d ' ')
  if [[ $n -ge 2 ]]; then
    chrome_test_pass "sped last: SRS lost its last flight of each session and answered Chrome's resend ($n)"
  else
    chrome_test_fail "sped last: SRS answered $n resends after a lost last flight, want one per session (see $CHROME_TEST_DIR/$CHROME_SRS_NAME.log)"
  fi
  echo "GAP sped last: ms from the lost last flight to Chrome's resend: ${gaps:-none}"
fi

chrome_test_srs 0
for kind in 'SPED complete' 'SPED off'; do
  echo "SRS sped on: $(chrome_test_srs_log_count "RTC: $kind,") '$kind' trace lines"
done
chrome_test_finish
