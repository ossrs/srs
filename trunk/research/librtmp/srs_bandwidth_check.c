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
    int type, size;
    u_int32_t timestamp = 0;
    char* data;
    
    // srs debug info.
    char srs_server[128];
    char srs_primary_authors[128];
    char srs_id[64];
    char srs_pid[64];
    char srs_server_ip[128];
    // bandwidth test data.
    int64_t start_time, end_time;
    int play_kbps, publish_kbps;
    int play_bytes, publish_bytes;
    int play_duration, publish_duration;
    
    if (argc <= 1) {
        printf("RTMP bandwidth check/test with server.\n"
            "Usage: %s <rtmp_url>\n"
            "   rtmp_url     RTMP bandwidth url to check. format: rtmp://server:port/app?key=xxx&&vhost=xxx\n"
            "For example:\n"
            "   %s rtmp://127.0.0.1:1935/app?key=35c9b402c12a7246868752e2878f7e0e,vhost=bandcheck.srs.com\n"
            "@remark, output text to stdout, while json to stderr.\n",
            argv[0], argv[0]);
        ret = 1;
        exit(ret);
        return ret;
    }
    
    rtmp = srs_rtmp_create2(argv[1]);
    
    printf("RTMP bandwidth check/test with server.\n");
    printf("srs(simple-rtmp-server) client librtmp library.\n");
    printf("version: %d.%d.%d\n", srs_version_major(), srs_version_minor(), srs_version_revision());
    printf("bandwidth check/test url: %s\n", argv[1]);
    
    if ((ret = srs_simple_handshake(rtmp)) != 0) {
        printf("simple handshake failed.\n");
        goto rtmp_destroy;
    }
    printf("simple handshake success\n");
    
    if ((ret = srs_connect_app(rtmp)) != 0) {
        printf("connect vhost/app failed.\n");
        goto rtmp_destroy;
    }
    printf("connect vhost/app success\n");
    
    if ((ret = srs_bandwidth_check(rtmp, 
        srs_server, srs_primary_authors, 
        srs_id, srs_pid, srs_server_ip,
        &start_time, &end_time, &play_kbps, &publish_kbps,
        &play_bytes, &publish_bytes, &play_duration, &publish_duration)) != 0
    ) {
        printf("bandwidth check/test failed.\n");
        goto rtmp_destroy;
    }
    printf("bandwidth check/test success\n");
    
    printf("\n%s, %s\n"
        "%s, srs_pid=%s, srs_id=%s\n"
        "duration: %dms(%d+%d)\n"
        "play: %dkbps\n"
        "publish: %dkbps\n\n", 
        (char*)srs_server, (char*)srs_primary_authors,
        (char*)srs_server_ip, (char*)srs_pid, (char*)srs_id,
        (int)(end_time - start_time), play_duration, publish_duration,
        play_kbps, 
        publish_kbps);
    
rtmp_destroy:
    srs_rtmp_destroy(rtmp);
    
    printf("terminate with ret=%d\n", ret);
    return ret;
}
