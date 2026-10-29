#!/bin/bash
# NACK under loss, both ways, with the NACK simulator.
# - Downlink: FFmpeg publishes H.264/AAC over RTMP, Chrome plays it over WHEP,
#   and SRS drops packets it sends to Chrome, which NACKs them.
# - Uplink: Chrome publishes over WHIP and plays it back over WHEP, and SRS
#   drops packets it receives from Chrome, then NACKs them.
# Each way runs on two SRS: nack_prefer_rtx off repairs with plain
# retransmissions, on with RTX. Every peer is checked before the drop, after
# it, and at the end, from its getStats() samples and the SRS NACK logs.
set -eu
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
source "$SCRIPT_DIR/chrome-test-lib.sh"

DROP=30
chrome_test_init loss 15700
chrome_test_srs 0
chrome_test_start SRS_VHOST_RTC_NACK_PREFER_RTX=off
chrome_test_simulator || chrome_test_skip 'loss: unverified: SRS built without --simulator=on'
chrome_test_srs 1
chrome_test_start SRS_VHOST_RTC_NACK_PREFER_RTX=on
# The uplink SRS, apart so their NACK logs count only the publisher's loss.
chrome_test_srs 2
chrome_test_start SRS_VHOST_RTC_NACK_PREFER_RTX=off
chrome_test_srs 3
chrome_test_start SRS_VHOST_RTC_NACK_PREFER_RTX=on

plain=$(chrome_test_stream)
rtx=$(chrome_test_stream)
up_plain=$(chrome_test_stream)
up_rtx=$(chrome_test_stream)
chrome_test_srs 0
chrome_test_publish_ffmpeg "$plain"
chrome_test_wait_stream "$plain"
plain_url=$(chrome_test_whep_url "$plain")
chrome_test_srs 1
chrome_test_publish_ffmpeg "$rtx"
chrome_test_wait_stream "$rtx"
rtx_url=$(chrome_test_whep_url "$rtx")
chrome_test_srs 2
up_plain_whip=$(chrome_test_whip_url "$up_plain")
up_plain_whep=$(chrome_test_whep_url "$up_plain")
chrome_test_srs 3
up_rtx_whip=$(chrome_test_whip_url "$up_rtx")
up_rtx_whep=$(chrome_test_whep_url "$up_rtx")

# Sampling starts once every job is ready: 3 s before the drops, then 6 s for
# the repair and the steady play after it.
chrome_test_run_start loss --seconds 9 --mode whep --name plain --url "$plain_url" \
  --mode whep --name rtx --url "$rtx_url" \
  --mode whip --name up-plain --url "$up_plain_whip" --mode whep --name up-plain-play --url "$up_plain_whep" \
  --mode whip --name up-rtx --url "$up_rtx_whip" --mode whep --name up-rtx-play --url "$up_rtx_whep"
for job in plain rtx up-plain up-plain-play up-rtx up-rtx-play; do
  chrome_test_run_ready "$job"
done
# A measuring window, not a wait for a condition: the baseline samples.
sleep 3
chrome_test_srs 0
plain_session=$(chrome_test_session plain)
plain_at=$(chrome_test_now_ms)
chrome_test_drop "$plain_session" "$DROP"
chrome_test_srs 1
rtx_session=$(chrome_test_session rtx)
rtx_at=$(chrome_test_now_ms)
chrome_test_drop "$rtx_session" "$DROP"
chrome_test_srs 2
up_plain_session=$(chrome_test_session up-plain)
up_plain_at=$(chrome_test_now_ms)
chrome_test_drop "$up_plain_session" "$DROP"
chrome_test_srs 3
up_rtx_session=$(chrome_test_session up-rtx)
up_rtx_at=$(chrome_test_now_ms)
chrome_test_drop "$up_rtx_session" "$DROP"
chrome_test_run_wait

# The drop time of a job: its own, or its publisher's for an uplink player.
drop_at() {
  case "$1" in
    plain) echo "$plain_at";;
    rtx) echo "$rtx_at";;
    up-plain*) echo "$up_plain_at";;
    up-rtx*) echo "$up_rtx_at";;
  esac
}

# The counters before the drop and at the end, for the log.
inbound='def c(s): (s | {nack: (series(.inboundVideo.nackCount) | last), lost: (series(.inboundVideo.packetsLost) | last),
  rtx: (series(.inboundVideo.retransmittedPacketsReceived) | last), freezes: (series(.inboundVideo.freezeCount) | last),
  frames: (series(.inboundVideo.framesDecoded) | last)});'
