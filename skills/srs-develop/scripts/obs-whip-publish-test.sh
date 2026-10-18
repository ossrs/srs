#!/bin/bash
# Publish synthetic H.264/Opus over WHIP from an isolated OBS to source-built SRS.
# SRS bridges the publish to RTMP (rtc_to_rtmp), so the API and HTTP-FLV show H.264/AAC.
set -eu
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
source "$SCRIPT_DIR/obs-test-lib.sh"

obs_test_init whip 15300
obs_test_start
obs_test_setup --whip "http://127.0.0.1:$OBS_API_PORT/rtc/v1/whip/?app=live&stream=$OBS_INSTANCE_ID" \
  --eip 127.0.0.1
# The API shows 640x360 once the bridge gets a keyframe; OBS x264 sends one
# every 250 frames by default (8.3 s at 30 fps), so wait longer than usual.
OBS_API_WAIT=15 obs_test_assert_api H264 AAC
obs_test_assert_frames
obs_test_play http-flv aac
obs_test_finish
