/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

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

#ifndef SRS_APP_AVC_AAC_HPP
#define SRS_APP_AVC_AAC_HPP

/*
#include <srs_app_avc_aac.hpp>
*/

#include <srs_core.hpp>

#include <srs_kernel_codec.hpp>

class SrsStream;

#define SRS_MAX_CODEC_SAMPLE 128
#define _SRS_AAC_SAMPLE_RATE_UNSET 15

// Sampling rate. The following values are defined:
// 0 = 5.5 kHz = 5512 Hz
// 1 = 11 kHz = 11025 Hz
// 2 = 22 kHz = 22050 Hz
// 3 = 44 kHz = 44100 Hz
enum SrsCodecAudioSampleRate
{
    // set to the max value to reserved, for array map.
    SrsCodecAudioSampleRateReserved                    = 4,
    
    SrsCodecAudioSampleRate5512                     = 0,
    SrsCodecAudioSampleRate11025                     = 1,
    SrsCodecAudioSampleRate22050                     = 2,
    SrsCodecAudioSampleRate44100                     = 3,
};

// Size of each audio sample. This parameter only pertains to
// uncompressed formats. Compressed formats always decode
// to 16 bits internally.
// 0 = 8-bit samples
// 1 = 16-bit samples
enum SrsCodecAudioSampleSize
{
    // set to the max value to reserved, for array map.
    SrsCodecAudioSampleSizeReserved                    = 2,
    
    SrsCodecAudioSampleSize8bit                     = 0,
    SrsCodecAudioSampleSize16bit                     = 1,
};

// Mono or stereo sound
// 0 = Mono sound
// 1 = Stereo sound
enum SrsCodecAudioSoundType
{
    // set to the max value to reserved, for array map.
    SrsCodecAudioSoundTypeReserved                    = 2, 
    
    SrsCodecAudioSoundTypeMono                         = 0,
    SrsCodecAudioSoundTypeStereo                     = 1,
};

/**
* buffer indicates the position and size.
*/
class SrsCodecBuffer
{
public:
    /**
    * @remark user must manage the bytes.
    */
    int size;
    char* bytes;
    
    SrsCodecBuffer();
    void append(void* data, int len);
    
    /**
    * free the bytes, 
    * user can invoke it to free the bytes,
    * the SrsCodecBuffer never free automatically.
    */
    void free();
};

/**
* the samples in the flv audio/video packet.
*/
class SrsCodecSample
{
public:
    /**
    * each audio/video raw data packet will dumps to one or multiple buffers,
    * the buffers will write to hls and clear to reset.
    * generally, aac audio packet corresponding to one buffer,
    * where avc/h264 video packet may contains multiple buffer.
    */
    int nb_buffers;
    SrsCodecBuffer buffers[SRS_MAX_CODEC_SAMPLE];
public:
    bool is_video;
    // CompositionTime, video_file_format_spec_v10_1.pdf, page 78.
    // cts = pts - dts, where dts = flvheader->timestamp.
    int32_t cts;
public:
    // video specified
    SrsCodecVideoAVCFrame frame_type;
    SrsCodecVideoAVCType avc_packet_type;
public:
    // audio specified
    SrsCodecAudioSampleRate sound_rate;
    SrsCodecAudioSampleSize sound_size;
    SrsCodecAudioSoundType sound_type;
    SrsCodecAudioType aac_packet_type;
public:
    SrsCodecSample();
    virtual ~SrsCodecSample();
    void clear();
    int add_sample(char* bytes, int size);
};

/**
* the h264/avc and aac codec, for media stream.
* to decode the stream of avc/aac for hls.
*/
class SrsAvcAacCodec
{
private:
    SrsStream* stream;
public:
    /**
    * video specified
    */
    // @see: SrsCodecVideo
    int            video_codec_id;
    // profile_idc, H.264-AVC-ISO_IEC_14496-10.pdf, page 45.
    u_int8_t    avc_profile; 
    // level_idc, H.264-AVC-ISO_IEC_14496-10.pdf, page 45.
    u_int8_t    avc_level; 
    int            width;
    int            height;
    int            video_data_rate; // in bps
    int            frame_rate;
    int            duration;
    // lengthSizeMinusOne, H.264-AVC-ISO_IEC_14496-15.pdf, page 16
    int8_t         NAL_unit_length;
    u_int16_t     sequenceParameterSetLength;
    char*         sequenceParameterSetNALUnit;
    u_int16_t     pictureParameterSetLength;
    char*         pictureParameterSetNALUnit;
public:
    /**
    * audio specified
    */
    // @see: SrsCodecAudioType
    int            audio_codec_id;
    int            audio_data_rate; // in bps
    // 1.6.2.1 AudioSpecificConfig, in aac-mp4a-format-ISO_IEC_14496-3+2001.pdf, page 33.
    // audioObjectType, value defines in 7.1 Profiles, aac-iso-13818-7.pdf, page 40.
    u_int8_t    aac_profile; 
    // samplingFrequencyIndex
    u_int8_t    aac_sample_rate;
    // channelConfiguration
    u_int8_t    aac_channels;
public:
    // the avc extra data, the AVC sequence header,
    // without the flv codec header,
    // @see: ffmpeg, AVCodecContext::extradata
    int         avc_extra_size;
    char*        avc_extra_data;
    // the aac extra data, the AAC sequence header,
    // without the flv codec header,
    // @see: ffmpeg, AVCodecContext::extradata
    int         aac_extra_size;
    char*        aac_extra_data;
public:
    SrsAvcAacCodec();
    virtual ~SrsAvcAacCodec();
// the following function used for hls to build the codec info.
public:
    virtual int audio_aac_demux(int8_t* data, int size, SrsCodecSample* sample);
    virtual int video_avc_demux(int8_t* data, int size, SrsCodecSample* sample);
};

#endif
