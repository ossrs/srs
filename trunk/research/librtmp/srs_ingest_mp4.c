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

int proxy(srs_mp4_t mp4, srs_rtmp_t ortmp);
int main(int argc, char** argv)
{
    printf("Ingest mp4 file and publish to RTMP server like FFMPEG.\n");
    printf("SRS(OSSRS) client librtmp library.\n");
    printf("Version: %d.%d.%d\n", srs_version_major(), srs_version_minor(), srs_version_revision());
    
    if (argc <= 2) {
        printf("Ingest mp4 file and publish to RTMP server\n"
               "Usage: %s <-i in_mp4_file> <-y out_rtmp_url>\n"
               "   in_mp4_file     input mp4 file, ingest from this file.\n"
               "   out_rtmp_url    output rtmp url, publish to this url.\n"
               "@remark Only support non-seek mp4, see https://github.com/ossrs/srs/issues/738#issuecomment-276343669\n"
               "For example:\n"
               "   %s -i avatar.mp4 -y rtmp://127.0.0.1/live/livestream\n",
               argv[0], argv[0]);
        exit(-1);
    }
    
    for (int opt = 0; opt < argc; opt++) {
        srs_human_trace("The argv[%d]=%s", opt, argv[opt]);
    }
    
    // fill the options for mac
    char* in_file = NULL;
    char* out_rtmp_url = NULL;
    for (int opt = 0; opt < argc - 1; opt++) {
        // ignore all options except -i and -y.
        char* p = argv[opt];
        
        // only accept -x
        if (p[0] != '-' || p[1] == 0 || p[2] != 0) {
            continue;
        }
        
        // parse according the option name.
        switch (p[1]) {
            case 'i': in_file = argv[opt + 1]; break;
            case 'y': out_rtmp_url = argv[opt + 1]; break;
            default: break;
        }
    }
    
    if (!in_file) {
        srs_human_trace("Invalid input file, use -i <input>");
        return -1;
    }
    if (!out_rtmp_url) {
        srs_human_trace("Invalid output url, use -y <output>");
        return -1;
    }
    
    srs_human_trace("Input file: %s", in_file);
    srs_human_trace("Output url: %s", out_rtmp_url);
    
    int ret = 0;
    
    srs_mp4_t mp4 = NULL;
    if ((mp4 = srs_mp4_open_read(in_file)) == NULL) {
        ret = 2;
        srs_human_trace("open mp4 file failed. ret=%d", ret);
        return ret;
    }
    
    srs_rtmp_t ortmp = srs_rtmp_create(out_rtmp_url);
    
    ret = proxy(mp4, ortmp);
    srs_human_trace("Ingest mp4 to RTMP ok.");
    
    srs_rtmp_destroy(ortmp);
    srs_mp4_close(mp4);
    
    return ret;
}

int connect_oc(srs_rtmp_t ortmp)
{
    int ret = 0;
    
    if ((ret = srs_rtmp_handshake(ortmp)) != 0) {
        srs_human_trace("ortmp simple handshake failed. ret=%d", ret);
        return ret;
    }
    srs_human_trace("ortmp simple handshake success");
    
    if ((ret = srs_rtmp_connect_app(ortmp)) != 0) {
        srs_human_trace("ortmp connect vhost/app failed. ret=%d", ret);
        return ret;
    }
    srs_human_trace("ortmp connect vhost/app success");
    
    if ((ret = srs_rtmp_publish_stream(ortmp)) != 0) {
        srs_human_trace("ortmp publish stream failed. ret=%d", ret);
        return ret;
    }
    srs_human_trace("ortmp publish stream success");
    
    return ret;
}

#define RE_PULSE_MS 300
#define RE_PULSE_JITTER_MS 3000
int64_t tools_main_entrance_startup_time;

int64_t re_create()
{
    // if not very precise, we can directly use this as re.
    int64_t re = srs_utils_time_ms();
    
    // use the starttime to get the deviation
    int64_t deviation = re - tools_main_entrance_startup_time;
    srs_human_trace("deviation is %d ms, pulse is %d ms", (int)(deviation), (int)(RE_PULSE_MS));
    
    // so, we adjust time to max(0, deviation)
    // because the last pulse, we already sleeped
    int adjust = (int)(deviation);
    if (adjust > 0) {
        srs_human_trace("adjust re time for %d ms", adjust);
        re -= adjust;
    } else {
        srs_human_trace("no need to adjust re time");
    }
    
    return re;
}

void re_update(int64_t re, int32_t starttime, uint32_t time)
{
    // send by pulse algorithm.
    int64_t now = srs_utils_time_ms();
    int64_t diff = time - starttime - (now -re);
    if (diff > RE_PULSE_MS && diff < RE_PULSE_JITTER_MS) {
        usleep((useconds_t)(diff * 1000));
    }
}

void re_cleanup(int64_t re, int32_t starttime, uint32_t time)
{
    // for the last pulse, always sleep.
    // for the virtual live encoder long time publishing.
    int64_t now = srs_utils_time_ms();
    int64_t diff = time - starttime - (now -re);
    if (diff > 0) {
        srs_human_trace("re_cleanup, diff=%d, start=%d, last=%d ms",
                        (int)diff, starttime, time);
        usleep((useconds_t)(diff * 1000));
    }
}

int do_proxy(srs_mp4_t mp4, srs_rtmp_t ortmp, int64_t re, int32_t* pstarttime, uint32_t* ptimestamp)
{
    int ret = 0;
    
    // packet data
    char type;
    int size;
    char* data = NULL;
    
    srs_human_trace("start ingest mp4 to RTMP stream");
    for (;;) {
#if 0
        // tag header
        if ((ret = srs_flv_read_tag_header(flv, &type, &size, ptimestamp)) != 0) {
            if (srs_flv_is_eof(ret)) {
                srs_human_trace("parse completed.");
                return 0;
            }
            srs_human_trace("flv get packet failed. ret=%d", ret);
            return ret;
        }
        
        if (size <= 0) {
            srs_human_trace("invalid size=%d", size);
            break;
        }
        
        // TODO: FIXME: mem leak when error.
        data = (char*)malloc(size);
        if ((ret = srs_flv_read_tag_data(flv, data, size)) != 0) {
            return ret;
        }
#endif
        uint32_t timestamp = *ptimestamp;
        
        if ((ret = srs_human_print_rtmp_packet(type, timestamp, data, size)) != 0) {
            srs_human_trace("print packet failed. ret=%d", ret);
            return ret;
        }
        
        if ((ret = srs_rtmp_write_packet(ortmp, type, *ptimestamp, data, size)) != 0) {
            srs_human_trace("irtmp get packet failed. ret=%d", ret);
            return ret;
        }
        
        if (*pstarttime < 0 && srs_utils_flv_tag_is_av(type)) {
            *pstarttime = *ptimestamp;
        }
        
        re_update(re, *pstarttime, *ptimestamp);
    }
    
    return ret;
}

int proxy(srs_mp4_t mp4, srs_rtmp_t ortmp)
{
    int ret = 0;
    
    if ((ret = srs_mp4_init_demuxer(mp4)) != 0) {
        return ret;
    }
    if ((ret = connect_oc(ortmp)) != 0) {
        return ret;
    }
    
    int64_t re = re_create();
    
    uint32_t timestamp = 0;
    int32_t starttime = -1;
    ret = do_proxy(mp4, ortmp, re, &starttime, &timestamp);
    
    // for the last pulse, always sleep.
    re_cleanup(re, starttime, timestamp);
    
    return ret;
}

