/*
The MIT License (MIT)

Copyright (c) 2013-2015 winlin

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

#ifndef SRS_KERNEL_AVC_HPP
#define SRS_KERNEL_AVC_HPP

/*
#include <srs_kernel_avc.hpp>
*/

#include <srs_core.hpp>

#include <srs_kernel_codec.hpp>

class SrsStream;
class SrsMpegtsFrame;
class SrsSimpleBuffer;
class SrsAvcAacCodec;
class SrsCodecSample;

/**
* the public data, event HLS disable, others can use it.
*/
/**
* the flv sample rate map
*/
extern int flv_sample_rates[];

/**
* the aac sample rate map
*/
extern int aac_sample_rates[];

#define __SRS_SRS_MAX_CODEC_SAMPLE 128
#define __SRS_AAC_SAMPLE_RATE_UNSET 15

// in ms, for HLS aac flush the audio
#define SRS_CONF_DEFAULT_AAC_DELAY 100

/**
* the FLV/RTMP supported audio sample size.
* Size of each audio sample. This parameter only pertains to
* uncompressed formats. Compressed formats always decode
* to 16 bits internally.
* 0 = 8-bit samples
* 1 = 16-bit samples
*/
enum SrsCodecAudioSampleSize
{
    // set to the max value to reserved, for array map.
    SrsCodecAudioSampleSizeReserved                 = 2,
    
    SrsCodecAudioSampleSize8bit                     = 0,
    SrsCodecAudioSampleSize16bit                    = 1,
};

/**
* the FLV/RTMP supported audio sound type/channel.
* Mono or stereo sound
* 0 = Mono sound
* 1 = Stereo sound
*/
enum SrsCodecAudioSoundType
{
    // set to the max value to reserved, for array map.
    SrsCodecAudioSoundTypeReserved                  = 2, 
    
    SrsCodecAudioSoundTypeMono                      = 0,
    SrsCodecAudioSoundTypeStereo                    = 1,
};

// @see: ngx_rtmp_SrsMpegtsFrame_t
class SrsMpegtsFrame
{
public:
    int64_t         pts;
    int64_t         dts;
    int             pid;
    int             sid;
    int             cc;
    bool            key;
    
    SrsMpegtsFrame();
};

/**
* jitter correct for audio,
* the sample rate 44100/32000 will lost precise,
* when mp4/ts(tbn=90000) covert to flv/rtmp(1000),
* so the Hls on ipad or iphone will corrupt,
* @see nginx-rtmp: est_pts
*/
class SrsTsAacJitter
{
private:
    int64_t base_pts;
    int64_t nb_samples;
    int sync_ms;
public:
    SrsTsAacJitter();
    virtual ~SrsTsAacJitter();
    /**
    * when buffer start, calc the "correct" pts for ts,
    * @param flv_pts, the flv pts calc from flv header timestamp,
    * @param sample_rate, the sample rate in format(flv/RTMP packet header).
    * @param aac_sample_rate, the sample rate in codec(sequence header).
    * @return the calc correct pts.
    */
    virtual int64_t on_buffer_start(int64_t flv_pts, int sample_rate, int aac_sample_rate);
    /**
    * when buffer continue, muxer donot write to file,
    * the audio buffer continue grow and donot need a pts,
    * for the ts audio PES packet only has one pts at the first time.
    */
    virtual void on_buffer_continue();
};

/**
* ts stream cache, 
* use to cache ts stream.
* 
* about the flv tbn problem:
*   flv tbn is 1/1000, ts tbn is 1/90000,
*   when timestamp convert to flv tbn, it will loose precise,
*   so we must gather audio frame together, and recalc the timestamp @see SrsTsAacJitter,
*   we use a aac jitter to correct the audio pts.
*/
class SrsTsCache
{
public:
    // current frame and buffer
    SrsMpegtsFrame* af;
    SrsSimpleBuffer* ab;
    SrsMpegtsFrame* vf;
    SrsSimpleBuffer* vb;
protected:
    // time jitter for aac
    SrsTsAacJitter* aac_jitter;
public:
    SrsTsCache();
    virtual ~SrsTsCache();
public:
    /**
    * write audio to cache
    */
    virtual int cache_audio(SrsAvcAacCodec* codec, int64_t pts, SrsCodecSample* sample);
    /**
    * write video to muxer.
    */
    virtual int cache_video(SrsAvcAacCodec* codec, int64_t dts, SrsCodecSample* sample);
private:
    virtual int do_cache_audio(SrsAvcAacCodec* codec, SrsCodecSample* sample);
    virtual int do_cache_video(SrsAvcAacCodec* codec, SrsCodecSample* sample);
};

/**
* the codec sample unit.
* for h.264 video packet, a NALU is a sample unit.
* for aac raw audio packet, a NALU is the entire aac raw data.
* for sequence header, it's not a sample unit.
*/
class SrsCodecSampleUnit
{
public:
    /**
    * the sample bytes is directly ptr to packet bytes,
    * user should never use it when packet destroyed.
    */
    int size;
    char* bytes;
public:
    SrsCodecSampleUnit();
    virtual ~SrsCodecSampleUnit();
};