outbound='def c(s): (s | {nack: (series(.outboundVideo.nackCount) | last),
  resent: (series(.outboundVideo.retransmittedPacketsSent) | last), audioNack: (series(.outboundAudio.nackCount) | last),
  audioResent: (series(.outboundAudio.retransmittedPacketsSent) | last), frames: (series(.outboundVideo.framesEncoded) | last)});'
for job in plain rtx up-plain-play up-rtx-play up-plain up-rtx; do
  at=$(drop_at "$job")
  counters=$inbound
  [[ $job == up-plain || $job == up-rtx ]] && counters=$outbound
  echo "STATS $job before $(jq -c --arg job "$job" "$CHROME_JQ_DEFS $counters first(.jobs[] | select(.name == \$job))
    | c(before($at))" "$CHROME_TEST_DIR/loss-stats.json") end $(jq -c --arg job "$job" "$CHROME_JQ_DEFS $counters
    first(.jobs[] | select(.name == \$job)) | c(since($at))" "$CHROME_TEST_DIR/loss-stats.json")"
done

# The log of the SRS a player plays from.
srs_log_of() {
  case "$1" in
    plain) echo srs;;
    rtx) echo srs-1;;
    up-plain*) echo srs-2;;
    up-rtx*) echo srs-3;;
  esac
}

# chrome_test_job_assert with the drop time as $at, and $b and $a the series
# of a sample field, such as inboundVideo.nackCount, before and since it.
# CHECK_ASSERT=chrome_test_freeze_assert makes it a freeze check.
check() {
  local job=$1 field=$2 filter=$3 desc=$4 at
  at=$(drop_at "$job")
  ${CHECK_ASSERT:-chrome_test_job_assert} loss "$job" "$at as \$at | (before(\$at) | series(.$field)) as \$b
    | (since(\$at) | series(.$field)) as \$a | $filter" "$desc" $(srs_log_of "$job")
}

# Players: playing before the drop with no loss, NACK or freeze, and after it
# a Chrome NACK, the repair and the play going on. The uplink players see the
# loss only as a late upstream packet.
for job in plain rtx up-plain-play up-rtx-play; do
  if chrome_test_loopback_known "WHEP $job loss"; then continue; fi
  # A player that joins the RTMP source mid-GOP decodes slowly at first: 25-30
  # frames in 3 s.
  check "$job" inboundVideo.framesDecoded '($b | length) >= 2 and ($b | last) - ($b | first) >= 15' "$job before drop: frames decoded +15"
  chrome_test_startup_freeze loss "$job" "$job"
  chrome_test_freeze_assert loss "$job" "settled(before($(drop_at "$job"))) | series(.inboundVideo.freezeCount)
    | length > 0 and last - first == 0" "$job before drop: no freeze after its first second" "$(srs_log_of "$job")"
  CHECK_ASSERT=chrome_test_freeze_assert check "$job" inboundVideo.freezeCount '($a | last) - ($b | last) <= 1' \
    "$job after drop: at most one freeze"
  check "$job" inboundVideo.framesDecoded '($a | last) - ($b | last) >= 60' "$job after drop: frames decoded +60"
  [[ $job == plain || $job == rtx ]] || continue
  check "$job" inboundVideo.nackCount '($b | last) - ($b | first) == 0' "$job before drop: no NACK"
  # Chrome reads a duplicate packet as -1 lost.
  check "$job" inboundVideo.packetsLost '($b | last) <= 0' "$job before drop: no packet lost"
  check "$job" inboundVideo.nackCount '($a | last) - ($b | last) >= 1' "$job after drop: Chrome NACK"
done

