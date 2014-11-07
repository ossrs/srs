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
gcc srs_h264_raw_publish.c ../../objs/lib/srs_librtmp.a -g -O0 -lstdc++ -o srs_h264_raw_publish
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "../../objs/include/srs_librtmp.h"

#define srs_trace(msg, ...) printf(msg, ##__VA_ARGS__);printf("\n")

int main(int argc, char** argv)
{
    srs_trace("publish raw h.264 as rtmp stream to server like FMLE/FFMPEG/Encoder");
    srs_trace("srs(simple-rtmp-server) client librtmp library.");
    srs_trace("version: %d.%d.%d", srs_version_major(), srs_version_minor(), srs_version_revision());
    
    if (argc <= 2) {
        srs_trace("Usage: %s <h264_raw_file> <rtmp_publish_url>", argv[0]);
        srs_trace("     h264_raw_file: the raw h264 steam file.");
        srs_trace("     rtmp_publish_url: the rtmp publish url.");
        srs_trace("For example:");
        srs_trace("     %s ./720p.h264.raw rtmp://127.0.0.1:1935/live/livestream", argv[0]);
        srs_trace("Where the file: http://winlinvip.github.io/srs.release/3rdparty/720p.h264.raw");
        srs_trace("See: https://github.com/winlinvip/simple-rtmp-server/issues/66");
        exit(-1);
    }
    
    const char* raw_file = argv[1];
    const char* rtmp_url = argv[2];
    srs_trace("raw_file=%s, rtmp_url=%s", raw_file, rtmp_url);
    
    srs_rtmp_t rtmp = srs_rtmp_create(rtmp_url);
    
    if (srs_simple_handshake(rtmp) != 0) {
        srs_trace("simple handshake failed.");
        goto rtmp_destroy;
    }
    srs_trace("simple handshake success");
    
    if (srs_connect_app(rtmp) != 0) {
        srs_trace("connect vhost/app failed.");
        goto rtmp_destroy;
    }
    srs_trace("connect vhost/app success");
    
    if (srs_publish_stream(rtmp) != 0) {
        srs_trace("publish stream failed.");
        goto rtmp_destroy;
    }
    srs_trace("publish stream success");
    
    u_int32_t timestamp = 0;
    for (;;) {
        int type = SRS_RTMP_TYPE_VIDEO;
        timestamp += 40;
        int size = 4096;
        char* data = (char*)malloc(4096);
        
        if (srs_write_packet(rtmp, type, timestamp, data, size) != 0) {
            goto rtmp_destroy;
        }
        srs_trace("sent packet: type=%s, time=%d, size=%d", srs_type2string(type), timestamp, size);
        
        usleep(40 * 1000);
    }
    
rtmp_destroy:
    srs_rtmp_destroy(rtmp);
    
    return 0;
}
