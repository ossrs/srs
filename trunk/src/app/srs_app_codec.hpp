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

#ifndef SRS_APP_CODEC_HPP
#define SRS_APP_CODEC_HPP

/*
#include <srs_app_codec.hpp>
*/

#include <srs_core.hpp>

#define SRS_MAX_CODEC_SAMPLE 128

class SrsStream;

// E.4.3.1 VIDEODATA
// CodecID UB [4]
// Codec Identifier. The following values are defined:
//     2 = Sorenson H.263
//     3 = Screen video
//     4 = On2 VP6
//     5 = On2 VP6 with alpha channel
//     6 = Screen video version 2
//     7 = AVC
enum SrsCodecVideo
{
    SrsCodecVideoReserved                = 0,
    
    SrsCodecVideoSorensonH263             = 2,
    SrsCodecVideoScreenVideo             = 3,
    SrsCodecVideoOn2VP6                 = 4,
    SrsCodecVideoOn2VP6WithAlphaChannel = 5,
    SrsCodecVideoScreenVideoVersion2     = 6,
    SrsCodecVideoAVC                     = 7,
};

// E.4.3.1 VIDEODATA
// Frame Type UB [4]
// Type of video frame. The following values are defined:
//     1 = key frame (for AVC, a seekable frame)
//     2 = inter frame (for AVC, a non-seekable frame)
//     3 = disposable inter frame (H.263 only)
//     4 = generated key frame (reserved for server use only)
//     5 = video info/command frame
enum SrsCodecVideoAVCFrame
{
    SrsCodecVideoAVCFrameReserved                    = 0,
    
    SrsCodecVideoAVCFrameKeyFrame                     = 1,
    SrsCodecVideoAVCFrameInterFrame                 = 2,
    SrsCodecVideoAVCFrameDisposableInterFrame         = 3,
    SrsCodecVideoAVCFrameGeneratedKeyFrame            = 4,
    SrsCodecVideoAVCFrameVideoInfoFrame                = 5,
};

// AVCPacketType IF CodecID == 7 UI8
// The following values are defined:
//     0 = AVC sequence header
//     1 = AVC NALU
//     2 = AVC end of sequence (lower level NALU sequence ender is
//         not required or supported)
enum SrsCodecVideoAVCType
{
    SrsCodecVideoAVCTypeReserved                    = -1,
    
    SrsCodecVideoAVCTypeSequenceHeader                 = 0,
    SrsCodecVideoAVCTypeNALU                         = 1,
    SrsCodecVideoAVCTypeSequenceHeaderEOF             = 2,
};

// SoundFormat UB [4] 
// Format of SoundData. The following values are defined:
//     0 = Linear PCM, platform endian
//     1 = ADPCM
//     2 = MP3
//     3 = Linear PCM, little endian
//     4 = Nellymoser 16 kHz mono
//     5 = Nellymoser 8 kHz mono
//     6 = Nellymoser
//     7 = G.711 A-law logarithmic PCM
//     8 = G.711 mu-law logarithmic PCM
//     9 = reserved
//     10 = AAC
//     11 = Speex
//     14 = MP3 8 kHz
//     15 = Device-specific sound
// Formats 7, 8, 14, and 15 are reserved.
// AAC is supported in Flash Player 9,0,115,0 and higher.
// Speex is supported in Flash Player 10 and higher.
enum SrsCodecAudio
{
    SrsCodecAudioLinearPCMPlatformEndian             = 0,
    SrsCodecAudioADPCM                                 = 1,
    SrsCodecAudioMP3                                 = 2,
    SrsCodecAudioLinearPCMLittleEndian                 = 3,
    SrsCodecAudioNellymoser16kHzMono                 = 4,
    SrsCodecAudioNellymoser8kHzMono                 = 5,
    SrsCodecAudioNellymoser                         = 6,
    SrsCodecAudioReservedG711AlawLogarithmicPCM        = 7,
    SrsCodecAudioReservedG711MuLawLogarithmicPCM    = 8,
    SrsCodecAudioReserved                             = 9,
    SrsCodecAudioAAC                                 = 10,
    SrsCodecAudioSpeex                                 = 11,
    SrsCodecAudioReservedMP3_8kHz                     = 14,
    SrsCodecAudioReservedDeviceSpecificSound         = 15,
};

// AACPacketType IF SoundFormat == 10 UI8
// The following values are defined:
//     0 = AAC sequence header
//     1 = AAC raw
enum SrsCodecAudioType
{
    SrsCodecAudioTypeReserved                        = -1,
    SrsCodecAudioTypeSequenceHeader                 = 0,
    SrsCodecAudioTypeRawData                         = 1,
};

// Sampling rate. The following values are defined:
// 0 = 5.5 kHz = 5512 Hz
// 1 = 11 kHz = 11025 Hz
// 2 = 22 kHz = 22050 Hz
// 3 = 44 kHz = 44100 Hz
enum SrsCodecAudioSampleRate
{
    SrsCodecAudioSampleRateReserved                    = -1,
    
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
    SrsCodecAudioSampleSizeReserved                    = -1,
    
    SrsCodecAudioSampleSize8bit                     = 0,
    SrsCodecAudioSampleSize16bit                     = 1,
};

// Mono or stereo sound
// 0 = Mono sound
// 1 = Stereo sound
enum SrsCodecAudioSoundType
{
    SrsCodecAudioSoundTypeReserved                    = -1, 
    
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
    int nb_buffers;
    SrsCodecBuffer buffers[SRS_MAX_CODEC_SAMPLE];
public:
    bool is_video;
    // video specified
    SrsCodecVideoAVCFrame frame_type;
    SrsCodecVideoAVCType avc_packet_type;
    // CompositionTime, video_file_format_spec_v10_1.pdf, page 78.
    // cts = pts - dts, where dts = flvheader->timestamp.
    int32_t cts;
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
* Annex E. The FLV File Format
*/
class SrsCodec
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
    SrsCodec();
    virtual ~SrsCodec();
// the following function used for hls to build the codec info.
public:
    virtual int audio_aac_demux(int8_t* data, int size, SrsCodecSample* sample);
    virtual int video_avc_demux(int8_t* data, int size, SrsCodecSample* sample);
// the following function used to finger out the flv/rtmp packet detail.
public:
    /**
    * only check the frame_type, not check the codec type.
    */
    static bool video_is_keyframe(int8_t* data, int size);
    /**
    * check codec h264, keyframe, sequence header
    */
    static bool video_is_sequence_header(int8_t* data, int size);
    /**
    * check codec aac, sequence header
    */
    static bool audio_is_sequence_header(int8_t* data, int size);
    /**
    * check codec h264.
    */
    static bool video_is_h264(int8_t* data, int size);
private:
    /**
    * check codec aac.
    */
    static bool audio_is_aac(int8_t* data, int size);
};

#endif