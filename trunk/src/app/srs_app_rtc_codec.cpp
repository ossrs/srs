/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Bepartofyou
 * Copyright (c) 2021 PieerePi - new SrsAudioTranscoder
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

#include <srs_kernel_codec.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_rtc_codec.hpp>

static const char* id2codec_name(SrsAudioCodecId id)
{
    switch (id) {
    case SrsAudioCodecIdAAC:
        return "aac";
    case SrsAudioCodecIdOpus:
        return "libopus";
    case SrsAudioCodecIdReservedG711AlawLogarithmicPCM:
        return "pcm_alaw";
    case SrsAudioCodecIdReservedG711MuLawLogarithmicPCM:
        return "pcm_mulaw";
    default:
        return "";
    }
}

static AVFrame *alloc_audio_frame(enum AVSampleFormat sample_fmt, uint64_t channel_layout, int sample_rate,
                                  int nb_samples)
{
    AVFrame *frame = av_frame_alloc();
    int ret;

    if (!frame) { return NULL; }

    frame->format = sample_fmt;
    frame->channel_layout = channel_layout;
    frame->sample_rate = sample_rate;
    frame->nb_samples = nb_samples;

    if (nb_samples) {
        ret = av_frame_get_buffer(frame, 0);
        if (ret < 0) { return NULL; }
    }

    av_frame_make_writable(frame);

    return frame;
}

SrsAudioDecoder::SrsAudioDecoder(SrsAudioCodecId codec, int samplerate, int channels)
    : codec_id_(codec),
    sampling_rate_(samplerate),
    channels_(channels)
{
    frame_ = NULL;
    packet_ = NULL;
    codec_ctx_ = NULL;
}

SrsAudioDecoder::~SrsAudioDecoder()
{
    if (codec_ctx_) {
        avcodec_free_context(&codec_ctx_);
        codec_ctx_ = NULL;
    }
    if (frame_) {
        av_frame_free(&frame_);
        frame_ = NULL;
    }
    if (packet_) {
        av_packet_free(&packet_);
        packet_ = NULL;
    }
}

srs_error_t SrsAudioDecoder::initialize()
{
    srs_error_t err = srs_success;
    int ret;

    //check codec name,only support "pcm_alaw","pcm_mulaw","aac","opus"
    if (codec_id_ != SrsAudioCodecIdAAC && codec_id_ != SrsAudioCodecIdOpus
        && codec_id_ != SrsAudioCodecIdReservedG711AlawLogarithmicPCM
        && codec_id_ != SrsAudioCodecIdReservedG711MuLawLogarithmicPCM) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "Invalid codec name %d", codec_id_);
    }

    const char* codec_name = id2codec_name(codec_id_);
    const AVCodec *codec = avcodec_find_decoder_by_name(codec_name);
    if (!codec) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "Codec not found by name %d(%s)", codec_id_, codec_name);
    }

    codec_ctx_ = avcodec_alloc_context3(codec);
    if (!codec_ctx_) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "Could not allocate audio codec context");
    }
    codec_ctx_->sample_rate = sampling_rate_;
    codec_ctx_->channels = channels_;
    codec_ctx_->channel_layout = av_get_default_channel_layout(channels_);

    if ((ret = avcodec_open2(codec_ctx_, codec, NULL)) < 0) {
        char error_buf[1024];
        av_strerror(ret, error_buf, sizeof(error_buf) - 1);
        return srs_error_new(ERROR_RTC_RTP_MUXER, "Could not open codec: %s", error_buf);
    }

    frame_ = av_frame_alloc();
    if (!frame_) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "Could not allocate audio frame");
    }

    packet_ = av_packet_alloc();
    if (!packet_) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "Could not allocate audio packet");
    }

    return err;
}

