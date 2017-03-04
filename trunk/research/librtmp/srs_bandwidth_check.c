/*
 The MIT License (MIT)
 
 Copyright (c) 2013-2017 SRS(ossrs)
 
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

#include <stdio.h>
#include <stdlib.h>

#include "../../objs/include/srs_librtmp.h"

// srs debug info.
char* ip = NULL;
char* sig = NULL;
int pid = 0, cid = 0;
int major = 0, minor = 0, revision= 0, build = 0;
// bandwidth test data.
int64_t start_time = 0;
int64_t end_time = 0;
int play_kbps = 0;
int publish_kbps = 0;
int play_bytes = 0;
int publish_bytes = 0;
int play_duration = 0;
int publish_duration = 0;

int do_check(srs_rtmp_t rtmp)
{
    int ret = 0;
    
    if ((ret = srs_rtmp_handshake(rtmp)) != 0) {
        srs_human_trace("simple handshake failed.");
        return ret;
    }
    srs_human_trace("simple handshake success");
    
    if ((ret = srs_rtmp_connect_app(rtmp)) != 0) {
        srs_human_trace("connect vhost/app failed.");
        return ret;
    }
    srs_human_trace("connect vhost/app success");
    
    if ((ret = srs_rtmp_get_server_sig(rtmp, &sig)) != 0) {
        srs_human_trace("Retrieve server ID failed, ret=%d", ret);
        return ret;
    }
    if ((ret = srs_rtmp_get_server_id(rtmp, &ip, &pid, &cid)) != 0) {
        srs_human_trace("Retrieve server ID failed, ret=%d", ret);
        return ret;
    }
    if ((ret = srs_rtmp_get_server_version(rtmp, &major, &minor, &revision, &build)) != 0) {
        srs_human_trace("Retrieve server version failed, ret=%d", ret);
        return ret;
    }

    if ((ret = srs_rtmp_play_stream(rtmp)) != 0) {
        srs_human_trace("Play rtmp stream failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = srs_rtmp_bandwidth_check(rtmp,
        &start_time, &end_time, &play_kbps, &publish_kbps,
        &play_bytes, &publish_bytes, &play_duration, &publish_duration)) != 0
    ) {
        srs_human_trace("bandwidth check/test failed.");
        return ret;
    }
    srs_human_trace("bandwidth check/test success");
    
    return ret;
}

int main(int argc, char** argv)
{
    int ret = 0;
    srs_rtmp_t rtmp;
    
    printf("RTMP bandwidth check/test with server.\n");
    printf("srs(ossrs) client librtmp library.\n");
    printf("version: %d.%d.%d\n", srs_version_major(), srs_version_minor(), srs_version_revision());
    
    if (argc <= 1) {
        printf("RTMP bandwidth check/test with server.\n"
            "Usage: %s <rtmp_url>\n"
            "   rtmp_url     RTMP bandwidth url to check. format: rtmp://server:port/app?key=xxx,vhost=xxx\n"
            "For example:\n"
            "   %s rtmp://127.0.0.1:1935/app?key=35c9b402c12a7246868752e2878f7e0e,vhost=bandcheck.srs.com\n"
            "   %s rtmp://127.0.0.1:1935/app?key=35c9b402c12a7246868752e2878f7e0e,vhost=bandcheck.srs.com>/dev/null\n"
            "@remark, output text to stdout, while json to stderr.\n",
            argv[0], argv[0], argv[0]);
        exit(-1);
    }
    
    char url[512];
    snprintf(url, sizeof(url), "%s/%s", argv[1], "livestream");
    rtmp = srs_rtmp_create((const char*)url);
    srs_human_trace("bandwidth check/test url: %s", argv[1]);
    
    if ((ret = do_check(rtmp)) != 0) {
        goto rtmp_destroy;
    }
    
    srs_human_trace("\n%s, %s, %s\n"
        "%s, %d.%d.%d.%d, srs_pid=%d, srs_id=%d\n"
        "duration: %dms(%d+%d)\n"
        "play: %dkbps\n"
        "publish: %dkbps",
        (char*)sig, NULL, NULL,
        (char*)ip, major, minor, revision, build, pid, cid,
        (int)(end_time - start_time), play_duration, publish_duration,
        play_kbps, 
        publish_kbps);
    
rtmp_destroy:
    fprintf(stderr, "{\"code\":%d,"
        "\"srs_server\":\"%s\", "
        "\"srs_primary\":\"%s\", "
        "\"srs_authors\":\"%s\", "
        "\"srs_server_ip\":\"%s\", "
        "\"srs_version\":\"%d.%d.%d.%d\", "
        "\"srs_pid\":%d, "
        "\"srs_id\":%d, "
        "\"duration\":%d, "
        "\"play_duration\":%d, "
        "\"publish_duration\":%d,"
        "\"play_kbps\":%d, "
        "\"publish_kbps\":%d"
        "}",
        ret,
        (char*)sig, NULL, NULL,
        (char*)ip, major, minor, revision, build, pid, cid,
        (int)(end_time - start_time), play_duration, publish_duration,
        play_kbps, publish_kbps);
    srs_rtmp_destroy(rtmp);
    
    srs_human_trace(" ");
    srs_human_trace("completed");
    
    return ret;
}
