//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_RTC_CODEC_HPP
#define SRS_APP_RTC_CODEC_HPP

#include <srs_core.hpp>

#include <srs_kernel_codec.hpp>
#include <srs_kernel_packet.hpp>

#include <string>

#ifdef __cplusplus
extern "C" {
#endif

#include <libavcodec/avcodec.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>

#ifdef __cplusplus
}
#endif

class SrsFFmpegLogHelper
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    static bool disabled_;

public:
    SrsFFmpegLogHelper();
    virtual ~SrsFFmpegLogHelper();

public:
    static void disable_ffmpeg_log();
    static void ffmpeg_log_callback(void *, int level, const char *fmt, va_list vl);
};

class ISrsAudioTranscoder
{
public:
    ISrsAudioTranscoder();
    virtual ~ISrsAudioTranscoder();

public:
    // The channels, sample_rate and bit_rate are for the encoder output.
    virtual srs_error_t initialize(SrsAudioCodecId from, SrsAudioCodecId to, int channels, int sample_rate, int bit_rate) = 0;
    virtual srs_error_t transcode(SrsParsedAudioPacket *in, std::vector<SrsParsedAudioPacket *> &outs) = 0;
    virtual void free_frames(std::vector<SrsParsedAudioPacket *> &frames) = 0;

public:
    // The AAC sequence header for FLV. The transcoder owns the data, so never free it.
    virtual void aac_codec_header(uint8_t **data, int *len) = 0;
};

class SrsAudioTranscoder : public ISrsAudioTranscoder
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    AVCodecContext *dec_;
    AVFrame *dec_frame_;
    AVPacket *dec_packet_;

    AVCodecContext *enc_;
    AVFrame *enc_frame_;
    AVPacket *enc_packet_;

    SwrContext *swr_;
    uint64_t swr_in_layout_;
    int swr_in_format_;
    int swr_in_rate_;
    uint8_t **swr_data_;
    AVAudioFifo *fifo_;

    int64_t new_pkt_pts_;
    int64_t next_out_pts_;

public:
    SrsAudioTranscoder();
    virtual ~SrsAudioTranscoder();

public:
    srs_error_t initialize(SrsAudioCodecId from, SrsAudioCodecId to, int channels, int sample_rate, int bit_rate);
    virtual srs_error_t transcode(SrsParsedAudioPacket *in, std::vector<SrsParsedAudioPacket *> &outs);
    void free_frames(std::vector<SrsParsedAudioPacket *> &frames);

public:
    void aac_codec_header(uint8_t **data, int *len);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t init_dec(SrsAudioCodecId from);
    srs_error_t init_enc(SrsAudioCodecId to, int channels, int samplerate, int bit_rate);
    srs_error_t init_swr(AVFrame *frame);
    srs_error_t init_fifo();

    srs_error_t decode_and_resample(SrsParsedAudioPacket *pkt);
    srs_error_t encode(std::vector<SrsParsedAudioPacket *> &pkts);

    srs_error_t add_samples_to_fifo(uint8_t **samples, int frame_size);
    void free_swr_samples();
};

#endif
