/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Bepartofyou
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef SRS_APP_AUDIO_RECODE_HPP
#define SRS_APP_AUDIO_RECODE_HPP

#include <srs_core.hpp>

#include <string>

#ifdef __cplusplus
extern "C" {
#endif

#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>

#include <opus/opus.h>

#ifdef __cplusplus
}
#endif

class SrsSample;

class SrsAudioDecoder
{
private:
    AVFrame* frame_;
    AVPacket* packet_;
    AVCodecContext* codec_ctx_;
    std::string codec_name_;
public:
    SrsAudioDecoder(std::string codec);
    virtual ~SrsAudioDecoder();
    srs_error_t initialize();
    virtual srs_error_t decode(SrsSample *pkt, char *buf, int &size);
    AVCodecContext* codec_ctx();
};

class SrsAudioEncoder
{
private:
    int inband_fec_;
	int channels_;
	int sampling_rate_;
	int complexity_;
	OpusEncoder *opus_;
public:
    SrsAudioEncoder(int samplerate, int channels, int fec, int complexity);
    virtual ~SrsAudioEncoder();
    srs_error_t initialize();
    virtual srs_error_t encode(SrsSample *frame, char *buf, int &size);
};

class SrsAudioResample
{
private:
    int src_rate_;
    int src_ch_layout_;
    int src_nb_channels_;
    enum AVSampleFormat src_sample_fmt_;
    int src_linesize_;
    int src_nb_samples_;
    uint8_t **src_data_;

    int dst_rate_;
    int dst_ch_layout_;
    int dst_nb_channels_;
    enum AVSampleFormat dst_sample_fmt_;
    int dst_linesize_;
    int dst_nb_samples_;
    uint8_t **dst_data_;

    int max_dst_nb_samples_;
    struct SwrContext *swr_ctx_;
public:
    SrsAudioResample(int src_rate, int src_layout, enum AVSampleFormat src_fmt,
        int src_nb, int dst_rate, int dst_layout, enum AVSampleFormat dst_fmt);
    virtual ~SrsAudioResample();
    srs_error_t initialize();
    virtual srs_error_t resample(SrsSample *pcm, char *buf, int &size);
};

class SrsAudioRecode
{
private:
    SrsAudioDecoder *dec_;
    SrsAudioEncoder *enc_;
    SrsAudioResample *resample_;
    int dst_channels_;
    int dst_samplerate_;
    int size_;
    char *data_;
public:
    SrsAudioRecode(int channels, int samplerate);
    virtual ~SrsAudioRecode();
    srs_error_t initialize();
    virtual srs_error_t recode(SrsSample *pkt, char **buf, int *buf_len, int &n);
};

#endif /* SRS_APP_AUDIO_RECODE_HPP */
