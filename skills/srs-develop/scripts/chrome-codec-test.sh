#!/bin/bash
# Chrome publishes each video codec SRS's WebRTC supports, H.264, VP9, AV1 and
# H.265, with Opus audio over WHIP, each in its own window, and plays each
# back over WHEP in the same browser. A codec the browser cannot encode, such
# as H.265 without a hardware encoder, is a SKIP and listed as unverified.
# VP8 is not tested: SRS's WebRTC has no VP8.
#
# H.265 runs first, in a browser of its own. In one browser with it, every
# publisher stopped encoding for about 4 s while the H.265 hardware encoder
# started, and afterwards some players got packets but decoded no more
# frames, a different codec each run.
#
# Alone, macOS's H.265 hardware encoder (VideoToolbox) often stops after its
# first keyframe while the camera keeps sending frames, with no quality
# limit. Then the case prints an INFO line and checks that the frames it did
# encode reached the player.
set -eu
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
source "$SCRIPT_DIR/chrome-test-lib.sh"

codecs='h264 vp9 av1 h265'
# How SRS's API and the browser's getStats name each codec, and its run.
api_codec() { case $1 in h264) echo H264;; vp9) echo VP9;; av1) echo AV1;; h265) echo HEVC;; esac; }
mime_codec() { case $1 in h264) echo H264;; vp9) echo VP9;; av1) echo AV1;; h265) echo H265;; esac; }
run_of() { if [[ $1 == h265 ]]; then echo codec-h265; else echo codec; fi; }

chrome_test_init codec 15900
chrome_test_start
streams=
jobs=()
h265_jobs=()
for codec in $codecs; do
  stream=$(chrome_test_stream)
  streams="$streams $codec:$stream"
  set -- --mode whip --name "whip-$codec" --codec "$codec" --url "$(chrome_test_whip_url "$stream")" \
    --mode whep --name "whep-$codec" --url "$(chrome_test_whep_url "$stream")"
  if [[ $codec == h265 ]]; then h265_jobs+=("$@"); else jobs+=("$@"); fi
done

# Each publisher is in the API only while the browser runs, and the H.265 one
# waits seconds for its first keyframe, so every answer is kept.
chrome_test_api_record
# The driver's default ready timeout is 10 s, and 20 s for H.265.
chrome_test_run codec-h265 --seconds 4 "${h265_jobs[@]}"
chrome_test_run codec --seconds 4 "${jobs[@]}"
chrome_test_api_record_stop

for entry in $streams; do
  codec=${entry%%:*} stream=${entry#*:} run=$(run_of "${entry%%:*}")
  label=$(echo "$codec" | tr '[:lower:]' '[:upper:]')
  if [[ "$(chrome_test_job_result "$run" "whip-$codec")" == skip ]]; then
    reason=$(jq -r --arg job "whip-$codec" 'first(.jobs[] | select(.name == $job)) | .reason // "skipped"' \
      "$CHROME_TEST_DIR/$run-stats.json")
    chrome_test_skipped "codec $codec: unverified: $reason"
    continue
  fi
  # Only video.codec: the API's VP9 and AV1 size and the VP9 audio codec are
  # not reliable yet (finding F2).
  chrome_test_api_seen "any(.streams[]?; .name == \"$stream\" and .video.codec == \"$(api_codec "$codec")\")" \
    "API $label publisher video $(api_codec "$codec")"
  mime=$(mime_codec "$codec")
  chrome_test_job_assert "$run" "whip-$codec" "latest(.outboundVideo.codec) == \"video/$mime\"
    and latest(.outboundAudio.codec) == \"audio/opus\"" "WHIP $label/Opus"
  frames=40 decoded='rise(.inboundVideo.framesDecoded) >= 40' decoded_desc='frames decoded +40'
  encoded=$(jq --arg job "whip-$codec" "$CHROME_JQ_DEFS first(.jobs[] | select(.name == \$job))
    | rise(.outboundVideo.framesEncoded)" "$CHROME_TEST_DIR/$run-stats.json")
  if [[ $codec == h265 && $encoded -lt $frames ]]; then
    echo "INFO WHIP $label: Chrome's encoder stalled, $encoded frames encoded"
    frames=1 decoded='latest(.inboundVideo.framesDecoded) >= 1' decoded_desc='a frame decoded'
  fi
  chrome_test_job_assert "$run" "whip-$codec" "rise(.outboundVideo.framesEncoded) >= $frames" \
    "WHIP $label frames encoded +$frames"
  chrome_test_loopback_known "WHEP $label play" && continue
  chrome_test_job_assert "$run" "whep-$codec" "latest(.inboundVideo.codec) == \"video/$mime\"
    and latest(.inboundAudio.codec) == \"audio/opus\"" "WHEP $label/Opus"
  chrome_test_job_assert "$run" "whep-$codec" "$decoded" "WHEP $label $decoded_desc"
done
chrome_test_finish
