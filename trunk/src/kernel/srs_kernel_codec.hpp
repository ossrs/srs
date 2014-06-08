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

#ifndef SRS_KERNEL_CODEC_HPP
#define SRS_KERNEL_CODEC_HPP

/*
#include <srs_kernel_codec.hpp>
*/

#include <srs_core.hpp>

class SrsStream;

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

/**
* Annex E. The FLV File Format
* @see SrsAvcAacCodec for the media stream codec.
*/
class SrsFlvCodec
{
public:
    SrsFlvCodec();
    virtual ~SrsFlvCodec();
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