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
/**
gcc srs_bandwidth_check.c ../../objs/lib/srs_librtmp.a -g -O0 -lstdc++ -o srs_bandwidth_check
*/

#include <stdio.h>
#include <stdlib.h>

#include "../../objs/include/srs_librtmp.h"

int main(int argc, char** argv)
{
    int ret = 0;
    srs_rtmp_t rtmp;
    
    // packet data
    int size;
    char type;
    char* data;
    u_int32_t timestamp;
    
    // srs debug info.
    char srs_server_ip[128];
    char srs_server[128];
    char srs_primary[128];
    char srs_authors[128];
    char srs_version[32];
    int srs_id = 0;
    int srs_pid = 0;
    // bandwidth test data.
    int64_t start_time = 0;
    int64_t end_time = 0;
    int play_kbps = 0;
    int publish_kbps = 0;
    int play_bytes = 0;
    int publish_bytes = 0;
    int play_duration = 0;
    int publish_duration = 0;
    
    // set to zero.
    srs_server_ip[0] = 0;
    srs_server[0] = 0;
    srs_primary[0] = 0;
    srs_authors[0] = 0;
    srs_version[0] = 0;
    
    printf("RTMP bandwidth check/test with server.\n");
    printf("srs(simple-rtmp-server) client librtmp library.\n");
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
    
    rtmp = srs_rtmp_create2(argv[1]);
    
    srs_human_trace("bandwidth check/test url: %s", argv[1]);
    
    if ((ret = srs_rtmp_handshake(rtmp)) != 0) {
        srs_human_trace("simple handshake failed.");
        goto rtmp_destroy;
    }
    srs_human_trace("simple handshake success");
    
    if ((ret = srs_rtmp_connect_app2(rtmp, 
        srs_server_ip, srs_server, srs_primary, srs_authors, srs_version, 
        &srs_id, &srs_pid)) != 0) {
        srs_human_trace("connect vhost/app failed.");
        goto rtmp_destroy;
    }
    srs_human_trace("connect vhost/app success");
    
    if ((ret = srs_rtmp_bandwidth_check(rtmp, 
        &start_time, &end_time, &play_kbps, &publish_kbps,
        &play_bytes, &publish_bytes, &play_duration, &publish_duration)) != 0
    ) {
        srs_human_trace("bandwidth check/test failed.");
        goto rtmp_destroy;
    }
    srs_human_trace("bandwidth check/test success");
    
    srs_human_trace("\n%s, %s, %s\n"
        "%s, %s, srs_pid=%d, srs_id=%d\n"
        "duration: %dms(%d+%d)\n"
        "play: %dkbps\n"
        "publish: %dkbps", 
        (char*)srs_server, (char*)srs_primary, (char*)srs_authors,
        (char*)srs_server_ip, (char*)srs_version, srs_pid, srs_id,
        (int)(end_time - start_time), play_duration, publish_duration,
        play_kbps, 
        publish_kbps);
    
rtmp_destroy:
    srs_rtmp_destroy(rtmp);
    
    fprintf(stderr, "{\"code\":%d,"
        "\"srs_server\":\"%s\", "
        "\"srs_primary\":\"%s\", "
        "\"srs_authors\":\"%s\", "
        "\"srs_server_ip\":\"%s\", "
        "\"srs_version\":\"%s\", "
        "\"srs_pid\":%d, "
        "\"srs_id\":%d, "
        "\"duration\":%d, "
        "\"play_duration\":%d, "
        "\"publish_duration\":%d,"
        "\"play_kbps\":%d, "
        "\"publish_kbps\":%d"
        "}",
        ret,
        (char*)srs_server, (char*)srs_primary, (char*)srs_authors,
        (char*)srs_server_ip, (char*)srs_version, srs_pid, srs_id,
        (int)(end_time - start_time), play_duration, publish_duration,
        play_kbps, publish_kbps);
    
    srs_human_trace(" ");
    srs_human_trace("completed");
    
    return ret;
}
