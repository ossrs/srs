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
#include "srs_flv_codec.h"

int parse_flv(int flv_fd);
int main(int argc, char** argv)
{
    int ret = 0;
    
    // user options.
    char* in_flv_file;
    // flv handler
    int flv_fd;
    
    if (argc <= 1) {
        printf("parse and show flv file detail\n"
            "Usage: %s in_flv_file\n"
            "   in_flv_file         flv file to parse and show.\n"
            "For example:\n"
            "   %s ../../doc/source.200kbps.768x320.flv\n",
            argv[0]);
        ret = 1;
        exit(ret);
        return ret;
    }
    
    in_flv_file = argv[1];
    
    trace("parse and show flv file detail.");
    trace("srs(simple-rtmp-server) client librtmp library.");
    trace("version: %d.%d.%d", srs_version_major(), srs_version_minor(), srs_version_revision());
    trace("input:  %s", in_flv_file);

    flv_fd = open_flv_file(in_flv_file);
    if (flv_fd <= 0) {
        ret = 2;
        trace("open flv file failed. ret=%d", ret);
        return ret;
    }
    
    ret = parse_flv(flv_fd);
    close_flv_file(flv_fd);
    
    return ret;
}

int parse_audio_data(char* data, int size)
{
    int ret = 0;
    return ret;
}

int parse_video_data(char* data, int size)
{
    int ret = 0;
    return ret;
}

void digit_to_char(char* src, int ssize, char* dst, int dsize)
{
    int i, j;
    char v;
    
    for (i = 0, j = 0; i < ssize && j < dsize; i++) {
        if (j >= dsize) {
            break;
        }
        v = (src[i] >> 4) & 0x0F;
        if (v < 10) {
            dst[j++] = '0' + v;
        } else {
            dst[j++] = 'A' + (v - 10);
        }
        
        if (j >= dsize) {
            break;
        }
        v = src[i] & 0x0F;
        if (v < 10) {
            dst[j++] = '0' + v;
        } else {
            dst[j++] = 'A' + (v - 10);
        }
        
        if (j >= dsize) {
            break;
        }
        if (i < ssize - 1) {
            dst[j++] = ' ';
        }
    }
}

int parse_bytes(char* data, int size, char* hbuf, int hsize, char* tbuf, int tsize, int print_size)
{
    memset(hbuf, 0, hsize);
    memset(tbuf, 0, tsize);
    if (size > print_size * 2) {
        digit_to_char(data, size, hbuf, hsize - 1);
        digit_to_char(data + size - print_size, size, tbuf, tsize - 1);
    }
}

int parse_script_data(char* data, int size)
{
    int ret = 0;
    
    char hbuf[48];
    char tbuf[48];
    parse_bytes(data, size, hbuf, sizeof(hbuf), tbuf, sizeof(tbuf), 16);
    
    srs_amf0_t amf0 = srs_amf0_parse(data, size);
    if (amf0 == NULL) {
        trace("invalid amf0 data.");
        return -1;
    }
    
    trace("details:\n"
        "[+00, +15] %s\n[-15, EOF] %s", 
        hbuf, tbuf);

    return ret;
}

int parse_flv(int flv_fd)
{
    int ret = 0;
    
    if ((ret = flv_open_ic(flv_fd)) != 0) {
        return ret;
    }
    
    // packet data
    int type, size;
    u_int32_t timestamp = 0;
    char* data = NULL;
    
    trace("start parse flv");
    for (;;) {
        if ((ret = flv_read_packet(flv_fd, &type, &timestamp, &data, &size)) != 0) {
            if (ret == ERROR_FLV_CODEC_EOF) {
                trace("parse completed.");
                return 0;
            }
            trace("irtmp get packet failed. ret=%d", ret);
            return ret;
        }
        trace("flv got packet: type=%s, time=%d, size=%d", srs_type2string(type), timestamp, size);
        
        // data tag
        if (type == SRS_RTMP_TYPE_AUDIO) {
            if ((ret = parse_audio_data(data, size)) != 0) {
                return ret;
            }
        } else if (type == SRS_RTMP_TYPE_VIDEO) {
            if ((ret = parse_video_data(data, size)) != 0) {
                return ret;
            }
        } else {
            if ((ret = parse_script_data(data, size)) != 0) {
                return ret;
            }
        }
        
        free(data);
    }
    
    return ret;
}