srs_error_t SrsAudioDecoder::decode(SrsSample *pkt, AVFrame **decoded_frame, int feed)
{
    srs_error_t err = srs_success;
    int ret;

    *decoded_frame = NULL;

    if (feed) {
        packet_->data = (uint8_t *)pkt->bytes;
        packet_->size = pkt->size;
        ret = avcodec_send_packet(codec_ctx_, packet_);
        if (ret < 0) {
            char error_buf[1024];
            av_strerror(ret, error_buf, sizeof(error_buf) - 1);
            return srs_error_new(ERROR_RTC_RTP_MUXER, "Error submitting the packet to the decoder: %s", error_buf);
        }
    }

    ret = avcodec_receive_frame(codec_ctx_, frame_);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        return err;
    } else if (ret < 0) {
        char error_buf[1024];
        av_strerror(ret, error_buf, sizeof(error_buf) - 1);
        return srs_error_new(ERROR_RTC_RTP_MUXER, "Error during decoding: %s", error_buf);
    }

    *decoded_frame = frame_;

    return err;
}

AVCodecContext* SrsAudioDecoder::codec_ctx()
{
    return codec_ctx_;
}

SrsAudioEncoder::SrsAudioEncoder(SrsAudioCodecId codec, int samplerate, int channels)
    : codec_id_(codec),
    sampling_rate_(samplerate),
    channels_(channels)
{
    frame_ = NULL;
    left_samples = 0;
    packet_ = NULL;
    codec_ctx_ = NULL;
}

SrsAudioEncoder::~SrsAudioEncoder()
{
    if (codec_ctx_) {
        avcodec_free_context(&codec_ctx_);
        codec_ctx_= NULL;
    }
    if (frame_) {
        av_frame_free(&frame_);
        frame_ = NULL;
    }
    if (packet_) {
        av_packet_free(&packet_);
        packet_ = NULL;
    }
}

srs_error_t SrsAudioEncoder::initialize()
{
    srs_error_t err = srs_success;
    int ret;

    if (codec_id_ != SrsAudioCodecIdAAC && codec_id_ != SrsAudioCodecIdOpus) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "Invalid codec name %d", codec_id_);
    }

    const char* codec_name = id2codec_name(codec_id_);
    const AVCodec *codec = avcodec_find_encoder_by_name(codec_name);
    if (!codec) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "Codec not found by name %d(%s)", codec_id_, codec_name);
    }

    codec_ctx_ = avcodec_alloc_context3(codec);
    if (!codec_ctx_) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "Could not allocate audio codec context");
    }

    codec_ctx_->sample_rate = sampling_rate_;
    codec_ctx_->channels = channels_;
    codec_ctx_->channel_layout = av_get_default_channel_layout(channels_);
    codec_ctx_->bit_rate = 48000;
    if (codec_id_ == SrsAudioCodecIdOpus) {
        codec_ctx_->sample_fmt = AV_SAMPLE_FMT_S16;
        //TODO: for more level setting
        codec_ctx_->compression_level = 1;
    } else if (codec_id_ == SrsAudioCodecIdAAC) {
        codec_ctx_->sample_fmt = AV_SAMPLE_FMT_FLTP;
        codec_ctx_->profile = FF_PROFILE_AAC_LOW;
    }

    if ((ret = avcodec_open2(codec_ctx_, codec, NULL)) < 0) {
        char error_buf[1024];
        av_strerror(ret, error_buf, sizeof(error_buf) - 1);
        return srs_error_new(ERROR_RTC_RTP_MUXER, "Could not open codec: %s", error_buf);
    }

    if (codec_ctx_->frame_size != 0) {
        frame_ = alloc_audio_frame(codec_ctx_->sample_fmt, codec_ctx_->channel_layout, codec_ctx_->sample_rate, codec_ctx_->frame_size);
        if (!frame_) {
            return srs_error_new(ERROR_RTC_RTP_MUXER, "Could not get audio frame buffer");
        }
    }

    packet_ = av_packet_alloc();
    if (!packet_) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "Could not allocate audio packet");
    }

    return err;
}

