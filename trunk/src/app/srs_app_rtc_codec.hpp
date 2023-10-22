//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_APP_RTC_CODEC_HPP
#define SRS_APP_RTC_CODEC_HPP

#include <srs_core.hpp>

#include <srs_kernel_codec.hpp>

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
#include <libavutil/audio_fifo.h>

#ifdef __cplusplus
}
#endif

class SrsAudioTranscoder
{
private:
    AVCodecContext *dec_;
    AVFrame *dec_frame_;
    AVPacket *dec_packet_;

    AVCodecContext *enc_;
    AVFrame *enc_frame_;
    AVPacket *enc_packet_;

    SwrContext *swr_;
    //buffer for swr out put
    uint8_t **swr_data_;
    AVAudioFifo *fifo_;

    int64_t new_pkt_pts_;
    int64_t next_out_pts_;
public:
    SrsAudioTranscoder();
    virtual ~SrsAudioTranscoder();
public:
    // Initialize the transcoder, transcode from codec as to codec.
    // The channels specifies the number of output channels for encoder, for example, 2.
    // The sample_rate specifies the sample rate of encoder, for example, 48000.
    // The bit_rate specifies the bitrate of encoder, for example, 48000.
    srs_error_t initialize(SrsAudioCodecId from, SrsAudioCodecId to, int channels, int sample_rate, int bit_rate);
    // Transcode the input audio frame in, as output audio frames outs.
    virtual srs_error_t transcode(SrsAudioFrame* in, std::vector<SrsAudioFrame*>& outs);
    // Free the generated audio frames by transcode.
    void free_frames(std::vector<SrsAudioFrame*>& frames);
public:
    // Get the aac codec header, for example, FLV sequence header.
    // @remark User should never free the data, it's managed by this transcoder.
    void aac_codec_header(uint8_t** data, int* len);
private:
    srs_error_t init_dec(SrsAudioCodecId from);
    srs_error_t init_enc(SrsAudioCodecId to, int channels, int samplerate, int bit_rate);
    srs_error_t init_swr(AVCodecContext* decoder);
    srs_error_t init_fifo();

    srs_error_t decode_and_resample(SrsAudioFrame* pkt);
    srs_error_t encode(std::vector<SrsAudioFrame*> &pkts);

    srs_error_t add_samples_to_fifo(uint8_t** samples, int frame_size);
    void free_swr_samples();
};

#endif /* SRS_APP_AUDIO_RECODE_HPP */

