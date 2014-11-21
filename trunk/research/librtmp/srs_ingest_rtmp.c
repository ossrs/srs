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
gcc srs_ingest_rtmp.c ../../objs/lib/srs_librtmp.a -g -O0 -lstdc++ -o srs_ingest_rtmp
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "../../objs/include/srs_librtmp.h"

int connect_ic(srs_rtmp_t irtmp);
int connect_oc(srs_rtmp_t ortmp);
int proxy(srs_rtmp_t irtmp, srs_rtmp_t ortmp);

int main(int argc, char** argv)
{
    int ret = 0;
    
    // user option parse index.
    int opt = 0;
    // user options.
    char* in_rtmp_url; char* out_rtmp_url;
    // rtmp handler
    srs_rtmp_t irtmp, ortmp;
    
    printf("ingest RTMP and publish to RTMP server like edge.\n");
    printf("srs(simple-rtmp-server) client librtmp library.\n");
    printf("version: %d.%d.%d\n", srs_version_major(), srs_version_minor(), srs_version_revision());
    
    if (argc <= 2) {
        printf("ingest RTMP and publish to RTMP server\n"
            "Usage: %s <-i in_rtmp_url> <-y out_rtmp_url>\n"
            "   in_rtmp_url     input rtmp url, ingest from this url.\n"
            "   out_rtmp_url    output rtmp url, publish to this url.\n"
            "For example:\n"
            "   %s -i rtmp://127.0.0.1/live/livestream -y rtmp://127.0.0.1/live/demo\n",
            argv[0], argv[0]);
        exit(-1);
    }
    
    // parse options in FFMPEG format.
    while ((opt = getopt(argc, argv, "i:y:")) != -1) {
        switch (opt) {
            case 'i':
                in_rtmp_url = optarg;
                break;
            case 'y':
                out_rtmp_url = optarg;
                break;
            default:
                break;
        }
    }
    
    srs_human_trace("input:  %s", in_rtmp_url);
    srs_human_trace("output: %s", out_rtmp_url);
    
    irtmp = srs_rtmp_create(in_rtmp_url);
    ortmp = srs_rtmp_create(out_rtmp_url);

    ret = proxy(irtmp, ortmp);
    srs_human_trace("proxy completed");
    
    srs_rtmp_destroy(irtmp);
    srs_rtmp_destroy(ortmp);
    
    return ret;
}

int proxy(srs_rtmp_t irtmp, srs_rtmp_t ortmp)
{
    int ret = 0;
    
    // packet data
    int size;
    char type;
    char* data;
    u_int32_t timestamp;

    if ((ret = connect_ic(irtmp)) != 0) {
        return ret;
    }
    if ((ret = connect_oc(ortmp)) != 0) {
        return ret;
    }
    
    srs_human_trace("start proxy RTMP stream");
    for (;;) {
        if ((ret = srs_read_packet(irtmp, &type, &timestamp, &data, &size)) != 0) {
            srs_human_trace("irtmp get packet failed. ret=%d", ret);
            return ret;
        }
        
        if ((ret = srs_human_print_rtmp_packet(type, timestamp, data, size)) != 0) {
            srs_human_trace("print packet failed. ret=%d", ret);
            return ret;
        }
        
        if ((ret = srs_write_packet(ortmp, type, timestamp, data, size)) != 0) {
            srs_human_trace("irtmp get packet failed. ret=%d", ret);
            return ret;
        }
        srs_human_verbose("ortmp sent packet: type=%s, time=%d, size=%d", 
            srs_human_flv_tag_type2string(type), timestamp, size);
    }
    
    return ret;
}

int connect_ic(srs_rtmp_t irtmp)
{
    int ret = 0;
    
    if ((ret = srs_simple_handshake(irtmp)) != 0) {
        srs_human_trace("irtmp simple handshake failed. ret=%d", ret);
        return ret;
    }
    srs_human_trace("irtmp simple handshake success");
    
    if ((ret = srs_connect_app(irtmp)) != 0) {
        srs_human_trace("irtmp connect vhost/app failed. ret=%d", ret);
        return ret;
    }
    srs_human_trace("irtmp connect vhost/app success");
    
    if ((ret = srs_play_stream(irtmp)) != 0) {
        srs_human_trace("irtmp play stream failed. ret=%d", ret);
        return ret;
    }
    srs_human_trace("irtmp play stream success");
    
    return ret;
}

int connect_oc(srs_rtmp_t ortmp)
{
    int ret = 0;
    
    if ((ret = srs_simple_handshake(ortmp)) != 0) {
        srs_human_trace("ortmp simple handshake failed. ret=%d", ret);
        return ret;
    }
    srs_human_trace("ortmp simple handshake success");
    
    if ((ret = srs_connect_app(ortmp)) != 0) {
        srs_human_trace("ortmp connect vhost/app failed. ret=%d", ret);
        return ret;
    }
    srs_human_trace("ortmp connect vhost/app success");
    
    if ((ret = srs_publish_stream(ortmp)) != 0) {
        srs_human_trace("ortmp publish stream failed. ret=%d", ret);
        return ret;
    }
    srs_human_trace("ortmp publish stream success");
    
    return ret;
}