srs_error_t SrsAudioEncoder::encode(AVFrame *resampled_frame, int &used_samples, AVPacket **encoded_packet)
{
    srs_error_t err = srs_success;
    int ret = 0;

    *encoded_packet = NULL;

    if ((resampled_frame->nb_samples - used_samples) > 0) {
        if (codec_ctx_->frame_size == 0) {
            ret = avcodec_send_frame(codec_ctx_, resampled_frame);
            used_samples = resampled_frame->nb_samples;
        } else {
            int filled_samples = codec_ctx_->frame_size - left_samples < resampled_frame->nb_samples - used_samples ?
                                 codec_ctx_->frame_size - left_samples : resampled_frame->nb_samples - used_samples;
            av_samples_copy(frame_->data, resampled_frame->data, left_samples, used_samples, filled_samples,
                            codec_ctx_->channels, codec_ctx_->sample_fmt);
            left_samples += filled_samples;
            srs_assert(left_samples <= codec_ctx_->frame_size);
            if (left_samples == codec_ctx_->frame_size) {
                ret = avcodec_send_frame(codec_ctx_, frame_);
            }
            left_samples = left_samples % codec_ctx_->frame_size;
            used_samples += filled_samples;
        }
        if (ret < 0) {
            char error_buf[1024];
            av_strerror(ret, error_buf, sizeof(error_buf) - 1);
            return srs_error_new(ERROR_RTC_RTP_MUXER, "Error sending the frame to the encoder, %s", error_buf);
        }
    }

    ret = avcodec_receive_packet(codec_ctx_, packet_);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        return err;
    } else if (ret < 0) {
        char error_buf[1024];
        av_strerror(ret, error_buf, sizeof(error_buf) - 1);
        return srs_error_new(ERROR_RTC_RTP_MUXER, "Error during encoding: %s", error_buf);
    }

    // av_packet_unref(*encoded_packet) outside
    *encoded_packet = packet_;

    return err;
}

AVCodecContext* SrsAudioEncoder::codec_ctx()
{
    return codec_ctx_;
}

SrsAudioResampler::SrsAudioResampler(int src_rate, int src_layout, enum AVSampleFormat src_fmt,
    int dst_rate, int dst_layout, AVSampleFormat dst_fmt)
    : src_rate_(src_rate),
    src_ch_layout_(src_layout),
    src_sample_fmt_(src_fmt),
    dst_rate_(dst_rate),
    dst_ch_layout_(dst_layout),
    dst_sample_fmt_(dst_fmt)
{
    max_dst_nb_samples_ = 0;
    frame_ = NULL;
    swr_ctx_ = NULL;
}

SrsAudioResampler::~SrsAudioResampler()
{
    if (swr_ctx_) {
        swr_free(&swr_ctx_);
        swr_ctx_ = NULL;
    }
    if (frame_) {
        av_frame_free(&frame_);
        frame_ = NULL;
    }
}

srs_error_t SrsAudioResampler::initialize()
{
    srs_error_t err = srs_success;

    swr_ctx_ = swr_alloc();
    if (!swr_ctx_) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "Could not allocate resampler context");
    }

    av_opt_set_int(swr_ctx_, "in_channel_layout",    src_ch_layout_, 0);
    av_opt_set_int(swr_ctx_, "in_sample_rate",       src_rate_, 0);
    av_opt_set_sample_fmt(swr_ctx_, "in_sample_fmt", src_sample_fmt_, 0);

    av_opt_set_int(swr_ctx_, "out_channel_layout",    dst_ch_layout_, 0);
    av_opt_set_int(swr_ctx_, "out_sample_rate",       dst_rate_, 0);
    av_opt_set_sample_fmt(swr_ctx_, "out_sample_fmt", dst_sample_fmt_, 0);

    int ret;
    if ((ret = swr_init(swr_ctx_)) < 0) {
        char error_buf[1024];
        av_strerror(ret, error_buf, sizeof(error_buf) - 1);
        return srs_error_new(ERROR_RTC_RTP_MUXER, "Failed to initialize the resampling context: %s", error_buf);
    }

    return err;
}

srs_error_t SrsAudioResampler::resample(AVFrame *decoded_frame, AVFrame **resampled_frame)
{
    srs_error_t err = srs_success;

    *resampled_frame = NULL;

    int nb_samples = av_rescale_rnd(swr_get_delay(swr_ctx_, src_rate_) +
                                    decoded_frame->nb_samples, dst_rate_, src_rate_, AV_ROUND_UP);
    if (nb_samples > max_dst_nb_samples_ || !frame_) {
        if (frame_) {
            av_frame_free(&frame_);
            frame_ = NULL;
        }
        frame_ = alloc_audio_frame(dst_sample_fmt_, dst_ch_layout_, dst_rate_, nb_samples);
        if (!frame_) {
            return srs_error_new(ERROR_RTC_RTP_MUXER, "alloc error");
        }
        max_dst_nb_samples_ = nb_samples;
    }

    nb_samples = swr_convert(swr_ctx_, frame_->data, max_dst_nb_samples_, (const uint8_t **)decoded_frame->data, decoded_frame->nb_samples);
    if (nb_samples < 0) {
        char error_buf[1024];
        av_strerror(nb_samples, error_buf, sizeof(error_buf) - 1);
        return srs_error_new(ERROR_RTC_RTP_MUXER, "Error while converting: %s", error_buf);
    }

    frame_->nb_samples = nb_samples;
    *resampled_frame = frame_;

    return err;
}

