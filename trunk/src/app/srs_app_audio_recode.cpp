
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
#include <srs_app_audio_recode.hpp>

static const int kOpusPacketMs  = 20;
static const int kOpusMaxbytes  = 8000;
static const int kFrameBufMax   = 40960;
static const int kPacketBufMax  = 8192;
static const int kPcmBufMax     = 4096*4;

SrsAudioDecoder::SrsAudioDecoder(std::string codec)
    : codec_name_(codec)
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

    if (codec_name_.compare("aac")) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "Invalid codec name");
    }

    const AVCodec *codec = avcodec_find_decoder_by_name(codec_name_.c_str());
    if (!codec) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "Codec not found by name");
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

SrsAudioEncoder::SrsAudioEncoder(int samplerate, int channels, int fec, int complexity)
    : inband_fec_(fec),
    channels_(channels),
    sampling_rate_(samplerate),
    complexity_(complexity)
{
    opus_ = NULL;
}

SrsAudioEncoder::~SrsAudioEncoder()
{
    if (opus_) {
        opus_encoder_destroy(opus_);
        opus_ = NULL;
    }
}

srs_error_t SrsAudioEncoder::initialize()
{
    srs_error_t err = srs_success;

    int error = 0;
    opus_ = opus_encoder_create(sampling_rate_, channels_, OPUS_APPLICATION_VOIP, &error);
    if (error != OPUS_OK) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "Error create Opus encoder");
    }

    switch (sampling_rate_)
    {
    case 48000:
        opus_encoder_ctl(opus_, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_FULLBAND));
        break;

    case 24000:
        opus_encoder_ctl(opus_, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_SUPERWIDEBAND));

    case 16000:
        opus_encoder_ctl(opus_, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_WIDEBAND));
        break;

    case 12000:
        opus_encoder_ctl(opus_, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_MEDIUMBAND));
        break;

    case 8000:
        opus_encoder_ctl(opus_, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_NARROWBAND));
        break;

    default:
        sampling_rate_ = 16000;
        opus_encoder_ctl(opus_, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_WIDEBAND));
        break;
    }
    opus_encoder_ctl(opus_, OPUS_SET_INBAND_FEC(inband_fec_));
    opus_encoder_ctl(opus_, OPUS_SET_COMPLEXITY(complexity_));

    return err;
}

srs_error_t SrsAudioEncoder::encode(SrsSample *frame, char *buf, int &size)
{
    srs_error_t err = srs_success;

    int nb_samples = sampling_rate_ * kOpusPacketMs / 1000;
    if (frame->size != nb_samples * 2 * channels_) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "invalid frame size %d, should be %d", frame->size, nb_samples * 2 * channels_);
    }

    opus_int16 *data = (opus_int16 *)frame->bytes;
	size = opus_encode(opus_, data, nb_samples, (unsigned char *)buf, kOpusMaxbytes);

    return err;
}

SrsAudioResample::SrsAudioResample(int src_rate, int src_layout, enum AVSampleFormat src_fmt,
    int src_nb, int dst_rate, int dst_layout, enum AVSampleFormat dst_fmt)
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
    if (max > dst_bufsize) {
        memcpy(buf, dst_data_[0], dst_bufsize);
        size = dst_bufsize;
    }

    return err;
}

SrsAudioRecode::SrsAudioRecode(int channels, int samplerate)
    : dst_channels_(channels),
    dst_samplerate_(samplerate)
{
    size_ = 0;
    data_ = new char[kPcmBufMax];
}

SrsAudioRecode::~SrsAudioRecode()
{
    if (dec_) {
        delete dec_;
        dec_ = NULL;
    }
    if (enc_) {
        delete enc_;
        enc_ = NULL;
    }
    if (resample_) {
        delete resample_;
        resample_ = NULL;
    }

    delete[] data_;
}

srs_error_t SrsAudioRecode::initialize()
{
    srs_error_t err = srs_success;

    dec_ = new SrsAudioDecoder("aac");
    if (!dec_) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "SrsAudioDecoder failed");
    }
    dec_->initialize();

    enc_ = new SrsAudioEncoder(dst_samplerate_, dst_channels_, 1, 1);
    if (!enc_) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "SrsAudioEncoder failed");
    }
    enc_->initialize();

    resample_ = NULL;

    return err;
}

// TODO: FIXME: Rename to transcode.
srs_error_t SrsAudioRecode::recode(SrsSample *pkt, char **buf, int *buf_len, int &n)
{
    srs_error_t err = srs_success;
    
    if (!dec_) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "dec_ nullptr");
    }

    int decode_len = kPacketBufMax;
    static char decode_buffer[kPacketBufMax];
    if ((err = dec_->decode(pkt, decode_buffer, decode_len)) != srs_success) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "decode error");
    }

    if (!resample_) {
        int channel_layout = av_get_default_channel_layout(dst_channels_);
        AVCodecContext *codec_ctx = dec_->codec_ctx();
        resample_ = new SrsAudioResample(codec_ctx->sample_rate, (int)codec_ctx->channel_layout, \
                        codec_ctx->sample_fmt, codec_ctx->frame_size, dst_samplerate_, channel_layout, \
                        AV_SAMPLE_FMT_S16);

        if (!resample_) {
            return srs_error_new(ERROR_RTC_RTP_MUXER, "SrsAudioResample failed");
        }
        if ((err = resample_->initialize()) != srs_success) {
            return srs_error_wrap(err, "init resample");
        }
    }

    SrsSample pcm;
    pcm.bytes = decode_buffer;
    pcm.size = decode_len;
    int resample_len = kFrameBufMax;
    static char resample_buffer[kFrameBufMax];
    if ((err = resample_->resample(&pcm, resample_buffer, resample_len)) != srs_success) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "resample error");
    }

    n = 0;
    int data_left = resample_len;
    int total;
    total = (dst_samplerate_ * kOpusPacketMs / 1000) * 2 * dst_channels_;

    if (size_ + data_left < total) {
        memcpy(data_ + size_, resample_buffer, data_left);
        size_ += data_left;
    } else {
        int index = 0;
        while (1) {
            data_left = data_left - (total - size_);
            memcpy(data_ + size_, resample_buffer + index, total - size_);
            index += total - size_;
            size_ += total - size_;
            if (!enc_) {
                return srs_error_new(ERROR_RTC_RTP_MUXER, "enc_ nullptr");
            }
            
            int encode_len;
            pcm.bytes = (char *)data_;
            pcm.size = size_;
            static char encode_buffer[kPacketBufMax];
            if ((err = enc_->encode(&pcm, encode_buffer, encode_len)) != srs_success) {
                return srs_error_new(ERROR_RTC_RTP_MUXER, "encode error");
            }

            memcpy(buf[n], encode_buffer, encode_len);
            buf_len[n] = encode_len;
            n++;

            size_ = 0;
            if(!data_left)
                break;

            if(data_left < total) {
                memcpy(data_ + size_, resample_buffer + index, data_left);
                size_ += data_left;
                break;
            }
        }
    }

    return err;
}
