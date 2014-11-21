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
gcc srs_detect_rtmp.c ../../objs/lib/srs_librtmp.a -g -O0 -lstdc++ -o srs_detect_rtmp
*/

#include <stdio.h>
#include <stdlib.h>

#include "../../objs/include/srs_librtmp.h"

int main(int argc, char** argv)
{
    int ret = 0;
    srs_rtmp_t rtmp;
    
    // time
    int64_t time_startup = srs_get_time_ms();
    int64_t time_dns_resolve = 0;
    int64_t time_socket_connect = 0;
    int64_t time_play_stream = 0;
    int64_t time_first_packet = 0;
    int64_t time_cleanup = 0;
    // delay = actual - expect time when quit.
    int delay = 0;
    // bytes
    int64_t bytes_nsend = 0;
    int time_duration = 0;
    int64_t bytes_nrecv = 0;
    
    // packet data
    int size;
    char type;
    char* data;
    u_int32_t timestamp;
    u_int32_t basetime = 0;
    
    // user options
    const char* rtmp_url = NULL;
    int duration = 0;
    int timeout = 0;

    printf("detect rtmp stream\n");
    printf("srs(simple-rtmp-server) client librtmp library.\n");
    printf("version: %d.%d.%d\n", srs_version_major(), srs_version_minor(), srs_version_revision());
    
    if (argc <= 3) {
        printf("detect stream on RTMP server, print result to stderr.\n"
            "Usage: %s <rtmp_url> <duration> <timeout>\n"
            "   rtmp_url     RTMP stream url to play\n"
            "   duration     how long to play, in seconds, stream time.\n"
            "   timeout      how long to timeout, in seconds, system time.\n"
            "For example:\n"
            "   %s rtmp://127.0.0.1:1935/live/livestream 3 10\n",
            argv[0], argv[0]);
        exit(-1);
    }
    
    rtmp_url = argv[1];
    duration = atoi(argv[2]);
    timeout = atoi(argv[3]);
    
    srs_human_trace("rtmp url: %s", rtmp_url);
    srs_human_trace("duration: %ds, timeout:%ds", duration, timeout);
    
    if (duration <= 0 || timeout <= 0) {
        srs_human_trace("duration and timeout must be positive.");
        exit(-2);
    }
    
    rtmp = srs_rtmp_create(rtmp_url);
    
    if ((ret = __srs_dns_resolve(rtmp)) != 0) {
        srs_human_trace("dns resolve failed. ret=%d", ret);
        goto rtmp_destroy;
    }
    srs_human_trace("dns resolve success");
    time_dns_resolve = srs_get_time_ms();
    
    if ((ret = __srs_connect_server(rtmp)) != 0) {
        srs_human_trace("socket connect failed. ret=%d", ret);
        goto rtmp_destroy;
    }
    srs_human_trace("socket connect success");
    time_socket_connect = srs_get_time_ms();
    
    if ((ret = __srs_do_simple_handshake(rtmp)) != 0) {
        srs_human_trace("do simple handshake failed. ret=%d", ret);
        goto rtmp_destroy;
    }
    srs_human_trace("do simple handshake success");
    
    if ((ret = srs_connect_app(rtmp)) != 0) {
        srs_human_trace("connect vhost/app failed. ret=%d", ret);
        goto rtmp_destroy;
    }
    srs_human_trace("connect vhost/app success");
    
    if ((ret = srs_play_stream(rtmp)) != 0) {
        srs_human_trace("play stream failed. ret=%d", ret);
        goto rtmp_destroy;
    }
    srs_human_trace("play stream success");
    time_play_stream = srs_get_time_ms();
    
    for (;;) {
        if ((ret = srs_read_packet(rtmp, &type, &timestamp, &data, &size)) != 0) {
            srs_human_trace("read packet failed. ret=%d", ret);
            goto rtmp_destroy;
        }
        srs_human_trace("got packet: type=%s, time=%d, size=%d", 
            srs_human_flv_tag_type2string(type), timestamp, size);
        
        if (SRS_RTMP_TYPE_VIDEO == type || SRS_RTMP_TYPE_AUDIO == type) {
            if (time_first_packet <= 0) {
                time_first_packet = srs_get_time_ms();
            }
            if (basetime <= 0) {
                basetime = timestamp;
            }
        }
        
        free(data);
        
        if (srs_get_time_ms() - time_startup > timeout * 1000) {
            srs_human_trace("timeout, terminate.");
            goto rtmp_destroy;
        }
        
        if ((timestamp - basetime) > duration * 1000) {
            srs_human_trace("duration exceed, terminate.");
            goto rtmp_destroy;
        }
    }
    
rtmp_destroy:
    bytes_nsend = srs_get_nsend_bytes(rtmp);
    bytes_nrecv = srs_get_nrecv_bytes(rtmp);
    
    srs_rtmp_destroy(rtmp);
    time_cleanup = srs_get_time_ms();
    time_duration = (int)(time_cleanup - time_startup);
    
    // print result to stderr.
    fprintf(stderr, "{"
        "\"%s\":%d, " //#0
        "\"%s\":%d, " //#1
        "\"%s\":%d, " // #2
        "\"%s\":%d, " // #3
        "\"%s\":%d, " // #4
        "\"%s\":%d, " // #5
        "\"%s\":%d, " // #6
        "\"%s\":%d, " // #7
        "\"%s\":%d, " // #8
        "\"%s\":%d, " // #9
        "\"%s\":%d, " // #10
        "%s,%s,%s,%s}",
        "code", ret, //#0
        // total = dns + tcp_connect + start_play + first_packet + last_packet
        "total", time_duration, //#1
        "dns", (int)(time_dns_resolve - time_startup), //#2
        "tcp_connect", (int)(time_socket_connect - time_dns_resolve), //#3
        "start_play", (int)(time_play_stream - time_socket_connect), //#4
        "first_packet", (int)(time_first_packet - time_play_stream), //#5
        "last_packet", (int)(time_cleanup - time_first_packet), //#6
        "stream", (int)(timestamp - basetime), //#7
        // expect = time_cleanup - time_first_packet
        // actual = stream
        // delay = actual - expect
        "delay", (int)(timestamp - basetime - (time_cleanup - time_first_packet)), //#8
        "publish_kbps", (int)((time_duration <= 0)? 0:(bytes_nsend * 8 / time_duration)), //#9
        "play_kbps", (int)((time_duration <= 0)? 0:(bytes_nrecv * 8 / time_duration)), //#10
        // unit in ms.
        "\"unit\": \"ms\"",
        "\"remark0\": \"total = dns + tcp_connect + start_play + first_packet + last_packet\"",
        "\"remark1\": \"delay = stream - (time_cleanup - time_first_packet)\"",
        "\"remark2\": \"if code is not 0, user must ignore all data\""
    );
    
    srs_human_trace("");
    srs_human_trace("completed");
    
    return ret;
}
