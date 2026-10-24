extern "C" {
#include <libavcodec/avcodec.h>
}

#include <stdio.h>

int main()
{
    const AVCodec *aac = avcodec_find_decoder_by_name("aac");
    AVCodecContext *dec = aac ? avcodec_alloc_context3(aac) : NULL;
    if (!dec || avcodec_open2(dec, aac, NULL) < 0) {
        printf("AAC decoder failed\n");
        return 1;
    }

    const AVCodec *opus = avcodec_find_encoder_by_name("libopus");
    AVCodecContext *enc = opus ? avcodec_alloc_context3(opus) : NULL;
    if (!enc) {
        printf("Opus encoder not found\n");
        return 1;
    }
    enc->sample_rate = 48000;
    enc->channels = 2;
    enc->channel_layout = av_get_default_channel_layout(2);
    enc->bit_rate = 48000;
    enc->sample_fmt = opus->sample_fmts[0];
    enc->time_base.num = 1;
    enc->time_base.den = 48000;
    if (avcodec_open2(enc, opus, NULL) < 0) {
        printf("Opus encoder failed\n");
        return 1;
    }

    avcodec_free_context(&enc);
    avcodec_free_context(&dec);
    printf("FFmpeg %s opened an AAC decoder and a libopus encoder\n", LIBAVCODEC_IDENT);
    return 0;
}
