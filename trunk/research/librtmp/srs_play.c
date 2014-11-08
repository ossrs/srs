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
gcc srs_play.c ../../objs/lib/srs_librtmp.a -g -O0 -lstdc++ -o srs_play
*/

#include <stdio.h>
#include <stdlib.h>

#include "../../objs/include/srs_librtmp.h"

int main(int argc, char** argv)
{
    printf("suck rtmp stream like rtmpdump\n");
    printf("srs(simple-rtmp-server) client librtmp library.\n");
    printf("version: %d.%d.%d\n", srs_version_major(), srs_version_minor(), srs_version_revision());
    
    if (argc <= 1) {
        printf("Usage: %s <rtmp_url>\n"
            "   rtmp_url     RTMP stream url to play\n"
            "For example:\n"
            "   %s rtmp://127.0.0.1:1935/live/livestream\n",
            argv[0], argv[0]);
        exit(-1);
    }
    
    srs_trace("rtmp url: %s", argv[1]);
    srs_rtmp_t rtmp = srs_rtmp_create(argv[1]);
    
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
    
    if (srs_play_stream(rtmp) != 0) {
        srs_trace("play stream failed.");
        goto rtmp_destroy;
    }
    srs_trace("play stream success");
    
    for (;;) {
        int type, size;
        u_int32_t timestamp = 0;
        char* data;
        
        if (srs_read_packet(rtmp, &type, &timestamp, &data, &size) != 0) {
            goto rtmp_destroy;
        }
        srs_trace("got packet: type=%s, time=%d, size=%d", 
            srs_type2string(type), timestamp, size);
        
        free(data);
    }
    
rtmp_destroy:
    srs_rtmp_destroy(rtmp);
    
    return 0;
}
