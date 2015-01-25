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

#ifndef SRS_KERNEL_TS_HPP
#define SRS_KERNEL_TS_HPP

/*
#include <srs_kernel_ts.hpp>
*/
#include <srs_core.hpp>

#include <string>

#include <srs_kernel_codec.hpp>

class SrsTsCache;
class SrsTSMuxer;
class SrsFileWriter;
class SrsFileReader;
class SrsAvcAacCodec;
class SrsCodecSample;
class SrsSimpleBuffer;

// Transport Stream packets are 188 bytes in length.
#define SRS_TS_PACKET_SIZE          188

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
* write data from frame(header info) and buffer(data) to ts file.
* it's a simple object wrapper for utility from nginx-rtmp: SrsMpegtsWriter
*/
class SrsTSMuxer
{
private:
    SrsCodecAudio previous;
    SrsCodecAudio current;
private:
    SrsFileWriter* writer;
    std::string path;
public:
    SrsTSMuxer(SrsFileWriter* w);
    virtual ~SrsTSMuxer();
public:
    /**
    * open the writer, donot write the PSI of ts.
    */
    virtual int open(std::string _path);
    /**
    * when open ts, we donot write the header(PSI),
    * for user may need to update the acodec to mp3 or others,
    * so we use delay write PSI, when write audio or video.
    * @remark for audio aac codec, for example, SRS1, it's ok to write PSI when open ts.
    * @see https://github.com/winlinvip/simple-rtmp-server/issues/301
    */
    virtual int update_acodec(SrsCodecAudio acodec);
    /**
    * write an audio frame to ts, 
    * @remark write PSI first when not write yet.
    */
    virtual int write_audio(SrsMpegtsFrame* af, SrsSimpleBuffer* ab);
    /**
    * write a video frame to ts, 
    * @remark write PSI first when not write yet.
    */
    virtual int write_video(SrsMpegtsFrame* vf, SrsSimpleBuffer* vb);
    /**
    * close the writer.
    */
    virtual void close();
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
public:
    // the audio cache buffer start pts, to flush audio if full.
    // @remark the pts is not the adjust one, it's the orignal pts.
    int64_t audio_buffer_start_pts;
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
* encode data to ts file.
*/
class SrsTsEncoder
{
private:
    SrsFileWriter* _fs;
private:
    SrsAvcAacCodec* codec;
    SrsCodecSample* sample;
    SrsTsCache* cache;
    SrsTSMuxer* muxer;
public:
    SrsTsEncoder();
    virtual ~SrsTsEncoder();
public:
    /**
    * initialize the underlayer file stream.
    */
    virtual int initialize(SrsFileWriter* fs);
public:
    /**
    * write audio/video packet.
    * @remark assert data is not NULL.
    */
    virtual int write_audio(int64_t timestamp, char* data, int size);
    virtual int write_video(int64_t timestamp, char* data, int size);
private:
    virtual int flush_audio();
    virtual int flush_video();
};

#endif

