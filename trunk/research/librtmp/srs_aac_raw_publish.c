/*
The MIT License (MIT)

Copyright (c) 2013-2015 winlin

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
gcc srs_aac_raw_publish.c ../../objs/lib/srs_librtmp.a -g -O0 -lstdc++ -o srs_aac_raw_publish
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// for open audio raw file.
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
       
#include "../../objs/include/srs_librtmp.h"

// https://github.com/simple-rtmp-server/srs/issues/212#issuecomment-64145910
int read_audio_frame(char* data, int size, char** pp, char** frame, int* frame_size) 
{
    char* p = *pp;
    
    // @remark, for this demo, to publish aac raw file to SRS,
    // we search the adts frame from the buffer which cached the aac data.
    // please get aac adts raw data from device, it always a encoded frame.
    if (!srs_aac_is_adts(p, size - (p - data))) {
        srs_human_trace("aac adts raw data invalid.");
        return -1;
    }
    
    // @see srs_audio_write_raw_frame
    // each frame prefixed aac adts header, '1111 1111 1111'B, that is 0xFFF., 
    // for instance, frame = FF F1 5C 80 13 A0 FC 00 D0 33 83 E8 5B
    *frame = p;
    // skip some data. 
    // @remark, user donot need to do this.
    p += srs_aac_adts_frame_size(p, size - (p - data));
    
    *pp = p;
    *frame_size = p - *frame;
    if (*frame_size <= 0) {
        srs_human_trace("aac adts raw data invalid.");
        return -1;
    }
    
    return 0;
}

int main(int argc, char** argv)
{
    printf("publish raw audio as rtmp stream to server like FMLE/FFMPEG/Encoder\n");
    printf("SRS(simple-rtmp-server) client librtmp library.\n");
    printf("version: %d.%d.%d\n", srs_version_major(), srs_version_minor(), srs_version_revision());
    
    if (argc <= 2) {
        printf("Usage: %s <audio_raw_file> <rtmp_publish_url>\n", argv[0]);
        printf("     audio_raw_file: the audio raw steam file.\n");
        printf("     rtmp_publish_url: the rtmp publish url.\n");
        printf("For example:\n");
        printf("     %s ./audio.raw.aac rtmp://127.0.0.1:1935/live/livestream\n", argv[0]);
        printf("Where the file: http://winlinvip.github.io/srs.release/3rdparty/audio.raw.aac\n");
        printf("See: https://github.com/simple-rtmp-server/srs/issues/212\n");
        exit(-1);
    }
    
    const char* raw_file = argv[1];
    const char* rtmp_url = argv[2];
    srs_human_trace("raw_file=%s, rtmp_url=%s", raw_file, rtmp_url);
    
    // open file
    int raw_fd = open(raw_file, O_RDONLY);
    if (raw_fd < 0) {
        srs_human_trace("open audio raw file %s failed.", raw_file);
        goto rtmp_destroy;
    }
    
    off_t file_size = lseek(raw_fd, 0, SEEK_END);
    if (file_size <= 0) {
        srs_human_trace("audio raw file %s empty.", raw_file);
        goto rtmp_destroy;
    }
    srs_human_trace("read entirely audio raw file, size=%dKB", (int)(file_size / 1024));
    
    char* audio_raw = (char*)malloc(file_size);
    if (!audio_raw) {
        srs_human_trace("alloc raw buffer failed for file %s.", raw_file);
        goto rtmp_destroy;
    }
    
    lseek(raw_fd, 0, SEEK_SET);
    ssize_t nb_read = 0;
    if ((nb_read = read(raw_fd, audio_raw, file_size)) != file_size) {
        srs_human_trace("buffer %s failed, expect=%dKB, actual=%dKB.", 
            raw_file, (int)(file_size / 1024), (int)(nb_read / 1024));
        goto rtmp_destroy;
    }
    
    // connect rtmp context
    srs_rtmp_t rtmp = srs_rtmp_create(rtmp_url);
    
    if (srs_rtmp_handshake(rtmp) != 0) {
        srs_human_trace("simple handshake failed.");
        goto rtmp_destroy;
    }
    srs_human_trace("simple handshake success");
    
    if (srs_rtmp_connect_app(rtmp) != 0) {
        srs_human_trace("connect vhost/app failed.");
        goto rtmp_destroy;
    }
    srs_human_trace("connect vhost/app success");
    
    if (srs_rtmp_publish_stream(rtmp) != 0) {
        srs_human_trace("publish stream failed.");
        goto rtmp_destroy;
    }
    srs_human_trace("publish stream success");
    
    u_int32_t timestamp = 0;
    u_int32_t time_delta = 45;
    // @remark, to decode the file.
    char* p = audio_raw;
    for (;p < audio_raw + file_size;) {
        // @remark, read a frame from file buffer.
        char* data = NULL;
        int size = 0;
        if (read_audio_frame(audio_raw, file_size, &p, &data, &size) < 0) {
            srs_human_trace("read a frame from file buffer failed.");
            goto rtmp_destroy;
        }
        
        // 0 = Linear PCM, platform endian
        // 1 = ADPCM
        // 2 = MP3
        // 7 = G.711 A-law logarithmic PCM
        // 8 = G.711 mu-law logarithmic PCM
        // 10 = AAC
        // 11 = Speex
        char sound_format = 10;
        // 2 = 22 kHz
        char sound_rate = 2;
        // 1 = 16-bit samples
        char sound_size = 1;
        // 1 = Stereo sound
        char sound_type = 1;
        
        timestamp += time_delta;
        
        int ret = 0;
        if ((ret = srs_audio_write_raw_frame(rtmp, 
            sound_format, sound_rate, sound_size, sound_type,
            data, size, timestamp)) != 0
        ) {
            srs_human_trace("send audio raw data failed. ret=%d", ret);
            goto rtmp_destroy;
        }
        
        srs_human_trace("sent packet: type=%s, time=%d, size=%d, codec=%d, rate=%d, sample=%d, channel=%d", 
            srs_human_flv_tag_type2string(SRS_RTMP_TYPE_AUDIO), timestamp, size, sound_format, sound_rate, sound_size,
            sound_type);
        
        // @remark, when use encode device, it not need to sleep.
        usleep(1000 * time_delta);
    }
    
rtmp_destroy:
    srs_rtmp_destroy(rtmp);
    close(raw_fd);
    free(audio_raw);
    
    return 0;
}

