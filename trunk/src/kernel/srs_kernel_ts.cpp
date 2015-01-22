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

#include <srs_kernel_ts.hpp>

// for srs-librtmp, @see https://github.com/winlinvip/simple-rtmp-server/issues/213
#ifndef _WIN32
#include <unistd.h>
#endif

#include <fcntl.h>
#include <sstream>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_avc.hpp>

SrsTsEncoder::SrsTsEncoder()
{
    _fs = NULL;
    codec = new SrsAvcAacCodec();
    sample = new SrsCodecSample();
}

SrsTsEncoder::~SrsTsEncoder()
{
    srs_freep(codec);
    srs_freep(sample);
}

int SrsTsEncoder::initialize(SrsFileWriter* fs)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(fs);
    
    if (!fs->is_open()) {
        ret = ERROR_KERNEL_FLV_STREAM_CLOSED;
        srs_warn("stream is not open for encoder. ret=%d", ret);
        return ret;
    }
    
    _fs = fs;
    
    return ret;
}

int SrsTsEncoder::write_audio(int64_t timestamp, char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    sample->clear();
    if ((ret = codec->audio_aac_demux(data, size, sample)) != ERROR_SUCCESS) {
        srs_error("http: ts codec demux audio failed. ret=%d", ret);
        return ret;
    }
    
    if (codec->audio_codec_id != SrsCodecAudioAAC) {
        return ret;
    }
    
    // ignore sequence header
    if (sample->aac_packet_type == SrsCodecAudioTypeSequenceHeader) {
        return ret;
    }

    // the dts calc from rtmp/flv header.
    // @remark for http ts stream, the timestamp is always monotonically increase,
    //      for the packet is filtered by consumer.
    int64_t dts = timestamp * 90;
    
    /*if ((ret = hls_cache->write_audio(codec, muxer, dts, sample)) != ERROR_SUCCESS) {
        srs_error("http: ts cache write audio failed. ret=%d", ret);
        return ret;
    }*/

    return ret;
}

int SrsTsEncoder::write_video(int64_t timestamp, char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    sample->clear();
    if ((ret = codec->video_avc_demux(data, size, sample)) != ERROR_SUCCESS) {
        srs_error("http: ts codec demux video failed. ret=%d", ret);
        return ret;
    }
    
    // ignore info frame,
    // @see https://github.com/winlinvip/simple-rtmp-server/issues/288#issuecomment-69863909
    if (sample->frame_type == SrsCodecVideoAVCFrameVideoInfoFrame) {
        return ret;
    }
    
    if (codec->video_codec_id != SrsCodecVideoAVC) {
        return ret;
    }
    
    // ignore sequence header
    if (sample->frame_type == SrsCodecVideoAVCFrameKeyFrame
         && sample->avc_packet_type == SrsCodecVideoAVCTypeSequenceHeader) {
        return ret;
    }
    
    int64_t dts = timestamp * 90;
    /*if ((ret = hls_cache->write_video(codec, muxer, dts, sample)) != ERROR_SUCCESS) {
        srs_error("http: ts cache write video failed. ret=%d", ret);
        return ret;
    }*/

    return ret;
}


