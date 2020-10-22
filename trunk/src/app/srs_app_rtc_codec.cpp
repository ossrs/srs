
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

#include <srs_kernel_codec.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_rtc_codec.hpp>

static const int kFrameBufMax   = 40960;
static const int kPacketBufMax  = 8192;

static const char* id2codec_name(SrsAudioCodecId id) 
{
    switch (id) {
    case SrsAudioCodecIdAAC:
        return "aac";
    case SrsAudioCodecIdOpus:
        return "libopus";
    default:
        return "";
    }
}

SrsAudioDecoder::SrsAudioDecoder(SrsAudioCodecId codec)
    : codec_id_(codec)
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

    //check codec name,only support "aac","opus"
    if (codec_id_ != SrsAudioCodecIdAAC && codec_id_ != SrsAudioCodecIdOpus) {
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

    if (avcodec_open2(codec_ctx_, codec, NULL) < 0) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "Could not open codec");
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

srs_error_t SrsAudioDecoder::decode(SrsSample *pkt, char *buf, int &size)
{
    srs_error_t err = srs_success;

    packet_->data = (uint8_t *)pkt->bytes;
    packet_->size = pkt->size;

    int ret = avcodec_send_packet(codec_ctx_, packet_);
    if (ret < 0) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "Error submitting the packet to the decoder");
    }

    int max = size;
    size = 0;

    while (ret >= 0) {
        ret = avcodec_receive_frame(codec_ctx_, frame_);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return err;
        } else if (ret < 0) {
            return srs_error_new(ERROR_RTC_RTP_MUXER, "Error during decoding");
        }

        int pcm_size = av_get_bytes_per_sample(codec_ctx_->sample_fmt);
        if (pcm_size < 0) {
            return srs_error_new(ERROR_RTC_RTP_MUXER, "Failed to calculate data size");
        }

        for (int i = 0; i < frame_->nb_samples; i++) {
            if (size + pcm_size * codec_ctx_->channels <= max) {
                memcpy(buf + size,frame_->data[0] + pcm_size*codec_ctx_->channels * i, pcm_size * codec_ctx_->channels);
                size += pcm_size * codec_ctx_->channels;
            }
        }
    }

    return err;
}

AVCodecContext* SrsAudioDecoder::codec_ctx()
{
    return codec_ctx_;
}

SrsAudioEncoder::SrsAudioEncoder(SrsAudioCodecId codec, int samplerate, int channels)
    : channels_(channels),
    sampling_rate_(samplerate),
    codec_id_(codec),
    want_bytes_(0)
{
    codec_ctx_ = NULL;
}

SrsAudioEncoder::~SrsAudioEncoder()
{
    if (codec_ctx_) {
        avcodec_free_context(&codec_ctx_);
    }

    if (frame_) {
        av_frame_free(&frame_);
    }
    
}

srs_error_t SrsAudioEncoder::initialize()
{
    srs_error_t err = srs_success;

    if (codec_id_ != SrsAudioCodecIdAAC && codec_id_ != SrsAudioCodecIdOpus) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "Invalid codec name %d", codec_id_);
    }

    frame_ = av_frame_alloc();
    if (!frame_) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "Could not allocate audio frame");
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
    }

    // TODO: FIXME: Show detail error.
    if (avcodec_open2(codec_ctx_, codec, NULL) < 0) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "Could not open codec");
    }

    want_bytes_ = codec_ctx_->channels * codec_ctx_->frame_size * av_get_bytes_per_sample(codec_ctx_->sample_fmt);

    frame_->format = codec_ctx_->sample_fmt;
    frame_->nb_samples = codec_ctx_->frame_size;
    frame_->channel_layout = codec_ctx_->channel_layout;

    if (av_frame_get_buffer(frame_, 0) < 0) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "Could not get audio frame buffer");
    }

    return err;
}

int SrsAudioEncoder::want_bytes()
{
    return want_bytes_;
}

srs_error_t SrsAudioEncoder::encode(SrsSample *frame, char *buf, int &size)
{
    srs_error_t err = srs_success;

    if (want_bytes_ > 0 && frame->size != want_bytes_) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "invalid frame size %d, should be %d", frame->size, want_bytes_);
    }

    // TODO: Directly use frame?
    memcpy(frame_->data[0], frame->bytes, frame->size);  

    /* send the frame for encoding */
    int r0 = avcodec_send_frame(codec_ctx_, frame_);
    if (r0 < 0) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "Error sending the frame to the encoder, %d", r0);
    }

    AVPacket pkt;
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;

    /* read all the available output packets (in general there may be any
     * number of them */
    size = 0;
    while (r0 >= 0) {
        r0 = avcodec_receive_packet(codec_ctx_, &pkt);
        if (r0 == AVERROR(EAGAIN) || r0 == AVERROR_EOF) {
            break;
        } else if (r0 < 0) {
            return srs_error_new(ERROR_RTC_RTP_MUXER, "Error during decoding %d", r0);
        }

        //TODO: fit encoder out more pkt
        memcpy(buf, pkt.data, pkt.size);
        size = pkt.size;
        av_packet_unref(&pkt);

        // TODO: FIXME: Refine api, got more than one packets.
    }

    return err;
}

