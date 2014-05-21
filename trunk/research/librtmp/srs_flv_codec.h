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

#ifndef SRS_RESEARH_FLV_CODEC_HPP
#define SRS_RESEARH_FLV_CODEC_HPP

/*
#include "srs_flv_codec.h"
*/

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#define ERROR_FLV_CODEC_EOF 100

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
    
    int32_t data_size = 0;
    u_int32_t time = 0; 
    
    char* pp;
    
    // read tag header
    if ((ret = read(flv_fd, th, sizeof(th))) != sizeof(th)) {
        if (ret == 0) {
            return ERROR_FLV_CODEC_EOF;
        }
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
    
    // check data size.
    if (data_size <= 0) {
        ret = -1;
        trace("invalid data size. size=%d, ret=%d", data_size, ret);
        return ret;
    }
    
    // read tag data.
    *size = data_size;
    *data = (char*)malloc(data_size);
    if ((ret = read(flv_fd, *data, data_size)) != data_size) {
        if (ret == 0) {
            return ERROR_FLV_CODEC_EOF;
        }
        ret = -1;
        trace("read flv tag data failed. size=%d, ret=%d", data_size, ret);
        return ret;
    }
    
    // ignore 4bytes tag size.
    if ((ret = read(flv_fd, ts, sizeof(ts))) != sizeof(ts)) {
        if (ret == 0) {
            return ERROR_FLV_CODEC_EOF;
        }
        ret = -1;
        trace("read flv tag size failed. ret=%d", ret);
        return ret;
    }
    
    return 0;
}

#endif
