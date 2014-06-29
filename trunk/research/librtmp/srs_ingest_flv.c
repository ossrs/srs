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
gcc srs_ingest_flv.c ../../objs/lib/srs_librtmp.a -g -O0 -lstdc++ -o srs_ingest_flv
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "../../objs/include/srs_librtmp.h"
#include "srs_research_public.h"

int proxy(srs_flv_t flv, srs_rtmp_t ortmp);
int connect_oc(srs_rtmp_t ortmp);

#define RE_PULSE_MS 300
int64_t re_create();
void re_update(int64_t re, u_int32_t time);
void re_cleanup(int64_t re, u_int32_t time);

int64_t tools_main_entrance_startup_time;
int main(int argc, char** argv)
{
    int ret = 0;
    
    // main function
    tools_main_entrance_startup_time = srs_get_time_ms();
    
    // user option parse index.
    int opt = 0;
    // user options.
    char* in_flv_file; char* out_rtmp_url;
    // rtmp handler
    srs_rtmp_t ortmp;
    // flv handler
    srs_flv_t flv;
    
    if (argc <= 2) {
        printf("ingest flv file and publish to RTMP server\n"
            "Usage: %s <-i in_flv_file> <-y out_rtmp_url>\n"
            "   in_flv_file     input flv file, ingest from this file.\n"
            "   out_rtmp_url    output rtmp url, publish to this url.\n"
            "For example:\n"
            "   %s -i ../../doc/source.200kbps.768x320.flv -y rtmp://127.0.0.1/live/demo\n",
            argv[0], argv[0]);
        ret = 1;
        exit(ret);
        return ret;
    }
    
    // parse options in FFMPEG format.
    while ((opt = getopt(argc, argv, "i:y:")) != -1) {
        switch (opt) {
            case 'i':
                in_flv_file = optarg;
                break;
            case 'y':
                out_rtmp_url = optarg;
                break;
            default:
                break;
        }
    }
    
    trace("ingest flv file and publish to RTMP server like FFMPEG.");
    trace("srs(simple-rtmp-server) client librtmp library.");
    trace("version: %d.%d.%d", srs_version_major(), srs_version_minor(), srs_version_revision());
    trace("input:  %s", in_flv_file);
    trace("output: %s", out_rtmp_url);

    if ((flv = srs_flv_open_read(in_flv_file)) == NULL) {
        ret = 2;
        trace("open flv file failed. ret=%d", ret);
        return ret;
    }
    
    ortmp = srs_rtmp_create(out_rtmp_url);

    ret = proxy(flv, ortmp);
    trace("ingest flv to RTMP completed");
    
    srs_rtmp_destroy(ortmp);
    srs_flv_close(flv);
    
    return ret;
}

int do_proxy(srs_flv_t flv, srs_rtmp_t ortmp, int64_t re, u_int32_t* ptimestamp)
{
    int ret = 0;
    
    // packet data
    char type;
    int size;
    char* data = NULL;
    
    trace("start ingest flv to RTMP stream");
    for (;;) {
        // tag header
        if ((ret = srs_flv_read_tag_header(flv, &type, &size, ptimestamp)) != 0) {
            if (srs_flv_is_eof(ret)) {
                trace("parse completed.");
                return 0;
            }
            trace("flv get packet failed. ret=%d", ret);
            return ret;
        }
        
        if (size <= 0) {
            trace("invalid size=%d", size);
            break;
        }
        
        // TODO: FIXME: mem leak when error.
        data = (char*)malloc(size);
        if ((ret = srs_flv_read_tag_data(flv, data, size)) != 0) {
            return ret;
        }
        
        if ((ret = srs_write_packet(ortmp, type, *ptimestamp, data, size)) != 0) {
            trace("irtmp get packet failed. ret=%d", ret);
            return ret;
        }
        verbose("ortmp sent packet: type=%s, time=%d, size=%d", 
            srs_type2string(type), *ptimestamp, size);
        
        re_update(re, *ptimestamp);
    }
    
    return ret;
}

int proxy(srs_flv_t flv, srs_rtmp_t ortmp)
{
    int ret = 0;
    u_int32_t timestamp = 0;
    
    char header[13];
    if ((ret = srs_flv_read_header(flv, header)) != 0) {
        return ret;
    }
    if ((ret = connect_oc(ortmp)) != 0) {
        return ret;
    }
    
    int64_t re = re_create();
    
    ret = do_proxy(flv, ortmp, re, &timestamp);
    
    // for the last pulse, always sleep.
    re_cleanup(re, timestamp);
    
    return ret;
}

int connect_oc(srs_rtmp_t ortmp)
{
    int ret = 0;
    
    if ((ret = srs_simple_handshake(ortmp)) != 0) {
        trace("ortmp simple handshake failed. ret=%d", ret);
        return ret;
    }
    trace("ortmp simple handshake success");
    
    if ((ret = srs_connect_app(ortmp)) != 0) {
        trace("ortmp connect vhost/app failed. ret=%d", ret);
        return ret;
    }
    trace("ortmp connect vhost/app success");
    
    if ((ret = srs_publish_stream(ortmp)) != 0) {
        trace("ortmp publish stream failed. ret=%d", ret);
        return ret;
    }
    trace("ortmp publish stream success");
    
    return ret;
}

int64_t re_create()
{
    // if not very precise, we can directly use this as re.
    int64_t re = srs_get_time_ms();
    
    // use the starttime to get the deviation
    int64_t deviation = re - tools_main_entrance_startup_time;
    trace("deviation is %d ms, pulse is %d ms", (int)(deviation), (int)(RE_PULSE_MS));
    
    // so, we adjust time to max(0, deviation)
    // because the last pulse, we already sleeped
    int adjust = (int)(deviation);
    if (adjust > 0) {
        trace("adjust re time for %d ms", adjust);
        re -= adjust;
    } else {
        trace("no need to adjust re time");
    }
    
    return re;
}
void re_update(int64_t re, u_int32_t time)
{
    // send by pulse algorithm.
    int64_t now = srs_get_time_ms();
    int64_t diff = time - (now -re);
    if (diff > RE_PULSE_MS) {
        usleep(diff * 1000);
    }
}
void re_cleanup(int64_t re, u_int32_t time)
{
    // for the last pulse, always sleep.
    // for the virtual live encoder long time publishing.
    int64_t now = srs_get_time_ms();
    int64_t diff = time - (now -re);
    if (diff > 0) {
        trace("re_cleanup sleep for the last pulse for %d ms", (int)diff);
        usleep(diff * 1000);
    }
}
