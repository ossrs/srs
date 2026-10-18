#!/bin/bash
# Publish synthetic H.264/AAC from an isolated OBS to source-built SRS.
set -eu
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
source "$SCRIPT_DIR/obs-test-lib.sh"

obs_test_init rtmp 15100
obs_test_start
obs_test_setup --rtmp "rtmp://127.0.0.1:$OBS_RTMP_PORT/live/$OBS_INSTANCE_ID"
obs_test_assert_api H264 AAC
obs_test_play http-flv aac
obs_test_play hls aac
obs_test_finish
