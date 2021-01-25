static const AVCodec * const codec_list[] = {
    &ff_aac_encoder,
    &ff_opus_encoder,
    &ff_pcm_alaw_encoder,
    &ff_pcm_mulaw_encoder,
    &ff_libopus_encoder,
    &ff_aac_decoder,
    &ff_aac_fixed_decoder,
    &ff_aac_latm_decoder,
    &ff_pcm_alaw_decoder,
    &ff_pcm_mulaw_decoder,
    &ff_libopus_decoder,
    NULL };