/**
* the samples in the flv audio/video packet.
* the sample used to analysis a video/audio packet,
* split the h.264 NALUs to buffers, or aac raw data to a buffer,
* and decode the video/audio specified infos.
* 
* the sample unit:
*       a video packet codec in h.264 contains many NALUs, each is a sample unit.
*       a audio packet codec in aac is a sample unit.
* @remark, the video/audio sequence header is not sample unit,
*       all sequence header stores as extra data, 
*       @see SrsAvcAacCodec.avc_extra_data and SrsAvcAacCodec.aac_extra_data
* @remark, user must clear all samples before decode a new video/audio packet.
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
    int nb_sample_units;
    SrsCodecSampleUnit sample_units[__SRS_SRS_MAX_CODEC_SAMPLE];
public:
    /**
    * whether the sample is video sample which demux from video packet.
    */
    bool is_video;
    /**
    * CompositionTime, video_file_format_spec_v10_1.pdf, page 78.
    * cts = pts - dts, where dts = flvheader->timestamp.
    */
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
public:
    /**
    * clear all samples.
    * the sample units never copy the bytes, it directly use the ptr,
    * so when video/audio packet is destroyed, the sample must be clear.
    * in a word, user must clear sample before demux it.
    * @remark demux sample use SrsAvcAacCodec.audio_aac_demux or video_avc_demux.
    */
    void clear();
    /**
    * add the a sample unit, it's a h.264 NALU or aac raw data.
    * the sample unit directly use the ptr of packet bytes,
    * so user must never use sample unit when packet is destroyed.
    * in a word, user must clear sample before demux it.
    */
    int add_sample_unit(char* bytes, int size);
};

/**
* the h264/avc and aac codec, for media stream.
*
* to demux the FLV/RTMP video/audio packet to sample,
* add each NALUs of h.264 as a sample unit to sample,
* while the entire aac raw data as a sample unit.
*
* for sequence header,
* demux it and save it in the avc_extra_data and aac_extra_data,
* 
* for the codec info, such as audio sample rate,
* decode from FLV/RTMP header, then use codec info in sequence 
* header to override it.
*/
class SrsAvcAacCodec
{
private:
    SrsStream* stream;
public:
    /**
    * metadata specified
    */
    int             duration;
    int             width;
    int             height;
    int             frame_rate;
    // @see: SrsCodecVideo
    int             video_codec_id;
    int             video_data_rate; // in bps
    // @see: SrsCod ecAudioType
    int             audio_codec_id;
    int             audio_data_rate; // in bps
public:
    /**
    * video specified
    */
    // profile_idc, H.264-AVC-ISO_IEC_14496-10.pdf, page 45.
    u_int8_t        avc_profile; 
    // level_idc, H.264-AVC-ISO_IEC_14496-10.pdf, page 45.
    u_int8_t        avc_level; 
    // lengthSizeMinusOne, H.264-AVC-ISO_IEC_14496-15.pdf, page 16
    int8_t          NAL_unit_length;
    u_int16_t       sequenceParameterSetLength;
    char*           sequenceParameterSetNALUnit;
    u_int16_t       pictureParameterSetLength;
    char*           pictureParameterSetNALUnit;
public:
    /**
    * audio specified
    * audioObjectType, in 1.6.2.1 AudioSpecificConfig, page 33,
    * 1.5.1.1 Audio object type definition, page 23,
    *           in aac-mp4a-format-ISO_IEC_14496-3+2001.pdf.
    */
    u_int8_t        aac_profile; 
    /**
    * samplingFrequencyIndex
    */
    u_int8_t        aac_sample_rate;
    /**
    * channelConfiguration
    */
    u_int8_t        aac_channels;
public:
    /**
    * the avc extra data, the AVC sequence header,
    * without the flv codec header,
    * @see: ffmpeg, AVCodecContext::extradata
    */
    int             avc_extra_size;
    char*           avc_extra_data;
    /**
    * the aac extra data, the AAC sequence header,
    * without the flv codec header,
    * @see: ffmpeg, AVCodecContext::extradata
    */
    int             aac_extra_size;
    char*           aac_extra_data;
public:
    SrsAvcAacCodec();
    virtual ~SrsAvcAacCodec();
// the following function used for hls to build the sample and codec.
public:
    /**
    * demux the audio packet in aac codec.
    * the packet mux in FLV/RTMP format defined in flv specification.
    * demux the audio speicified data(sound_format, sound_size, ...) to sample.
    * demux the aac specified data(aac_profile, ...) to codec from sequence header.
    * demux the aac raw to sample units.
    */
    virtual int audio_aac_demux(char* data, int size, SrsCodecSample* sample);
    /**
    * demux the video packet in h.264 codec.
    * the packet mux in FLV/RTMP format defined in flv specification.
    * demux the video specified data(frame_type, codec_id, ...) to sample.
    * demux the h.264 sepcified data(avc_profile, ...) to codec from sequence header.
    * demux the h.264 NALUs to sampe units.
    */
    virtual int video_avc_demux(char* data, int size, SrsCodecSample* sample);
private:
    /**
    * when avc packet type is SrsCodecVideoAVCTypeSequenceHeader,
    * decode the sps and pps.
    */
    virtual int avc_demux_sps_pps(SrsStream* stream);
    /**
    * demux the avc NALU in "AnnexB" 
    * from H.264-AVC-ISO_IEC_14496-10.pdf, page 211.
    */
    virtual int avc_demux_annexb_format(SrsStream* stream, SrsCodecSample* sample);
    /**
    * demux the avc NALU in "ISO Base Media File Format" 
    * from H.264-AVC-ISO_IEC_14496-15.pdf, page 20
    */
    virtual int avc_demux_ibmf_format(SrsStream* stream, SrsCodecSample* sample);
};

#endif

