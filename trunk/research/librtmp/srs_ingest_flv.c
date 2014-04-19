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

int proxy(int flv_fd, srs_rtmp_t ortmp);
int connect_oc(srs_rtmp_t ortmp);

int open_flv_file(char* in_flv_file);
void close_flv_file(int flv_fd);
int flv_open_ic(int flv_fd);
int flv_read_packet(int flv_fd, int* type, u_int32_t* timestamp, char** data, int* size);

int64_t re_create();
int64_t re_update(int64_t re, u_int32_t time);

int main(int argc, char** argv)
{
    int ret = 0;
    
    // user option parse index.
    int opt = 0;
    // user options.
    char* in_flv_file; char* out_rtmp_url;
    // rtmp handler
    srs_rtmp_t ortmp;
    // flv handler
    int flv_fd;
    
    if (argc <= 2) {
        printf("ingest flv file and publish to RTMP server\n"
            "Usage: %s <-i in_flv_file> <-y out_rtmp_url>\n"
            "   in_flv_file     input flv file, ingest from this file.\n"
            "   out_rtmp_url    output rtmp url, publish to this url.\n"
            "For example:\n"
            "   %s -i ../../doc/source.200kbps.768x320.flv -y rtmp://127.0.0.1/live/demo\n",
            argv[0]);
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

    flv_fd = open_flv_file(in_flv_file);
    if (flv_fd <= 0) {
        ret = 2;
        trace("open flv file failed. ret=%d", ret);
        return ret;
    }
    
    ortmp = srs_rtmp_create(out_rtmp_url);

    ret = proxy(flv_fd, ortmp);
    trace("ingest flv to RTMP completed");
    
    srs_rtmp_destroy(ortmp);
    close_flv_file(flv_fd);
    
    return ret;
}

int proxy(int flv_fd, srs_rtmp_t ortmp)
{
    int ret = 0;
    
    // packet data
    int type, size;
    u_int32_t timestamp = 0;
    char* data = NULL;
    // re
    int64_t re = re_create();
    
    if ((ret = flv_open_ic(flv_fd)) != 0) {
        return ret;
    }
    if ((ret = connect_oc(ortmp)) != 0) {
        return ret;
    }
    
    trace("start ingest flv to RTMP stream");
    for (;;) {
        if ((ret = flv_read_packet(flv_fd, &type, &timestamp, &data, &size)) != 0) {
            trace("irtmp get packet failed. ret=%d", ret);
            return ret;
        }
        verbose("irtmp got packet: type=%s, time=%d, size=%d", 
            srs_type2string(type), timestamp, size);
        
        if ((ret = srs_write_packet(ortmp, type, timestamp, data, size)) != 0) {
            trace("irtmp get packet failed. ret=%d", ret);
            return ret;
        }
        verbose("ortmp sent packet: type=%s, time=%d, size=%d", 
            srs_type2string(type), timestamp, size);
        
        re = re_update(re, timestamp);
    }
    
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
    return 0;
}
int64_t re_update(int64_t re, u_int32_t time)
{
    if (time - re > 500) {
        usleep((time - re) * 1000);
        return time;
    }
    
    return re;
}

int open_flv_file(char* in_flv_file)
{
    return open(in_flv_file, O_RDONLY);
}

void close_flv_file(int fd)
{
    if (fd > 0) {
        close(fd);
    }
}

int flv_open_ic(int flv_fd)
{
    int ret = 0;
    
    char h[13]; // 9+4
    
    if (read(flv_fd, h, sizeof(h)) != sizeof(h)) {
        ret = -1;
        trace("read flv header failed. ret=%d", ret);
        return ret;
    }
    
    if (h[0] != 'F' || h[1] != 'L' || h[2] != 'V') {
        ret = -1;
        trace("input is not a flv file. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int flv_read_packet(int flv_fd, int* type, u_int32_t* timestamp, char** data, int* size)
{
    int ret = 0;
    
    char th[11]; // tag header
    char ts[4]; // tag size
    
    u_int32_t data_size = 0;
    u_int32_t time = 0;
    
    char* pp;
    
    // read tag header
    if (read(flv_fd, th, sizeof(th)) != sizeof(th)) {
        ret = -1;
        trace("read flv tag header failed. ret=%d", ret);
        return ret;
    }
    
    // Reserved UB [2]
    // Filter UB [1]
    // TagType UB [5]
    *type = (int)(th[0] & 0x1F);
    
    // DataSize UI24
    pp = (char*)&data_size;
    pp[2] = th[1];
    pp[1] = th[2];
    pp[0] = th[3];
    
    // Timestamp UI24
    pp = (char*)&time;
    pp[2] = th[4];
    pp[1] = th[5];
    pp[0] = th[6];
    
    // TimestampExtended UI8
    pp[3] = th[7];
    
    *timestamp = time;
    
    if (data_size > 0) {
        *size = data_size;
        *data = (char*)malloc(data_size);
        
        // read tag data
        if (read(flv_fd, *data, data_size) != data_size) {
            ret = -1;
            trace("read flv tag data failed. size=%d, ret=%d", data_size, ret);
            return ret;
        }
    }
    
    // ignore 4bytes tag size.
    if (read(flv_fd, ts, sizeof(ts)) != sizeof(ts)) {
        ret = -1;
        trace("read flv tag size failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}
