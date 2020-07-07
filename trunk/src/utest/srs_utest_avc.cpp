/*
The MIT License (MIT)

Copyright (c) 2013-2020 Winlin

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
#include <srs_utest_avc.hpp>

#include <srs_raw_avc.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_error.hpp>
#include <srs_core_autofree.hpp>

VOID TEST(SrsAVCTest, H264ParseAnnexb)
{
    srs_error_t err;

    // Multiple frames.
    if (true) {
        SrsRawH264Stream h;

        uint8_t buf[] = {
            0, 0, 1, 0xd, 0xa, 0xf, 0, 0, 1, 0xa,
        };
        SrsBuffer b((char*)buf, sizeof(buf));

        char* frame = NULL; int nb_frame = 0;
        HELPER_ASSERT_SUCCESS(h.annexb_demux(&b, &frame, &nb_frame));
        EXPECT_EQ(3, nb_frame);
        EXPECT_EQ((char*)(buf+3), frame);

        HELPER_ASSERT_SUCCESS(h.annexb_demux(&b, &frame, &nb_frame));
        EXPECT_EQ(1, nb_frame);
        EXPECT_EQ((char*)(buf+9), frame);
    }

    // N prefix case, should success.
    if (true) {
        SrsRawH264Stream h;

        uint8_t buf[] = {
            0, 0, 0, 1, 0xd, 0xa, 0xf, 0xa,
        };
        SrsBuffer b((char*)buf, sizeof(buf));

        char* frame = NULL; int nb_frame = 0;
        HELPER_ASSERT_SUCCESS(h.annexb_demux(&b, &frame, &nb_frame));
        EXPECT_EQ(4, nb_frame);
        EXPECT_EQ((char*)(buf+4), frame);
    }

    // No prefix, should fail and return an empty frame.
    if (true) {
        SrsRawH264Stream h;

        uint8_t buf[] = {
            0, 0, 2, 0xd, 0xa, 0xf, 0xa,
        };
        SrsBuffer b((char*)buf, sizeof(buf));

        char* frame = NULL; int nb_frame = 0;
        HELPER_ASSERT_FAILED(h.annexb_demux(&b, &frame, &nb_frame));
        EXPECT_EQ(0, nb_frame);
    }

    // No prefix, should fail and return an empty frame.
    if (true) {
        SrsRawH264Stream h;

        uint8_t buf[] = {
            0, 1, 0xd, 0xa, 0xf, 0xa,
        };
        SrsBuffer b((char*)buf, sizeof(buf));

        char* frame = NULL; int nb_frame = 0;
        HELPER_ASSERT_FAILED(h.annexb_demux(&b, &frame, &nb_frame));
        EXPECT_EQ(0, nb_frame);
    }

    // No prefix, should fail and return an empty frame.
    if (true) {
        SrsRawH264Stream h;

        uint8_t buf[] = {
            0xd, 0xa, 0xf, 0xa,
        };
        SrsBuffer b((char*)buf, sizeof(buf));

        char* frame = NULL; int nb_frame = 0;
        HELPER_ASSERT_FAILED(h.annexb_demux(&b, &frame, &nb_frame));
        EXPECT_EQ(0, nb_frame);
    }

    // Normal case, should success.
    if (true) {
        SrsRawH264Stream h;

        uint8_t buf[] = {
            0, 0, 1, 0xd, 0xa, 0xf, 0xa,
        };
        SrsBuffer b((char*)buf, sizeof(buf));

        char* frame = NULL; int nb_frame = 0;
        HELPER_ASSERT_SUCCESS(h.annexb_demux(&b, &frame, &nb_frame));
        EXPECT_EQ(4, nb_frame);
        EXPECT_EQ((char*)(buf+3), frame);
    }
}

VOID TEST(SrsAVCTest, H264SequenceHeader)
{
    srs_error_t err;

    // For muxing sequence header.
    if (true) {
        SrsRawH264Stream h; string sh;
        HELPER_ASSERT_SUCCESS(h.mux_sequence_header("Hello", "world", 0, 0, sh));
        EXPECT_EQ(11+5+5, (int)sh.length());
        EXPECT_STREQ("Hello", sh.substr(8, 5).c_str());
        EXPECT_STREQ("world", sh.substr(16).c_str());
    }

    // For PPS demuxing.
    if (true) {
        SrsRawH264Stream h;
        string pps;
        HELPER_ASSERT_FAILED(h.pps_demux(NULL, 0, pps));
        EXPECT_TRUE(pps.empty());
    }
    if (true) {
        SrsRawH264Stream h;
        string frame = "Hello, world!", pps;
        HELPER_ASSERT_SUCCESS(h.pps_demux((char*)frame.data(), frame.length(), pps));
        EXPECT_STREQ("Hello, world!", pps.c_str());
    }

    // For SPS demuxing.
    if (true) {
        SrsRawH264Stream h;
        string sps;
        HELPER_ASSERT_SUCCESS(h.sps_demux(NULL, 0, sps));
        EXPECT_TRUE(sps.empty());
    }
    if (true) {
        SrsRawH264Stream h;
        string frame = "Hello, world!", sps;
        HELPER_ASSERT_SUCCESS(h.sps_demux((char*)frame.data(), frame.length(), sps));
        EXPECT_STREQ("Hello, world!", sps.c_str());
    }

    // For PPS.
    if (true) {
        SrsRawH264Stream h;
        uint8_t frame[] = {
            0x9,
        };
        EXPECT_FALSE(h.is_pps((char*)frame, sizeof(frame)));
    }
    if (true) {
        SrsRawH264Stream h;
        uint8_t frame[] = {
            0xf8,
        };
        EXPECT_FALSE(h.is_pps((char*)frame, sizeof(frame)));
    }
    if (true) {
        SrsRawH264Stream h;
        uint8_t frame[] = {
            0xe8,
        };
        EXPECT_TRUE(h.is_pps((char*)frame, sizeof(frame)));
    }
    if (true) {
        SrsRawH264Stream h;
        uint8_t frame[] = {
            8,
        };
        EXPECT_TRUE(h.is_pps((char*)frame, sizeof(frame)));
    }

    // For SPS.
    if (true) {
        SrsRawH264Stream h;
        uint8_t frame[] = {
            0x8,
        };
        EXPECT_FALSE(h.is_sps((char*)frame, sizeof(frame)));
    }
    if (true) {
        SrsRawH264Stream h;
        uint8_t frame[] = {
            0xf7,
        };
        EXPECT_FALSE(h.is_sps((char*)frame, sizeof(frame)));
    }
    if (true) {
        SrsRawH264Stream h;
        uint8_t frame[] = {
            0xe7,
        };
        EXPECT_TRUE(h.is_sps((char*)frame, sizeof(frame)));
    }
    if (true) {
        SrsRawH264Stream h;
        uint8_t frame[] = {
            7,
        };
        EXPECT_TRUE(h.is_sps((char*)frame, sizeof(frame)));
    }
}

VOID TEST(SrsAVCTest, H264IPBFrame)
{
    srs_error_t err;

    // For muxing avc to flv frame.
    if (true) {
        SrsRawH264Stream h; int nb_flv = 0; char* flv = NULL;
        string video("Hello"); int8_t frame_type = SrsVideoAvcFrameTypeKeyFrame; int8_t avc_packet_type = SrsVideoAvcFrameTraitSequenceHeader;
        HELPER_ASSERT_SUCCESS(h.mux_avc2flv(video, frame_type, avc_packet_type, 0, 0x010203, &flv, &nb_flv));
        EXPECT_EQ(10, nb_flv);
        EXPECT_EQ(SrsVideoAvcFrameTypeKeyFrame, uint8_t((flv[0]>>4)&0x0f));
        EXPECT_EQ(SrsVideoAvcFrameTraitSequenceHeader, uint8_t(flv[1]));
        EXPECT_EQ(01, flv[2]); EXPECT_EQ(02, flv[3]); EXPECT_EQ(03, flv[4]);
        EXPECT_STREQ("Hello", HELPER_ARR2STR(flv+5, 5).c_str());
        srs_freep(flv);
    }

    // For muxing I/P/B frame.
    if (true) {
        SrsRawH264Stream h; string frame;
        HELPER_ASSERT_SUCCESS(h.mux_ipb_frame((char*)"Hello", 5, frame));
        EXPECT_EQ(4+5, (int)frame.length());
        EXPECT_EQ(0, (uint8_t)frame.at(0)); EXPECT_EQ(0, (uint8_t)frame.at(1));
        EXPECT_EQ(0, (uint8_t)frame.at(2)); EXPECT_EQ(5, (uint8_t)frame.at(3));
        EXPECT_STREQ("Hello", frame.substr(4).c_str());
    }
}

VOID TEST(SrsAVCTest, AACDemuxADTS)
{
    srs_error_t err;

    // Fail if not adts format.
    if (true) {
        SrsRawAacStream h; char* frame = NULL; int nb_frame = 0; SrsRawAacStreamCodec codec;
        uint8_t b[] = {0xff, 0x09, 0x2c,0x40, 0,0xe0,0}; SrsBuffer buf((char*)b, sizeof(b));
        HELPER_EXPECT_FAILED(h.adts_demux(&buf, &frame, &nb_frame, codec));
    }

    // Fail if less than 7 bytes.
    if (true) {
        SrsRawAacStream h; char* frame = NULL; int nb_frame = 0; SrsRawAacStreamCodec codec;
        uint8_t b[] = {0xff, 0xf9}; SrsBuffer buf((char*)b, sizeof(b));
        HELPER_EXPECT_FAILED(h.adts_demux(&buf, &frame, &nb_frame, codec));
    }

    // For lower sampling rate, such as 5512HZ.
    if (true) {
        SrsRawAacStream h; char* frame = NULL; int nb_frame = 0; SrsRawAacStreamCodec codec;
        uint8_t b[] = {0xff, 0xf9, 0x2c,0x40, 0,0xe0,0}; SrsBuffer buf((char*)b, sizeof(b));
        HELPER_ASSERT_SUCCESS(h.adts_demux(&buf, &frame, &nb_frame, codec));
        EXPECT_EQ(1, codec.protection_absent); // b[1]
        EXPECT_EQ(SrsAacObjectTypeAacMain, codec.aac_object); // b[2]
        EXPECT_EQ(0xb, codec.sampling_frequency_index); // b[2]
        EXPECT_EQ(1, codec.channel_configuration); // b[3]
        EXPECT_EQ(7, codec.frame_length); // b[5]
        EXPECT_EQ(0, nb_frame);

        EXPECT_EQ(SrsAudioSampleRate5512, codec.sound_rate);
        EXPECT_EQ(0, codec.sound_type);
        EXPECT_EQ(1, codec.sound_size);
    }

    // For lower sampling rate, such as 22050HZ.
    if (true) {
        SrsRawAacStream h; char* frame = NULL; int nb_frame = 0; SrsRawAacStreamCodec codec;
        uint8_t b[] = {0xff, 0xf9, 0x18,0x40, 0,0xe0,0}; SrsBuffer buf((char*)b, sizeof(b));
        HELPER_ASSERT_SUCCESS(h.adts_demux(&buf, &frame, &nb_frame, codec));
        EXPECT_EQ(1, codec.protection_absent); // b[1]
        EXPECT_EQ(SrsAacObjectTypeAacMain, codec.aac_object); // b[2]
        EXPECT_EQ(6, codec.sampling_frequency_index); // b[2]
        EXPECT_EQ(1, codec.channel_configuration); // b[3]
        EXPECT_EQ(7, codec.frame_length); // b[5]
        EXPECT_EQ(0, nb_frame);

        EXPECT_EQ(SrsAudioSampleRate22050, codec.sound_rate);
        EXPECT_EQ(0, codec.sound_type);
        EXPECT_EQ(1, codec.sound_size);
    }

    // For higher sampling rate, use 44100HZ.
    if (true) {
        SrsRawAacStream h; char* frame = NULL; int nb_frame = 0; SrsRawAacStreamCodec codec;
        uint8_t b[] = {0xff, 0xf9, 0x04,0x40, 0,0xe0,0}; SrsBuffer buf((char*)b, sizeof(b));
        HELPER_ASSERT_SUCCESS(h.adts_demux(&buf, &frame, &nb_frame, codec));
        EXPECT_EQ(1, codec.protection_absent); // b[1]
        EXPECT_EQ(SrsAacObjectTypeAacMain, codec.aac_object); // b[2]
        EXPECT_EQ(1, codec.sampling_frequency_index); // b[2]
        EXPECT_EQ(1, codec.channel_configuration); // b[3]
        EXPECT_EQ(7, codec.frame_length); // b[5]
        EXPECT_EQ(0, nb_frame);

        EXPECT_EQ(SrsAudioSampleRate44100, codec.sound_rate);
        EXPECT_EQ(0, codec.sound_type);
        EXPECT_EQ(1, codec.sound_size);
    }

    // If protected, there are 2B signature.
    if (true) {
        SrsRawAacStream h; char* frame = NULL; int nb_frame = 0; SrsRawAacStreamCodec codec;
        uint8_t b[] = {0xff, 0xf0, 0x10,0x40, 0x01,0x40,0, 0,0, 1}; SrsBuffer buf((char*)b, sizeof(b));
        HELPER_ASSERT_SUCCESS(h.adts_demux(&buf, &frame, &nb_frame, codec));
        EXPECT_EQ(0, codec.protection_absent); // b[1]
        EXPECT_EQ(SrsAacObjectTypeAacMain, codec.aac_object); // b[2]
        EXPECT_EQ(4, codec.sampling_frequency_index); // b[2]
        EXPECT_EQ(1, codec.channel_configuration); // b[3]
        EXPECT_EQ(10, codec.frame_length); // b[4,5]
        ASSERT_EQ(1, nb_frame); EXPECT_EQ(1, (uint8_t)frame[0]);

        EXPECT_EQ(SrsAudioSampleRate44100, codec.sound_rate);
        EXPECT_EQ(0, codec.sound_type);
        EXPECT_EQ(1, codec.sound_size);
    }

    // Fail if not enough data.
    if (true) {
        SrsRawAacStream h; char* frame = NULL; int nb_frame = 0; SrsRawAacStreamCodec codec;
        uint8_t b[] = {0xff, 0xf0, 0x10,0x40, 0x04,0,0, 1}; SrsBuffer buf((char*)b, sizeof(b));
        HELPER_EXPECT_FAILED(h.adts_demux(&buf, &frame, &nb_frame, codec));
    }

    // If protected, there should be 2B signature.
    if (true) {
        SrsRawAacStream h; char* frame = NULL; int nb_frame = 0; SrsRawAacStreamCodec codec;
        uint8_t b[] = {0xff, 0xf0, 0x10,0x40, 0x01,0,0, 1}; SrsBuffer buf((char*)b, sizeof(b));
        HELPER_EXPECT_FAILED(h.adts_demux(&buf, &frame, &nb_frame, codec));
    }

    // ID should be 1, but we ignored.
    if (true) {
        SrsRawAacStream h; char* frame = NULL; int nb_frame = 0; SrsRawAacStreamCodec codec;
        uint8_t b[] = {0xff, 0xf1, 0x10,0x40, 0x01,0,0, 1}; SrsBuffer buf((char*)b, sizeof(b));
        HELPER_ASSERT_SUCCESS(h.adts_demux(&buf, &frame, &nb_frame, codec));
    }

    // Minimum AAC frame, with raw data.
    if (true) {
        SrsRawAacStream h; char* frame = NULL; int nb_frame = 0; SrsRawAacStreamCodec codec;
        uint8_t b[] = {0xff, 0xf9, 0x10,0x40, 0x01,0,0, 1}; SrsBuffer buf((char*)b, sizeof(b));
        HELPER_ASSERT_SUCCESS(h.adts_demux(&buf, &frame, &nb_frame, codec));
        EXPECT_EQ(1, codec.protection_absent); // b[1]
        EXPECT_EQ(SrsAacObjectTypeAacMain, codec.aac_object); // b[2]
        EXPECT_EQ(4, codec.sampling_frequency_index); // b[2]
        EXPECT_EQ(1, codec.channel_configuration); // b[3]
        EXPECT_EQ(8, codec.frame_length); // b[4]
        ASSERT_EQ(1, nb_frame); EXPECT_EQ(1, (uint8_t)frame[0]);

        EXPECT_EQ(SrsAudioSampleRate44100, codec.sound_rate);
        EXPECT_EQ(0, codec.sound_type);
        EXPECT_EQ(1, codec.sound_size);
    }

    // Minimum AAC frame, no raw data.
    if (true) {
        SrsRawAacStream h; char* frame = NULL; int nb_frame = 0; SrsRawAacStreamCodec codec;
        uint8_t b[] = {0xff, 0xf9, 0x10,0x40, 0,0xe0,0}; SrsBuffer buf((char*)b, sizeof(b));
        HELPER_ASSERT_SUCCESS(h.adts_demux(&buf, &frame, &nb_frame, codec));
        EXPECT_EQ(1, codec.protection_absent); // b[1]
        EXPECT_EQ(SrsAacObjectTypeAacMain, codec.aac_object); // b[2]
        EXPECT_EQ(4, codec.sampling_frequency_index); // b[2]
        EXPECT_EQ(1, codec.channel_configuration); // b[3]
        EXPECT_EQ(7, codec.frame_length); // b[5]
        EXPECT_EQ(0, nb_frame);

        EXPECT_EQ(SrsAudioSampleRate44100, codec.sound_rate);
        EXPECT_EQ(0, codec.sound_type);
        EXPECT_EQ(1, codec.sound_size);
    }
}

VOID TEST(SrsAVCTest, AACMuxSequenceHeader)
{
    srs_error_t err;

    // For sampling rate 22050HZ.
    if (true) {
        SrsRawAacStream h; string sh; SrsRawAacStreamCodec codec;
        codec.aac_object = SrsAacObjectTypeAacMain;
        codec.channel_configuration = 1;
        codec.sound_rate = SrsAudioSampleRate22050;
        codec.sampling_frequency_index = 7;
        HELPER_ASSERT_SUCCESS(h.mux_sequence_header(&codec, sh));
        EXPECT_EQ(2, (int)sh.length());
        EXPECT_EQ(0x0b, (uint8_t)sh.at(0));
        EXPECT_EQ(0x88, (uint8_t)sh.at(1));
    }

    // For sampling rate 11025HZ.
    if (true) {
        SrsRawAacStream h; string sh; SrsRawAacStreamCodec codec;
        codec.aac_object = SrsAacObjectTypeAacMain;
        codec.channel_configuration = 1;
        codec.sound_rate = SrsAudioSampleRate11025;
        codec.sampling_frequency_index = 0xa;
        HELPER_ASSERT_SUCCESS(h.mux_sequence_header(&codec, sh));
        EXPECT_EQ(2, (int)sh.length());
        EXPECT_EQ(0x0d, (uint8_t)sh.at(0));
        EXPECT_EQ(0x08, (uint8_t)sh.at(1));
    }

    // Fail for invalid sampling rate.
    if (true) {
        SrsRawAacStream h; string sh; SrsRawAacStreamCodec codec;
        codec.aac_object = SrsAacObjectTypeAacMain;
        codec.sampling_frequency_index = SrsAacSampleRateUnset;
        codec.sound_rate = SrsAudioSampleRateReserved;
        HELPER_EXPECT_FAILED(h.mux_sequence_header(&codec, sh));
    }

    // Normal case.
    if (true) {
        SrsRawAacStream h; string sh; SrsRawAacStreamCodec codec;
        codec.aac_object = SrsAacObjectTypeAacMain;
        codec.channel_configuration = 1;
        codec.sampling_frequency_index = 4;
        codec.sound_rate = SrsAudioSampleRateReserved;
        HELPER_ASSERT_SUCCESS(h.mux_sequence_header(&codec, sh));
        EXPECT_EQ(2, (int)sh.length());
        EXPECT_EQ(0x0a, (uint8_t)sh.at(0));
        EXPECT_EQ(0x08, (uint8_t)sh.at(1));
    }

    // Fail for invalid aac object.
    if (true) {
        SrsRawAacStream h; string sh; SrsRawAacStreamCodec codec;
        codec.aac_object = SrsAacObjectTypeReserved;
        HELPER_EXPECT_FAILED(h.mux_sequence_header(&codec, sh));
    }

    // Normal case.
    if (true) {
        SrsRawAacStream h; string sh; SrsRawAacStreamCodec codec;
        codec.aac_object = SrsAacObjectTypeAacMain;
        codec.channel_configuration = 1;
        codec.sound_rate = SrsAudioSampleRate44100;
        codec.sampling_frequency_index = 4;
        HELPER_ASSERT_SUCCESS(h.mux_sequence_header(&codec, sh));
        EXPECT_EQ(2, (int)sh.length());
        EXPECT_EQ(0x0a, (uint8_t)sh.at(0));
        EXPECT_EQ(0x08, (uint8_t)sh.at(1));
    }

    // We ignored the sound_rate.
    if (true) {
        SrsRawAacStream h; string sh; SrsRawAacStreamCodec codec;
        codec.aac_object = SrsAacObjectTypeAacMain;
        codec.channel_configuration = 1;
        codec.sound_rate = SrsAudioSampleRate22050;
        codec.sampling_frequency_index = 4;
        HELPER_ASSERT_SUCCESS(h.mux_sequence_header(&codec, sh));
        EXPECT_EQ(2, (int)sh.length());
        EXPECT_EQ(0x0a, (uint8_t)sh.at(0));
        EXPECT_EQ(0x08, (uint8_t)sh.at(1));
    }

    // Use sound_rate if sampling_frequency_index not set.
    if (true) {
        SrsRawAacStream h; string sh; SrsRawAacStreamCodec codec;
        codec.aac_object = SrsAacObjectTypeAacMain;
        codec.channel_configuration = 1;
        codec.sound_rate = SrsAudioSampleRate44100;
        codec.sampling_frequency_index = SrsAacSampleRateUnset;
        HELPER_ASSERT_SUCCESS(h.mux_sequence_header(&codec, sh));
        EXPECT_EQ(2, (int)sh.length());
        EXPECT_EQ(0x0a, (uint8_t)sh.at(0));
        EXPECT_EQ(0x08, (uint8_t)sh.at(1));
    }
}

VOID TEST(SrsAVCTest, AACMuxToFLV)
{
    srs_error_t err;

    // For MP3 frame.
    if (true) {
        SrsRawAacStream h;
        string frame("Hello"); SrsRawAacStreamCodec codec;
        char* flv = NULL; int nb_flv = 0;
        codec.sound_format = SrsAudioCodecIdMP3;
        codec.sound_rate = 0; codec.sound_size = 1; codec.sound_type = 1;
        codec.aac_packet_type = 4;
        HELPER_ASSERT_SUCCESS(h.mux_aac2flv((char*)frame.data(), frame.length(), &codec, 0, &flv, &nb_flv));
        EXPECT_EQ(6, nb_flv);
        EXPECT_EQ(0x23, (uint8_t)flv[0]);
        EXPECT_STREQ("Hello", HELPER_ARR2STR(flv+1,5).c_str());
        srs_freep(flv);
    }

    // For Opus frame.
    if (true) {
        SrsRawAacStream h;
        string frame("Hello"); SrsRawAacStreamCodec codec;
        char* flv = NULL; int nb_flv = 0;
        codec.sound_format = SrsAudioCodecIdOpus;
        codec.sound_rate = 0; codec.sound_size = 1; codec.sound_type = 1;
        codec.aac_packet_type = 4;
        HELPER_ASSERT_SUCCESS(h.mux_aac2flv((char*)frame.data(), frame.length(), &codec, 0, &flv, &nb_flv));
        EXPECT_EQ(6, nb_flv);
        EXPECT_EQ(0xd3, (uint8_t)flv[0]);
        EXPECT_STREQ("Hello", HELPER_ARR2STR(flv+1,5).c_str());
        srs_freep(flv);
    }

    // For Speex frame.
    if (true) {
        SrsRawAacStream h;
        string frame("Hello"); SrsRawAacStreamCodec codec;
        char* flv = NULL; int nb_flv = 0;
        codec.sound_format = SrsAudioCodecIdSpeex;
        codec.sound_rate = 0; codec.sound_size = 1; codec.sound_type = 1;
        codec.aac_packet_type = 4;
        HELPER_ASSERT_SUCCESS(h.mux_aac2flv((char*)frame.data(), frame.length(), &codec, 0, &flv, &nb_flv));
        EXPECT_EQ(6, nb_flv);
        EXPECT_EQ(0xb3, (uint8_t)flv[0]);
        EXPECT_STREQ("Hello", HELPER_ARR2STR(flv+1,5).c_str());
        srs_freep(flv);
    }

    // For AAC frame.
    if (true) {
        SrsRawAacStream h;
        string frame("Hello"); SrsRawAacStreamCodec codec;
        char* flv = NULL; int nb_flv = 0;
        codec.sound_format = SrsAudioCodecIdAAC;
        codec.sound_rate = 0; codec.sound_size = 1; codec.sound_type = 1;
        codec.aac_packet_type = 4;
        HELPER_ASSERT_SUCCESS(h.mux_aac2flv((char*)frame.data(), frame.length(), &codec, 0, &flv, &nb_flv));
        EXPECT_EQ(7, nb_flv);
        EXPECT_EQ(0xa3, (uint8_t)flv[0]);
        EXPECT_EQ(0x04, (uint8_t)flv[1]);
        EXPECT_STREQ("Hello", HELPER_ARR2STR(flv+2,5).c_str());
        srs_freep(flv);
    }
    if (true) {
        SrsRawAacStream h;
        string frame("Hello"); SrsRawAacStreamCodec codec;
        char* flv = NULL; int nb_flv = 0;
        codec.sound_format = SrsAudioCodecIdAAC;
        codec.sound_rate = 1; codec.sound_size = 1; codec.sound_type = 0;
        codec.aac_packet_type = 4;
        HELPER_ASSERT_SUCCESS(h.mux_aac2flv((char*)frame.data(), frame.length(), &codec, 0, &flv, &nb_flv));
        EXPECT_EQ(7, nb_flv);
        EXPECT_EQ(0xa6, (uint8_t)flv[0]);
        EXPECT_EQ(0x04, (uint8_t)flv[1]);
        EXPECT_STREQ("Hello", HELPER_ARR2STR(flv+2,5).c_str());
        srs_freep(flv);
    }
    if (true) {
        SrsRawAacStream h;
        string frame("Hello"); SrsRawAacStreamCodec codec;
        char* flv = NULL; int nb_flv = 0;
        codec.sound_format = SrsAudioCodecIdAAC;
        codec.sound_rate = 1; codec.sound_size = 0; codec.sound_type = 1;
        codec.aac_packet_type = 4;
        HELPER_ASSERT_SUCCESS(h.mux_aac2flv((char*)frame.data(), frame.length(), &codec, 0, &flv, &nb_flv));
        EXPECT_EQ(7, nb_flv);
        EXPECT_EQ(0xa5, (uint8_t)flv[0]);
        EXPECT_EQ(0x04, (uint8_t)flv[1]);
        EXPECT_STREQ("Hello", HELPER_ARR2STR(flv+2,5).c_str());
        srs_freep(flv);
    }
    if (true) {
        SrsRawAacStream h;
        string frame("Hello"); SrsRawAacStreamCodec codec;
        char* flv = NULL; int nb_flv = 0;
        codec.sound_format = SrsAudioCodecIdAAC;
        codec.sound_rate = 1; codec.sound_size = 1; codec.sound_type = 1;
        codec.aac_packet_type = 4;
        HELPER_ASSERT_SUCCESS(h.mux_aac2flv((char*)frame.data(), frame.length(), &codec, 0, &flv, &nb_flv));
        EXPECT_EQ(7, nb_flv);
        EXPECT_EQ(0xa7, (uint8_t)flv[0]);
        EXPECT_EQ(0x04, (uint8_t)flv[1]);
        EXPECT_STREQ("Hello", HELPER_ARR2STR(flv+2,5).c_str());
        srs_freep(flv);
    }
    if (true) {
        SrsRawAacStream h;
        string frame("Hello"); SrsRawAacStreamCodec codec;
        char* flv = NULL; int nb_flv = 0;
        codec.sound_format = SrsAudioCodecIdAAC;
        codec.sound_rate = 3; codec.sound_size = 1; codec.sound_type = 1;
        codec.aac_packet_type = 4;
        HELPER_ASSERT_SUCCESS(h.mux_aac2flv((char*)frame.data(), frame.length(), &codec, 0, &flv, &nb_flv));
        EXPECT_EQ(7, nb_flv);
        EXPECT_EQ(0xaf, (uint8_t)flv[0]);
        EXPECT_EQ(0x04, (uint8_t)flv[1]);
        EXPECT_STREQ("Hello", HELPER_ARR2STR(flv+2,5).c_str());
        srs_freep(flv);
    }
}