if [[ -z "${CHROME_LOOPBACK:-}" ]]; then
  # A plain repair arrives on the media SSRC, so packetsLost falls back.
  check plain inboundVideo.packetsLost '($b | last) as $p | ($a | last) <= $p
    and any(since($at)[]; .at < $at + 5000 and (.inboundVideo.packetsLost // 1e9) <= $p)' \
    'plain after drop: packets lost back to the pre-drop count within 5s'
  check plain inboundVideo.retransmittedPacketsReceived '($a | last) == 0' 'plain after drop: no RTX received'
  # An RTX repair arrives on the RTX SSRC; packetsLost may stay up.
  check rtx inboundVideo.retransmittedPacketsReceived '($a | last) >= 1' 'rtx after drop: RTX packets received'
fi

# The SSRC of the Video or Audio packets the publisher's SRS dropped.
dropped_ssrc() {
  sed -n "s/.* drop seq=[0-9]*, ssrc=\([0-9]*\)\/$1, .*/\1/p" "$CHROME_TEST_DIR/$CHROME_SRS_NAME.log" | head -n 1
}

# Publishers: no NACK or retransmission before the drop; after it Chrome
# resends every packet SRS dropped, and keeps encoding. Chrome sometimes
# resends the video before SRS has seen the gap, likely from the TWCC
# feedback, which skips dropped packets; so its NACK count must rise only
# for an SSRC SRS sent a NACK for.
for kind in plain rtx; do
  job=up-$kind
  if chrome_test_loopback_known "WHIP $job loss"; then continue; fi
  chrome_test_srs 2
  [[ $kind == plain ]] || chrome_test_srs 3
  check "$job" outboundVideo.framesEncoded '($b | last) - ($b | first) >= 40' "$job before drop: frames encoded +40"
  check "$job" outboundVideo.framesEncoded '($a | last) - ($b | last) >= 60' "$job after drop: frames encoded +60"
  for media in Video Audio; do
    check "$job" "outbound$media.nackCount" '($b | last) - ($b | first) == 0' "$job before drop: no $media NACK"
    check "$job" "outbound$media.retransmittedPacketsSent" '($b | last) - ($b | first) == 0' \
      "$job before drop: no $media retransmission"
    # The drop splits between video and audio by their packet order.
    drops=$(chrome_test_srs_log_count "/$media, ts=")
    if [[ $drops -eq 0 ]]; then
      echo "INFO $job: SRS dropped no $media packet"
      continue
    fi
    check "$job" "outbound$media.retransmittedPacketsSent" "(\$a | last) - (\$b | last) >= $drops" \
      "$job after drop: Chrome retransmitted the $drops $media packets SRS dropped"
    ssrc=$(dropped_ssrc "$media")
    requests=$(chrome_test_srs_log_count "NACK: request ssrc=$ssrc,")
    if [[ $requests -eq 0 ]]; then
      echo "INFO $job: Chrome resent $media before SRS asked (no NACK: request ssrc=$ssrc)"
      continue
    fi
    check "$job" "outbound$media.nackCount" '($a | last) - ($b | last) >= 1' \
      "$job after drop: Chrome got SRS's $media NACK ($requests requests)"
  done
done

# The SRS side: the drop, the NACK, and the repair of the configured kind.
# log_check <pattern> <test operator> <count> <description>
log_check() {
  local n
  n=$(chrome_test_srs_log_count "$1")
  if test "$n" "$2" "$3"; then
    chrome_test_pass "$4 ($n)"
  else
    chrome_test_fail "$4: $n lines '$1' in $CHROME_TEST_DIR/$CHROME_SRS_NAME.log"
  fi
}
chrome_test_srs 0
log_check 'player drop seq=' -eq "$DROP" "plain SRS dropped $DROP packets"
log_check 'NACK: received ssrc=' -ge 1 'plain SRS received NACK'
log_check 'NACK: resend plain' -ge 1 'plain SRS resent plain'
log_check 'NACK: resend RTX' -eq 0 'plain SRS resent no RTX'
chrome_test_srs 1
log_check 'player drop seq=' -eq "$DROP" "rtx SRS dropped $DROP packets"
log_check 'NACK: received ssrc=' -ge 1 'rtx SRS received NACK'
log_check 'NACK: resend RTX' -ge 1 'rtx SRS resent RTX'
log_check 'NACK: resend plain' -eq 0 'rtx SRS resent no plain'
chrome_test_srs 2
log_check 'drop seq=' -eq "$DROP" "up-plain SRS dropped $DROP received packets"
log_check 'NACK: request ssrc=' -ge 1 'up-plain SRS sent NACK'
log_check 'NACK: recovered seq=' -ge 1 'up-plain SRS recovered packets'
log_check 'NACK: RTX recv' -eq 0 'up-plain SRS received no RTX'
chrome_test_srs 3
log_check 'drop seq=' -eq "$DROP" "up-rtx SRS dropped $DROP received packets"
log_check 'NACK: request ssrc=' -ge 1 'up-rtx SRS sent NACK'
log_check 'NACK: recovered seq=' -ge 1 'up-rtx SRS recovered packets'
log_check 'NACK: RTX recv' -ge 1 'up-rtx SRS received RTX'
chrome_test_finish
