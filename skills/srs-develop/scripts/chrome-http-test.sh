#!/bin/bash
# FFmpeg publishes H.264/AAC over RTMP, and Chrome plays it at once over
# HTTP-FLV and HTTP-TS (mpegts.js) and HLS (hls.js), each in its own window.
# Each player must play 3 s or more at the source's size, drop under 10% of
# its frames, and decode audio.
#
# HTTP-TS runs on a second SRS: its mount [vhost]/[app]/[stream].ts also
# matches the HLS segment URLs, such as live/<stream>-8.ts, so one SRS would
# serve those as empty live TS streams and hls.js would stall.
#
# The source's keyframes are up to 6 s apart, and by default SRS also ends an
# HLS segment without a keyframe once it reaches 2.1 fragments. A player that
# opens a few seconds late, such as on the slower Windows test host, then
# starts at a segment with no SPS, and hls.js plays audio only and stalls. So
# the segments end only at keyframes.
set -eu
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
source "$SCRIPT_DIR/chrome-test-lib.sh"

chrome_test_init http 15800
chrome_test_srs 0
chrome_test_start SRS_VHOST_HLS_HLS_AOF_RATIO=10
chrome_test_srs 1
chrome_test_start 'SRS_VHOST_HTTP_REMUX_MOUNT=[vhost]/[app]/[stream].ts' SRS_VHOST_HLS_ENABLED=off

stream=$(chrome_test_stream)
chrome_test_srs 1
chrome_test_publish_ffmpeg "$stream"
ts_url=$(chrome_test_ts_url "$stream")
chrome_test_srs 0
chrome_test_publish_ffmpeg "$stream"
flv_url=$(chrome_test_flv_url "$stream")
hls_url=$(chrome_test_hls_url "$stream")
chrome_test_srs 1
chrome_test_wait_stream "$stream"
chrome_test_srs 0
chrome_test_wait_stream "$stream"
# The first segment ends at the source's second keyframe, 5 s in.
CHROME_API_WAIT=10 chrome_test_wait_hls "$stream"

chrome_test_run http --seconds 5 --mode flv --name flv --url "$flv_url" \
  --mode ts --name ts --url "$ts_url" --mode hls --name hls --url "$hls_url"

# How far the player got from its first frame: currentTime is 0 until then,
# and HLS can start past 0.
played='[.samples[]? | .media.currentTime | select(. != null and . > 0)] | (last // 0) - (first // 0)'
for job in flv ts hls; do
  label=$(echo "$job" | tr '[:lower:]' '[:upper:]')
  chrome_test_job_assert http "$job" "($played) >= 3" "$label played 3s or more"
  chrome_test_job_assert http "$job" 'latest(.media.videoWidth) == 768 and latest(.media.videoHeight) == 320' \
    "$label 768x320"
  chrome_test_job_assert http "$job" 'latest(.media.totalVideoFrames) > 0
    and latest(.media.droppedVideoFrames) / latest(.media.totalVideoFrames) < 0.1' "$label dropped frames under 10%"
  chrome_test_job_assert http "$job" 'rise(.media.audioDecodedBytes) > 0' "$label audio decoded"
done
chrome_test_finish