AVCodecContext* SrsAudioEncoder::codec_ctx()
{
    return codec_ctx_;
}

SrsAudioResample::SrsAudioResample(int src_rate, int src_layout, enum AVSampleFormat src_fmt,
    int src_nb, int dst_rate, int dst_layout, AVSampleFormat dst_fmt)
    : src_rate_(src_rate),
    src_ch_layout_(src_layout),
    src_sample_fmt_(src_fmt),
    src_nb_samples_(src_nb),
    dst_rate_(dst_rate),
    dst_ch_layout_(dst_layout),
    dst_sample_fmt_(dst_fmt)
{
    src_nb_channels_ = 0;
    dst_nb_channels_ = 0;
    src_linesize_ = 0;
    dst_linesize_ = 0;
    dst_nb_samples_ = 0;
    src_data_ = NULL;
    dst_data_ = 0;

    max_dst_nb_samples_ = 0;
    swr_ctx_ = NULL;
}

SrsAudioResample::~SrsAudioResample()
{
    if (src_data_) {
        av_freep(&src_data_[0]);
        av_freep(&src_data_);
        src_data_ = NULL;
    }
    if (dst_data_) {
        av_freep(&dst_data_[0]);
        av_freep(&dst_data_);
        dst_data_ = NULL;
    }
    if (swr_ctx_) {
        swr_free(&swr_ctx_);
        swr_ctx_ = NULL;
    }
}

srs_error_t SrsAudioResample::initialize()
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
        return srs_error_new(ERROR_RTC_RTP_MUXER, "Failed to initialize the resampling context");
    }

    src_nb_channels_ = av_get_channel_layout_nb_channels(src_ch_layout_);
    ret = av_samples_alloc_array_and_samples(&src_data_, &src_linesize_, src_nb_channels_,
                                             src_nb_samples_, src_sample_fmt_, 0);
    if (ret < 0) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "Could not allocate source samples");
    }

    max_dst_nb_samples_ = dst_nb_samples_ =
        av_rescale_rnd(src_nb_samples_, dst_rate_, src_rate_, AV_ROUND_UP);

    dst_nb_channels_ = av_get_channel_layout_nb_channels(dst_ch_layout_);
    ret = av_samples_alloc_array_and_samples(&dst_data_, &dst_linesize_, dst_nb_channels_,
                                             dst_nb_samples_, dst_sample_fmt_, 0);
    if (ret < 0) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "Could not allocate destination samples");
    }

    return err;
}

srs_error_t SrsAudioResample::resample(SrsSample *pcm, char *buf, int &size)
{
    srs_error_t err = srs_success;

    int ret, plane = 1;
    if (src_sample_fmt_ == AV_SAMPLE_FMT_FLTP) {
        plane = 2;
    }
    if (src_linesize_ * plane < pcm->size || pcm->size < 0) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "size not ok");
    }
    memcpy(src_data_[0], pcm->bytes, pcm->size);

    dst_nb_samples_ = av_rescale_rnd(swr_get_delay(swr_ctx_, src_rate_) +
                                    src_nb_samples_, dst_rate_, src_rate_, AV_ROUND_UP);
    if (dst_nb_samples_ > max_dst_nb_samples_) {
        av_freep(&dst_data_[0]);
        ret = av_samples_alloc(dst_data_, &dst_linesize_, dst_nb_channels_,
                                dst_nb_samples_, dst_sample_fmt_, 1);
        if (ret < 0) {
            return srs_error_new(ERROR_RTC_RTP_MUXER, "alloc error");
        }
        max_dst_nb_samples_ = dst_nb_samples_;
    }

    ret = swr_convert(swr_ctx_, dst_data_, dst_nb_samples_, (const uint8_t **)src_data_, src_nb_samples_);
    if (ret < 0) {
       return srs_error_new(ERROR_RTC_RTP_MUXER, "Error while converting"); 
    }

    int dst_bufsize = av_samples_get_buffer_size(&dst_linesize_, dst_nb_channels_,
                                                ret, dst_sample_fmt_, 1);
    if (dst_bufsize < 0) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "Could not get sample buffer size"); 
    }

    int max = size;
    size = 0;
    if (max >= dst_bufsize) {
        memcpy(buf, dst_data_[0], dst_bufsize);
        size = dst_bufsize;
    }

    return err;
}

