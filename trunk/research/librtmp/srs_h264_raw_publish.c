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
gcc srs_h264_raw_publish.c ../../objs/lib/srs_librtmp.a -g -O0 -lstdc++ -o srs_h264_raw_publish
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// for open h264 raw file.
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
       
#include "../../objs/include/srs_librtmp.h"

#define srs_trace(msg, ...) printf(msg, ##__VA_ARGS__);printf("\n")

int read_h264_frame(char* data, int size, char** pp, int fps,
    char** frame, int* frame_size, int* dts, int* pts)
{
    char* p = *pp;
    
    // @remark, for this demo, to publish h264 raw file to SRS,
    // we search the h264 frame from the buffer which cached the h264 data.
    // please get h264 raw data from device, it always a encoded frame.
    int pnb_start_code = 0;
    if (!srs_h264_startswith_annexb(p, size - (p - data), &pnb_start_code)) {
        srs_trace("h264 raw data invalid.");
        return -1;
    }
    
    // @see srs_write_h264_raw_frames
    // each frame prefixed h.264 annexb header, by N[00] 00 00 01, where N>=0, 
    // for instance, frame = header(00 00 00 01) + payload(67 42 80 29 95 A0 14 01 6E 40)
    *frame = p;
    p += pnb_start_code;
    
    for (;p < data + size; p++) {
        if (srs_h264_startswith_annexb(p, size - (p - data), &pnb_start_code)) {
            break;
        }
    }
    
    *pp = p;
    *frame_size = p - *frame;
    if (*frame_size <= 0) {
        srs_trace("h264 raw data invalid.");
        return -1;
    }
    
    // @remark, please get the dts and pts from device,
    // we assume there is no B frame, and the fps can guess the fps and dts,
    // while the dts and pts must read from encode lib or device.
    *dts += 1000 / fps;
    *pts = *dts;

    return 0;
}

int main(int argc, char** argv)
{
    srs_trace("publish raw h.264 as rtmp stream to server like FMLE/FFMPEG/Encoder");
    srs_trace("srs(simple-rtmp-server) client librtmp library.");
    srs_trace("version: %d.%d.%d", srs_version_major(), srs_version_minor(), srs_version_revision());
    
    if (argc <= 2) {
        srs_trace("Usage: %s <h264_raw_file> <rtmp_publish_url>", argv[0]);
        srs_trace("     h264_raw_file: the h264 raw steam file.");
        srs_trace("     rtmp_publish_url: the rtmp publish url.");
        srs_trace("For example:");
        srs_trace("     %s ./720p.h264.raw rtmp://127.0.0.1:1935/live/livestream", argv[0]);
        srs_trace("Where the file: http://winlinvip.github.io/srs.release/3rdparty/720p.h264.raw");
        srs_trace("See: https://github.com/winlinvip/simple-rtmp-server/issues/66");
        exit(-1);
    }
    
    const char* raw_file = argv[1];
    const char* rtmp_url = argv[2];
    srs_trace("raw_file=%s, rtmp_url=%s", raw_file, rtmp_url);
    
    // open file
    int raw_fd = open(raw_file, O_RDONLY);
    if (raw_fd < 0) {
        srs_trace("open h264 raw file %s failed.", raw_fd);
        goto rtmp_destroy;
    }
    
    off_t file_size = lseek(raw_fd, 0, SEEK_END);
    if (file_size <= 0) {
        srs_trace("h264 raw file %s empty.", raw_file);
        goto rtmp_destroy;
    }
    srs_trace("read entirely h264 raw file, size=%dKB", (int)(file_size / 1024));
    
    char* h264_raw = (char*)malloc(file_size);
    if (!h264_raw) {
        srs_trace("alloc raw buffer failed for file %s.", raw_file);
        goto rtmp_destroy;
    }
    
    lseek(raw_fd, 0, SEEK_SET);
    ssize_t nb_read = 0;
    if ((nb_read = read(raw_fd, h264_raw, file_size)) != file_size) {
        srs_trace("buffer %s failed, expect=%dKB, actual=%dKB.", 
            raw_file, (int)(file_size / 1024), (int)(nb_read / 1024));
        goto rtmp_destroy;
    }
    
    // connect rtmp context
    srs_rtmp_t rtmp = srs_rtmp_create(rtmp_url);
    
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
    
    if (srs_publish_stream(rtmp) != 0) {
        srs_trace("publish stream failed.");
        goto rtmp_destroy;
    }
    srs_trace("publish stream success");
    
    u_int32_t dts = 0;
    u_int32_t pts = 0;
    // @remark, the dts and pts if read from device, for instance, the encode lib,
    // so we assume the fps is 25, and each h264 frame is 1000ms/25fps=40ms/f.
    u_int32_t fps = 25;
    // @remark, to decode the file.
    char* p = h264_raw;
    for (;p < h264_raw + file_size;) {
        // @remark, read a frame from file buffer.
        char* data = NULL;
        int size = 0;
        if (read_h264_frame(h264_raw, file_size, &p, fps, &data, &size, &dts, &pts) < 0) {
            srs_trace("read a frame from file buffer failed.");
            goto rtmp_destroy;
        }
        
        // send out the h264 packet over RTMP
        if (srs_write_h264_raw_frames(rtmp, data, size, dts, pts) != 0) {
            srs_trace("send h264 raw data failed.");
            goto rtmp_destroy;
        }
        
        // 5bits, 7.3.1 NAL unit syntax, 
        // H.264-AVC-ISO_IEC_14496-10.pdf, page 44.
        u_int8_t nut = (char)data[0] & 0x1f;
        srs_trace("sent packet: type=%s, time=%d, size=%d, fps=%d, b[0]=%#x(%s)", 
            srs_type2string(SRS_RTMP_TYPE_VIDEO), dts, size, fps, (char)data[0],
            (nut == 7? "SPS":(nut == 8? "PPS":(nut == 5? "I":(nut == 1? "P":"Unknown")))));
        
        // @remark, when use encode device, it not need to sleep.
        usleep(1000 / fps * 1000);
    }
    srs_trace("h264 raw data completed");
    
rtmp_destroy:
    srs_rtmp_destroy(rtmp);
    close(raw_fd);
    free(h264_raw);
    
    return 0;
}

