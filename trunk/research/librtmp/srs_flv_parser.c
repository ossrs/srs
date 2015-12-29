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
gcc srs_ingest_flv.c ../../objs/lib/srs_librtmp.a -g -O0 -lstdc++ -o srs_ingest_flv
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "../../objs/include/srs_librtmp.h"

int parse_flv(srs_flv_t flv);
int main(int argc, char** argv)
{
    int ret = 0;
    
    // user options.
    char* in_flv_file;
    // flv handler
    srs_flv_t flv;
    
    printf("parse and show flv file detail.\n");
    printf("srs(ossrs) client librtmp library.\n");
    printf("version: %d.%d.%d\n", srs_version_major(), srs_version_minor(), srs_version_revision());
    
    if (argc <= 1) {
        printf("parse and show flv file detail\n"
            "Usage: %s in_flv_file\n"
            "   in_flv_file         flv file to parse and show.\n"
            "For example:\n"
            "   %s doc/source.200kbps.768x320.flv\n"
            "   %s ../../doc/source.200kbps.768x320.flv\n",
            argv[0], argv[0], argv[0]);
        exit(-1);
    }
    
    in_flv_file = argv[1];
    srs_human_trace("input:  %s", in_flv_file);

    if ((flv = srs_flv_open_read(in_flv_file)) == NULL) {
        ret = 2;
        srs_human_trace("open flv file failed. ret=%d", ret);
        return ret;
    }
    
    ret = parse_flv(flv);
    srs_flv_close(flv);
    
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

    if (size > 0) {
        digit_to_char(data, size, hbuf, hsize - 1);
    }
    
    if (size > print_size * 2) {
        digit_to_char(data + size - print_size, size, tbuf, tsize - 1);
    }

    return 0;
}

int parse_flv(srs_flv_t flv)
{
    int ret = 0;
    
    // flv header
    char header[13];
    // packet data
    char type;
    u_int32_t timestamp = 0;
    char* data = NULL;
    int32_t size;
    int64_t offset = 0;
    
    if ((ret = srs_flv_read_header(flv, header)) != 0) {
        return ret;
    }
    
    srs_human_trace("start parse flv");
    for (;;) {
        offset = srs_flv_tellg(flv);
        
        // tag header
        if ((ret = srs_flv_read_tag_header(flv, &type, &size, &timestamp)) != 0) {
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
        
        data = (char*)malloc(size);
        
        if ((ret = srs_flv_read_tag_data(flv, data, size)) == 0) {
            if ((ret = srs_human_print_rtmp_packet(type, timestamp, data, size)) == 0) {
                char hbuf[48]; char tbuf[48];
                parse_bytes(data, size, hbuf, sizeof(hbuf), tbuf, sizeof(tbuf), 16);
                srs_human_raw("offset=%d, first and last 16 bytes:\n"
                    "[+00, +15] %s\n[-15, EOF] %s\n", (int)offset, hbuf, tbuf);
            } else {
                srs_human_trace("print packet failed. ret=%d", ret);
            }
        } else {
            srs_human_trace("read flv failed. ret=%d", ret);
        }
        
        free(data);
        
        if (ret != 0) {
            srs_human_trace("parse failed, ret=%d", ret);
            return ret;
        }
    }
    
    return ret;
}