SrsAudioTranscoder::SrsAudioTranscoder(SrsAudioCodecId src_codec, int src_channels, int src_samplerate, SrsAudioCodecId dst_codec, int dst_channels, int dst_samplerate)
    : src_codec_(src_codec),
    src_channels_(src_channels),
    src_samplerate_(src_samplerate),
    dst_codec_(dst_codec),
    dst_channels_(dst_channels),
    dst_samplerate_(dst_samplerate)
{
    dec_ = NULL;
    enc_ = NULL;
    resample_ = NULL;
}

SrsAudioTranscoder::~SrsAudioTranscoder()
{
    srs_freep(dec_);
    srs_freep(enc_);
    srs_freep(resample_);
}

srs_error_t SrsAudioTranscoder::initialize()
{
    srs_error_t err = srs_success;

    dec_ = new SrsAudioDecoder(src_codec_, src_samplerate_, src_channels_);
    if ((err = dec_->initialize()) != srs_success) {
        return srs_error_wrap(err, "dec init");
    }

    enc_ = new SrsAudioEncoder(dst_codec_, dst_samplerate_, dst_channels_);
    if ((err = enc_->initialize()) != srs_success) {
        return srs_error_wrap(err, "enc init");
    }

    return err;
}

srs_error_t SrsAudioTranscoder::transcode(SrsSample *pkt, char **buf_array, int *len_array, int buf_array_size, int buf_max_size, int &n)
{
    srs_error_t err = srs_success;

    n = 0;

    if (!dec_ || !enc_) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "dec_ or enc_ nullptr");
    }

    int decode_feed = 1;
    do {
        AVFrame *decoded_frame = NULL;
        if ((err = dec_->decode(pkt, &decoded_frame, decode_feed)) != srs_success) {
            return srs_error_wrap(err, "decode error");
        }
        if (!decoded_frame) {
            break;
        }
        decode_feed = 0;

        if (!resample_) {
            int channel_layout = av_get_default_channel_layout(dst_channels_);
            AVCodecContext *codec_ctx = dec_->codec_ctx();
            resample_ = new SrsAudioResampler(codec_ctx->sample_rate, (int)codec_ctx->channel_layout, codec_ctx->sample_fmt,
                                              dst_samplerate_, channel_layout, enc_->codec_ctx()->sample_fmt);
            if (!resample_ || (err = resample_->initialize()) != srs_success) {
                return srs_error_wrap(err, "init resample");
            }
        }

        AVFrame *resampled_frame = NULL;
        if ((err = resample_->resample(decoded_frame, &resampled_frame)) != srs_success) {
            return srs_error_wrap(err, "resample error");
        }

        int used_samples = 0;
        do {
            AVPacket *encoded_packet = NULL;
            if ((err = enc_->encode(resampled_frame, used_samples, &encoded_packet)) != srs_success) {
                return srs_error_wrap(err, "encode error");
            }
            if (!encoded_packet) {
                if (resampled_frame->nb_samples > used_samples) {
                    continue;
                }
                break;
            }

            if (n < buf_array_size && encoded_packet->size <= buf_max_size) {
                memcpy(buf_array[n], encoded_packet->data, encoded_packet->size);
                len_array[n] = encoded_packet->size;
                n++;
            } else {
                av_packet_unref(encoded_packet);
                return srs_error_new(ERROR_RTC_RTP_MUXER, "copy error, n %d, buf array size %d, pkt size %d, buf max size %d",
                                     n, buf_array_size, encoded_packet->size, buf_max_size);
            }

            av_packet_unref(encoded_packet);
        } while (1);
    } while (1);

    return err;
}