SrsAudioRecode::SrsAudioRecode(SrsAudioCodecId src_codec, SrsAudioCodecId dst_codec,int channels, int samplerate)
    : dst_channels_(channels),
    dst_samplerate_(samplerate),
    src_codec_(src_codec),
    dst_codec_(dst_codec)
{
    size_ = 0;
    data_ = NULL;

    dec_ = NULL;
    enc_ = NULL;
    resample_ = NULL;
}

SrsAudioRecode::~SrsAudioRecode()
{
    srs_freep(dec_);
    srs_freep(enc_);
    srs_freep(resample_);
    srs_freepa(data_);
}

srs_error_t SrsAudioRecode::initialize()
{
    srs_error_t err = srs_success;

    dec_ = new SrsAudioDecoder(src_codec_);
    if ((err = dec_->initialize()) != srs_success) {
        return srs_error_wrap(err, "dec init");
    }

    enc_ = new SrsAudioEncoder(dst_codec_, dst_samplerate_, dst_channels_);
    if ((err = enc_->initialize()) != srs_success) {
        return srs_error_wrap(err, "enc init");
    }
    
    enc_want_bytes_ = enc_->want_bytes();
    if (enc_want_bytes_ > 0) {
        data_ = new char[enc_want_bytes_];
        srs_assert(data_);
    }

    return err;
}

srs_error_t SrsAudioRecode::transcode(SrsSample *pkt, char **buf, int *buf_len, int &n)
{
    srs_error_t err = srs_success;
    
    if (!dec_) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "dec_ nullptr");
    }

    int decode_len = kPacketBufMax;
    static char decode_buffer[kPacketBufMax];
    if ((err = dec_->decode(pkt, decode_buffer, decode_len)) != srs_success) {
        return srs_error_wrap(err, "decode error");
    }

    if (!resample_) {
        int channel_layout = av_get_default_channel_layout(dst_channels_);
        AVCodecContext *codec_ctx = dec_->codec_ctx();
        resample_ = new SrsAudioResample(codec_ctx->sample_rate, (int)codec_ctx->channel_layout, \
                        codec_ctx->sample_fmt, codec_ctx->frame_size, dst_samplerate_, channel_layout, \
                        enc_->codec_ctx()->sample_fmt);
        if ((err = resample_->initialize()) != srs_success) {
            return srs_error_wrap(err, "init resample");
        }
    }

    SrsSample pcm;
    pcm.bytes = decode_buffer;
    pcm.size = decode_len;
    int resample_len = kFrameBufMax;
    static char resample_buffer[kFrameBufMax];
    static char encode_buffer[kPacketBufMax];
    if ((err = resample_->resample(&pcm, resample_buffer, resample_len)) != srs_success) {
        return srs_error_wrap(err, "resample error");
    }

    n = 0;

    // We can encode it in one time.
    if (enc_want_bytes_ <= 0) {
        int encode_len;
        pcm.bytes = (char *)data_;
        pcm.size = size_;
        if ((err = enc_->encode(&pcm, encode_buffer, encode_len)) != srs_success) {
            return srs_error_wrap(err, "encode error");
        }

        memcpy(buf[n], encode_buffer, encode_len);
        buf_len[n] = encode_len;
        n++;

        return err;
    }

    // Need to refill the sample to data, because the frame size is not matched to encoder.
    int data_left = resample_len;
    if (size_ + data_left < enc_want_bytes_) {
        memcpy(data_ + size_, resample_buffer, data_left);
        size_ += data_left;
        return err;
    }

    int index = 0;
    while (1) {
        data_left = data_left - (enc_want_bytes_ - size_);
        memcpy(data_ + size_, resample_buffer + index, enc_want_bytes_ - size_);
        index += enc_want_bytes_ - size_;
        size_ += enc_want_bytes_ - size_;

        int encode_len;
        pcm.bytes = (char *)data_;
        pcm.size = size_;
        if ((err = enc_->encode(&pcm, encode_buffer, encode_len)) != srs_success) {
            return srs_error_wrap(err, "encode error");
        }

        if (encode_len > 0) {
            memcpy(buf[n], encode_buffer, encode_len);
            buf_len[n] = encode_len;
            n++;
        }

        size_ = 0;
        if(!data_left) {
            break;
        }

        if(data_left < enc_want_bytes_) {
            memcpy(data_ + size_, resample_buffer + index, data_left);
            size_ += data_left;
            break;
        }
    }

    return err;
}
