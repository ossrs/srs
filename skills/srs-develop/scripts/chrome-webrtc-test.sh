#!/bin/bash
# Chrome publishes H.264/Opus over WHIP and plays it back over WHEP and HTTP-FLV
# (rtc_to_rtmp), and plays FFmpeg's RTMP H.264/AAC over WHEP (rtmp_to_rtc), all
# in one browser against source-built SRS.
set -eu
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
source "$SCRIPT_DIR/chrome-test-lib.sh"

chrome_test_init webrtc 15600
chrome_test_start
whip=$(chrome_test_stream)
rtmp=$(chrome_test_stream)
chrome_test_publish_ffmpeg "$rtmp"
chrome_test_wait_stream "$rtmp"

# The bridge reports the WHIP stream as H.264 with AAC from the Opus input.
CHROME_API_WAIT=20 chrome_test_api_watch "any(.streams[]?; .name == \"$whip\" and .publish.active == true
  and .video.codec == \"H264\" and .audio.codec == \"AAC\")"
# The FLV of a WHIP stream starts at the bridge's first keyframe request,
# pli_for_rtmp (6 s) after the publish. The bridge's AAC starts only at
# Chrome's first audio sender report, 5.4 to 8.3 s after the publish, so the
# publisher runs about 11 s.
chrome_test_run webrtc --seconds 8 --timeout 15 \
  --mode whip --name whip --codec h264 --url "$(chrome_test_whip_url "$whip")" \
  --mode whep --name whep --url "$(chrome_test_whep_url "$whip")" \
  --mode flv --name flv --url "$(chrome_test_flv_url "$whip")" \
  --mode whep --name whep-rtmp --url "$(chrome_test_whep_url "$rtmp")"
chrome_test_api_watched 'API WHIP publisher H264/AAC'

chrome_test_job_assert webrtc whip 'latest(.outboundVideo.codec) == "video/H264"
  and latest(.outboundAudio.codec) == "audio/opus" and rise(.outboundVideo.framesEncoded) >= 40' \
  'WHIP H264/Opus frames encoded +40'

# framesPerSecond can still be missing 3 s after the first frame, so the rate
# comes from framesDecoded over the samples' time.
fps='[.samples[]? | select(.inboundVideo.framesDecoded != null)]
  | (last.inboundVideo.framesDecoded - first.inboundVideo.framesDecoded) / ((last.t - first.t) / 1000)'
if ! chrome_test_loopback_known 'WHEP play'; then
  chrome_test_job_assert webrtc whep 'latest(.inboundVideo.codec) == "video/H264"
    and latest(.inboundAudio.codec) == "audio/opus"' 'WHEP H264/Opus'
  chrome_test_job_assert webrtc whep 'rise(.inboundVideo.framesDecoded) >= 40' 'WHEP frames decoded +40'
  chrome_test_job_assert webrtc whep "($fps) >= 15" 'WHEP 15 fps or more'
  chrome_test_job_assert webrtc whep 'latest(.inboundVideo.frameWidth) == 640
    and latest(.inboundVideo.frameHeight) == 480' 'WHEP 640x480'
  chrome_test_job_assert webrtc whep 'rise(.inboundAudio.totalAudioEnergy) > 0' 'WHEP audio energy'
  chrome_test_startup_freeze webrtc whep WHEP
  chrome_test_freeze_assert webrtc whep 'settled([.samples[]?]) | series(.inboundVideo.freezeCount)
    | length > 0 and last - first == 0' 'WHEP no freeze after its first second' srs
fi

if ! chrome_test_loopback_known 'WHEP play of RTMP'; then
  chrome_test_job_assert webrtc whep-rtmp 'latest(.inboundVideo.codec) == "video/H264"
    and latest(.inboundAudio.codec) == "audio/opus"' 'WHEP of RTMP H264/Opus'
  chrome_test_job_assert webrtc whep-rtmp 'rise(.inboundVideo.framesDecoded) >= 40' 'WHEP of RTMP frames decoded +40'
  chrome_test_job_assert webrtc whep-rtmp 'latest(.inboundVideo.frameWidth) == 768
    and latest(.inboundVideo.frameHeight) == 320' 'WHEP of RTMP 768x320'
  chrome_test_job_assert webrtc whep-rtmp 'rise(.inboundAudio.totalAudioEnergy) > 0' 'WHEP of RTMP audio energy'
fi

chrome_test_job_assert webrtc flv 'rise(.media.currentTime) >= 2' 'FLV of WHIP played 2s'
# The bridge sends video from the keyframe after its video sync and audio from
# its audio sync, seconds apart, while the FLV player joins at once: SRS
# guesses the FLV header from the first media, so it can lack either track.
header=$(grep -o -m 1 'FLV: write header audio=[01], video=[01]' "$CHROME_TEST_DIR/srs.log" || true)
[[ -n "$header" ]] || chrome_test_fail "FLV of WHIP: no FLV header in $CHROME_TEST_DIR/srs.log"
if [[ "$header" == *'video=1' ]]; then
  chrome_test_job_assert webrtc flv 'latest(.media.videoWidth) == 640 and latest(.media.videoHeight) == 480' \
    'FLV of WHIP 640x480'
else
  chrome_test_known F3 'FLV of WHIP has no video: SRS wrote an audio-only FLV header'
fi
if [[ "$header" == *'audio=1'* ]]; then
  chrome_test_job_assert webrtc flv 'latest(.media.audioDecodedBytes) > 0' 'FLV of WHIP AAC audio decoded'
else
  chrome_test_known F3 'FLV of WHIP has no audio: SRS wrote a video-only FLV header'
fi
chrome_test_finish
