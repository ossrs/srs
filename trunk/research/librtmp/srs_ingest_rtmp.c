/*
The MIT License (MIT)

Copyright (c) 2013-2016 SRS(ossrs)

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

// whether use verbose log.
int verbose = 0;
// 2000 is about 30s.
#define PITHY_PRINT_EVERY_MSGS 2000

int main(int argc, char** argv)
{
    int ret = 0;
    
    // user option parse index.
    int opt = 0;
    // user options.
    char* in_rtmp_url = NULL;
    char* out_rtmp_url = NULL;
    // rtmp handler
    srs_rtmp_t irtmp, ortmp;
    
    printf("Ingest RTMP to server like FFMPEG over srs-librtmp %d.%d.%d\n",
        srs_version_major(), srs_version_minor(), srs_version_revision());
    
    if (argc <= 2) {
        printf("ingest RTMP and publish to RTMP server\n"
            "Usage: %s <-i in_rtmp_url> <-y out_rtmp_url> [-v verbose]\n"
            "   in_rtmp_url     input rtmp url, ingest from this url.\n"
            "   out_rtmp_url    output rtmp url, publish to this url.\n"
            "   verbose         output verbose log.\n"
           "For example:\n"
           "   %s -i rtmp://127.0.0.1/live/livestream -y rtmp://127.0.0.1/live/demo\n"
           "   %s -i rtmp://127.0.0.1/live/livestream -y rtmp://127.0.0.1/live/demo -v verbose\n",
            argv[0], argv[0], argv[0]);
        exit(-1);
    }
    
    // fill the options for mac
    for (opt = 0; opt < argc - 1; opt++) {
        // ignore all options except -i and -y.
        char* p = argv[opt];
        
        // only accept -x
        if (p[0] != '-' || p[1] == 0 || p[2] != 0) {
            continue;
        }
        
        // parse according the option name.
        switch (p[1]) {
            case 'i': in_rtmp_url = argv[++opt]; break;
            case 'y': out_rtmp_url = argv[++opt]; break;
            case 'v': verbose=1; opt++; break;
            default: break;
        }
    }
    
    if (!in_rtmp_url) {
        srs_human_trace("input invalid, use -i <input>");
        return -1;
    }
    if (!out_rtmp_url) {
        srs_human_trace("output invalid, use -y <output>");
        return -1;
    }
    
    srs_human_trace("ingest %s to %s, verbose=%d", in_rtmp_url, out_rtmp_url, verbose);
    
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
    u_int64_t nb_msgs = 0;

    if ((ret = connect_ic(irtmp)) != 0) {
        return ret;
    }
    if ((ret = connect_oc(ortmp)) != 0) {
        return ret;
    }
    
    if (verbose) {
        srs_human_trace("start proxy RTMP stream");
    } else {
        srs_human_verbose("start proxy RTMP stream");
    }
    for (;;) {
        if ((ret = srs_rtmp_read_packet(irtmp, &type, &timestamp, &data, &size)) != 0) {
            srs_human_trace("irtmp get packet failed. ret=%d", ret);
            return ret;
        }
        
        if (!srs_utils_flv_tag_is_ok(type)) {
            if (verbose) {
                srs_human_trace("ignore invalid flv tag=%d, dts=%d, %d bytes", type, timestamp, size);
            } else {
                srs_human_verbose("ignore invalid flv tag=%d, dts=%d, %d bytes", type, timestamp, size);
            }
            free(data);
            continue;
        }
        
        if (verbose || ((nb_msgs++ % PITHY_PRINT_EVERY_MSGS) == 0 && nb_msgs > 10)) {
            if ((ret = srs_human_print_rtmp_packet(type, timestamp, data, size)) != 0) {
                srs_human_trace("print packet failed. ret=%d", ret);
                return ret;
            }
        }
        
        if ((ret = srs_rtmp_write_packet(ortmp, type, timestamp, data, size)) != 0) {
            srs_human_trace("irtmp get packet failed. ret=%d", ret);
            return ret;
        }
        
        if (verbose) {
            srs_human_trace("ortmp sent packet: type=%s, time=%d, size=%d",
                srs_human_flv_tag_type2string(type), timestamp, size);
        } else {
            srs_human_verbose("ortmp sent packet: type=%s, time=%d, size=%d",
                srs_human_flv_tag_type2string(type), timestamp, size);
        }
    }
    
    return ret;
}

int connect_ic(srs_rtmp_t irtmp)
{
    int ret = 0;
    
    // srs debug info.
    char srs_server_ip[128];
    char srs_server[128];
    char srs_primary[128];
    char srs_authors[128];
    char srs_version[32];
    int srs_id = 0;
    int srs_pid = 0;
    
    // set to zero.
    srs_server_ip[0] = 0;
    srs_server[0] = 0;
    srs_primary[0] = 0;
    srs_authors[0] = 0;
    srs_version[0] = 0;
    
    if ((ret = srs_rtmp_handshake(irtmp)) != 0) {
        srs_human_trace("irtmp simple handshake failed. ret=%d", ret);
        return ret;
    }
    if (verbose) {
        srs_human_trace("irtmp simple handshake success");
    } else {
        srs_human_verbose("irtmp simple handshake success");
    }
    
    if (srs_rtmp_connect_app2(irtmp,
        srs_server_ip, srs_server, srs_primary, srs_authors, srs_version,
        &srs_id, &srs_pid) != 0) {
        srs_human_trace("irtmp connect vhost/app failed. ret=%d", ret);
        return ret;
    }
    srs_human_trace("irtmp connect ok, ip=%s, server=%s/%s, pid=%d, cid=%d", srs_server_ip, srs_server, srs_version, srs_pid, srs_id);
    
    if ((ret = srs_rtmp_play_stream(irtmp)) != 0) {
        srs_human_trace("irtmp play stream failed. ret=%d", ret);
        return ret;
    }
    if (verbose) {
        srs_human_trace("irtmp play stream success");
    } else {
        srs_human_verbose("irtmp play stream success");
    }
    
    return ret;
}

int connect_oc(srs_rtmp_t ortmp)
{
    int ret = 0;
    
    // srs debug info.
    char srs_server_ip[128];
    char srs_server[128];
    char srs_primary[128];
    char srs_authors[128];
    char srs_version[32];
    int srs_id = 0;
    int srs_pid = 0;
    
    // set to zero.
    srs_server_ip[0] = 0;
    srs_server[0] = 0;
    srs_primary[0] = 0;
    srs_authors[0] = 0;
    srs_version[0] = 0;
    
    if ((ret = srs_rtmp_handshake(ortmp)) != 0) {
        srs_human_trace("ortmp simple handshake failed. ret=%d", ret);
        return ret;
    }
    if (verbose) {
        srs_human_trace("ortmp simple handshake success");
    } else {
        srs_human_verbose("ortmp simple handshake success");
    }
    
    if (srs_rtmp_connect_app2(ortmp,
        srs_server_ip, srs_server, srs_primary, srs_authors, srs_version,
        &srs_id, &srs_pid) != 0) {
        srs_human_trace("ortmp connect vhost/app failed. ret=%d", ret);
        return ret;
    }
    srs_human_trace("ortmp connect ok, ip=%s, server=%s/%s, pid=%d, cid=%d", srs_server_ip, srs_server, srs_version, srs_pid, srs_id);
    
    if ((ret = srs_rtmp_publish_stream(ortmp)) != 0) {
        srs_human_trace("ortmp publish stream failed. ret=%d", ret);
        return ret;
    }
    if (verbose) {
        srs_human_trace("ortmp publish stream success");
    } else {
        srs_human_verbose("ortmp publish stream success");
    }
    
    return ret;
}
