/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
#include <srs_utest_config.hpp>

using namespace std;

#include <srs_app_config.hpp>

VOID TEST(ConfigTest, CheckMacros)
{
#ifndef SRS_CONSTS_LOCALHOST
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_PID_FILE
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_COFNIG_FILE
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_MAX_CONNECTIONS
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_HLS_PATH
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_HLS_FRAGMENT
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_HLS_WINDOW
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_DVR_PATH
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_DVR_PLAN_SESSION
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_DVR_PLAN_SEGMENT
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_DVR_PLAN_HSS
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_DVR_PLAN
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_DVR_DURATION
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_TIME_JITTER
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_QUEUE_LENGTH
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_PAUSED_LENGTH
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_BANDWIDTH_INTERVAL
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_BANDWIDTH_LIMIT_KBPS
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_HTTP_MOUNT
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_HTTP_DIR
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_HTTP_STREAM_PORT
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_HTTP_API_PORT
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_HTTP_HEAETBEAT_ENABLED
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_HTTP_HEAETBEAT_INTERVAL
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_HTTP_HEAETBEAT_URL
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_HTTP_HEAETBEAT_INDEX
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_HTTP_HEAETBEAT_SUMMARIES
    EXPECT_TRUE(false);
#endif
#ifndef SRS_STAGE_PLAY_USER_INTERVAL_MS
    EXPECT_TRUE(false);
#endif
#ifndef SRS_STAGE_PUBLISH_USER_INTERVAL_MS
    EXPECT_TRUE(false);
#endif
#ifndef SRS_STAGE_FORWARDER_INTERVAL_MS
    EXPECT_TRUE(false);
#endif
#ifndef SRS_STAGE_ENCODER_INTERVAL_MS
    EXPECT_TRUE(false);
#endif
#ifndef SRS_STAGE_INGESTER_INTERVAL_MS
    EXPECT_TRUE(false);
#endif
#ifndef SRS_STAGE_HLS_INTERVAL_MS
    EXPECT_TRUE(false);
#endif
#ifndef SRS_STAGE_EDGE_INTERVAL_MS
    EXPECT_TRUE(false);
#endif
#ifndef SRS_AUTO_INGEST_TYPE_FILE
    EXPECT_TRUE(false);
#endif
#ifndef SRS_AUTO_INGEST_TYPE_STREAM
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_TRANSCODE_IFORMAT
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_TRANSCODE_OFORMAT
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_LOG_FILE
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONSTS_NULL_FILE
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_FF_LOG_DIR
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_LOG_LEVEL
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_LOG_TANK_CONSOLE
    EXPECT_TRUE(false);
#endif
#ifndef xxxxxxxxxxxxxxxxxxxxxxxx
    EXPECT_TRUE(false);
#endif
#ifndef xxxxxxxxxxxxxxxxxxxxxxxx
    EXPECT_TRUE(false);
#endif
#ifndef xxxxxxxxxxxxxxxxxxxxxxxx
    EXPECT_TRUE(false);
#endif
#ifndef xxxxxxxxxxxxxxxxxxxxxxxx
    EXPECT_TRUE(false);
#endif
#ifndef xxxxxxxxxxxxxxxxxxxxxxxx
    EXPECT_TRUE(false);
#endif
#ifndef xxxxxxxxxxxxxxxxxxxxxxxx
    EXPECT_TRUE(false);
#endif
#ifndef xxxxxxxxxxxxxxxxxxxxxxxx
    EXPECT_TRUE(false);
#endif
#ifndef xxxxxxxxxxxxxxxxxxxxxxxx
    EXPECT_TRUE(false);
#endif
#ifndef xxxxxxxxxxxxxxxxxxxxxxxx
    EXPECT_TRUE(false);
#endif
#ifndef xxxxxxxxxxxxxxxxxxxxxxxx
    EXPECT_TRUE(false);
#endif
#ifndef xxxxxxxxxxxxxxxxxxxxxxxx
    EXPECT_TRUE(false);
#endif
#ifndef xxxxxxxxxxxxxxxxxxxxxxxx
    EXPECT_TRUE(false);
#endif
}
