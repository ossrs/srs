#!/bin/bash
# Publish synthetic H.264/AAC over SRT from an isolated OBS to source-built SRS.
set -eu
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
source "$SCRIPT_DIR/obs-test-lib.sh"

obs_test_init srt 15200
obs_test_start
obs_test_setup --srt "srt://127.0.0.1:$OBS_SRT_PORT" \
  --srt-streamid "#!::r=live/$OBS_INSTANCE_ID,m=publish"
obs_test_assert_api H264 AAC
obs_test_play http-flv aac
obs_test_finish
