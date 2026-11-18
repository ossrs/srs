//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_kernel_aac.hpp>

// For srs-librtmp, @see https://github.com/ossrs/srs/issues/213
#ifndef _WIN32
#include <unistd.h>
#endif

#include <fcntl.h>
#include <sstream>
using namespace std;

#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_io.hpp>
#include <srs_kernel_log.hpp>

ISrsAacTransmuxer::ISrsAacTransmuxer()
{
}

ISrsAacTransmuxer::~ISrsAacTransmuxer()
{
}

SrsAacTransmuxer::SrsAacTransmuxer()
{
    writer_ = NULL;
    got_sequence_header_ = false;
    aac_object_ = SrsAacObjectTypeReserved;
    aac_sample_rate_ = 0;
    aac_channels_ = 0;
}

SrsAacTransmuxer::~SrsAacTransmuxer()
{
}

srs_error_t SrsAacTransmuxer::initialize(ISrsStreamWriter *fs)
{
    srs_error_t err = srs_success;

    srs_assert(fs);

    writer_ = fs;

    return err;
}

srs_error_t SrsAacTransmuxer::write_audio(int64_t timestamp, char *data, int size)
{
    srs_error_t err = srs_success;

    srs_assert(data);

    timestamp &= 0x7fffffff;

    SrsUniquePtr<SrsBuffer> stream(new SrsBuffer(data, size));

    if (!stream->require(1)) {
        return srs_error_new(ERROR_AAC_DECODE_ERROR, "aac decode audio sound_format failed");
    }

    // @see E.4.2 Audio Tags, video_file_format_spec_v10_1.pdf, page 76
    int8_t sound_format = stream->read_1bytes();

    // int8_t sound_type = sound_format & 0x01;
    // int8_t sound_size = (sound_format >> 1) & 0x01;
    // int8_t sound_rate = (sound_format >> 2) & 0x03;
    sound_format = (sound_format >> 4) & 0x0f;

    if ((SrsAudioCodecId)sound_format != SrsAudioCodecIdAAC) {
        return srs_error_new(ERROR_AAC_DECODE_ERROR, "aac required, format=%d", sound_format);
    }

    if (!stream->require(1)) {
        return srs_error_new(ERROR_AAC_DECODE_ERROR, "aac decode aac_packet_type failed");
    }

    SrsAudioAacFrameTrait aac_packet_type = (SrsAudioAacFrameTrait)stream->read_1bytes();
    if (aac_packet_type == SrsAudioAacFrameTraitSequenceHeader) {
        // 1.6.2.1 AudioSpecificConfig, in ISO_IEC_14496-3-AAC-2001.pdf, page 33.
        // Its first 2 bytes hold audioObjectType (5 bits), samplingFrequencyIndex (4 bits)
        // and channelConfiguration (4 bits).
        if (!stream->require(2)) {
            return srs_error_new(ERROR_AAC_DECODE_ERROR, "aac decode sequence header failed");
        }

        int8_t audioObjectType = stream->read_1bytes();
        aac_sample_rate_ = stream->read_1bytes();

        aac_channels_ = (aac_sample_rate_ >> 3) & 0x0f;
        aac_sample_rate_ = ((audioObjectType << 1) & 0x0e) | ((aac_sample_rate_ >> 7) & 0x01);

        audioObjectType = (audioObjectType >> 3) & 0x1f;
        aac_object_ = (SrsAacObjectType)audioObjectType;

        got_sequence_header_ = true;

        return err;
    }

    if (!got_sequence_header_) {
        return srs_error_new(ERROR_AAC_DECODE_ERROR, "aac no sequence header");
    }

    int16_t aac_raw_length = stream->size() - stream->pos();

    // 1.A.2.2 Audio_Data_Transport_Stream frame, ADTS, in ISO_IEC_14496-3-AAC-2001.pdf, page 75.
    // The header is 7 bytes when protection_absent is set, else 9 bytes with the 16-bit
    // adts_error_check.
    char aac_fixed_header[7];
    if (true) {
        char *pp = aac_fixed_header;
        // aac_frame_length counts the header and error_check bytes too.
        int16_t aac_frame_length = aac_raw_length + 7;

        // syncword 12
        *pp++ = 0xff;
        // 1.A.2.2.1 Fixed Header of ADTS: the last 4 bits of syncword, ID 1, layer 2,
        // protection_absent 1.
        *pp++ = 0xf1;

        // profile 2, sampling_frequency_index 4, private_bit 1, then the first bit of
        // channel_configuration 3.
        SrsAacProfile aac_profile = srs_aac_rtmp2ts(aac_object_);
        *pp++ = ((aac_profile << 6) & 0xc0) | ((aac_sample_rate_ << 2) & 0x3c) | ((aac_channels_ >> 2) & 0x01);
        // The last 2 bits of channel_configuration, original/copy 1, home 1, then
        // 1.A.2.2.2 Variable Header of ADTS: copyright_identification_bit 1,
        // copyright_identification_start 1, and the first 2 bits of aac_frame_length 13.
        *pp++ = ((aac_channels_ << 6) & 0xc0) | ((aac_frame_length >> 11) & 0x03);

        *pp++ = aac_frame_length >> 3;
        // adts_buffer_fullness 11
        *pp++ = (aac_frame_length << 5) & 0xe0;

        // no_raw_data_blocks_in_frame 2
        *pp++ = 0xfc;
    }

    if ((err = writer_->write(aac_fixed_header, 7, NULL)) != srs_success) {
        return srs_error_wrap(err, "write aac header");
    }

    if ((err = writer_->write(data + stream->pos(), aac_raw_length, NULL)) != srs_success) {
        return srs_error_wrap(err, "write aac frame");
    }

    return err;
}
