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

#ifndef SRS_APP_HLS_HPP
#define SRS_APP_HLS_HPP

/*
#include <srs_app_hls.hpp>
*/
#include <srs_core.hpp>

/**
* the HLS section, only available when HLS enabled.
*/
#ifdef SRS_AUTO_HLS

#include <string>
#include <vector>

class SrsSharedPtrMessage;
class SrsCodecSample;
class SrsMpegtsFrame;
class SrsAmf0Object;
class SrsRtmpJitter;
class SrsTSMuxer;
class SrsAvcAacCodec;
class SrsRequest;
class SrsPithyPrint;
class SrsSource;
class SrsFileWriter;
class SrsSimpleBuffer;
class SrsTsAacJitter;
class SrsTsCache;

/**
* the wrapper of m3u8 segment from specification:
*
* 3.3.2.  EXTINF
* The EXTINF tag specifies the duration of a media segment.
*/
class SrsHlsSegment
{
public:
    // duration in seconds in m3u8.
    double duration;
    // sequence number in m3u8.
    int sequence_no;
    // ts uri in m3u8.
    std::string uri;
    // ts full file to write.
    std::string full_path;
    // the muxer to write ts.
    SrsFileWriter* writer;
    SrsTSMuxer* muxer;
    // current segment start dts for m3u8
    int64_t segment_start_dts;
    // whether current segement is sequence header.
    bool is_sequence_header;
    
    SrsHlsSegment();
    virtual ~SrsHlsSegment();
    
    /**
    * update the segment duration.
    * @current_frame_dts the dts of frame, in tbn of ts.
    */
    virtual void update_duration(int64_t current_frame_dts);
};

/**
* muxer the HLS stream(m3u8 and ts files).
* generally, the m3u8 muxer only provides methods to open/close segments,
* to flush video/audio, without any mechenisms.
* 
* that is, user must use HlsCache, which will control the methods of muxer,
* and provides HLS mechenisms.
*/
class SrsHlsMuxer
{
private:
    std::string app;
    std::string stream;
private:
    std::string hls_path;
    int hls_fragment;
    int hls_window;
private:
    int _sequence_no;
    std::string m3u8;
private:
    /**
    * m3u8 segments.
    */
    std::vector<SrsHlsSegment*> segments;
    /**
    * current writing segment.
    */
    SrsHlsSegment* current;
public:
    SrsHlsMuxer();
    virtual ~SrsHlsMuxer();
public:
    virtual int sequence_no();
public:
    virtual int update_config(std::string _app, std::string _stream, std::string path, int fragment, int window);
    /**
    * open a new segment(a new ts file),
    * @param segment_start_dts use to calc the segment duration,
    *       use 0 for the first segment of HLS.
    */
    virtual int segment_open(int64_t segment_start_dts);
    virtual int on_sequence_header();
    /**
    * whether video overflow,
    * that is whether the current segment duration >= the segment in config
    */
    virtual bool is_segment_overflow();
    virtual int flush_audio(SrsMpegtsFrame* af, SrsSimpleBuffer* ab);
    virtual int flush_video(SrsMpegtsFrame* af, SrsSimpleBuffer* ab, SrsMpegtsFrame* vf, SrsSimpleBuffer* vb);
    /**
    * close segment(ts).
    * @param log_desc the description for log.
    */
    virtual int segment_close(std::string log_desc);
private:
    virtual int refresh_m3u8();
    virtual int _refresh_m3u8(int& fd, std::string m3u8_file);
    virtual int create_dir();
};

/**
* hls stream cache, 
* use to cache hls stream and flush to hls muxer.
* 
* when write stream to ts file:
* video frame will directly flush to M3u8Muxer,
* audio frame need to cache, because it's small and flv tbn problem.
* 
* whatever, the Hls cache used to cache video/audio,
* and flush video/audio to m3u8 muxer if needed.
* 
* about the flv tbn problem:
*   flv tbn is 1/1000, ts tbn is 1/90000,
*   when timestamp convert to flv tbn, it will loose precise,
*   so we must gather audio frame together, and recalc the timestamp @see SrsTsAacJitter,
*   we use a aac jitter to correct the audio pts.
*/
class SrsHlsCache
{
private:
    // the audio cache buffer start pts, to flush audio if full.
    int64_t audio_buffer_start_pts;
    SrsTsCache* cache;
public:
    SrsHlsCache();
    virtual ~SrsHlsCache();
public:
    /**
    * when publish or unpublish stream.
    */
    virtual int on_publish(SrsHlsMuxer* muxer, SrsRequest* req, int64_t segment_start_dts);
    virtual int on_unpublish(SrsHlsMuxer* muxer);
    /**
    * when get sequence header, 
    * must write a #EXT-X-DISCONTINUITY to m3u8.
    * @see: hls-m3u8-draft-pantos-http-live-streaming-12.txt
    * @see: 3.4.11.  EXT-X-DISCONTINUITY
    */
    virtual int on_sequence_header(SrsHlsMuxer* muxer);
    /**
    * write audio to cache, if need to flush, flush to muxer.
    */
    virtual int write_audio(SrsAvcAacCodec* codec, SrsHlsMuxer* muxer, int64_t pts, SrsCodecSample* sample);
    /**
    * write video to muxer.
    */
    virtual int write_video(SrsAvcAacCodec* codec, SrsHlsMuxer* muxer, int64_t dts, SrsCodecSample* sample);
private:
    /**
    * reopen the muxer for a new hls segment,
    * close current segment, open a new segment,
    * then write the key frame to the new segment.
    * so, user must reap_segment then flush_video to hls muxer.
    */
    virtual int reap_segment(std::string log_desc, SrsHlsMuxer* muxer, int64_t segment_start_dts);
};

/**
* delivery RTMP stream to HLS(m3u8 and ts),
* SrsHls provides interface with SrsSource.
* TODO: FIXME: add utest for hls.
*/
class SrsHls
{
private:
    SrsHlsMuxer* muxer;
    SrsHlsCache* hls_cache;
private:
    bool hls_enabled;
    SrsSource* source;
    SrsAvcAacCodec* codec;
    SrsCodecSample* sample;
    SrsRtmpJitter* jitter;
    SrsPithyPrint* pithy_print;
    /**
    * we store the stream dts,
    * for when we notice the hls cache to publish,
    * it need to know the segment start dts.
    * 
    * for example. when republish, the stream dts will 
    * monotonically increase, and the ts dts should start 
    * from current dts.
    * 
    * or, simply because the HlsCache never free when unpublish,
    * so when publish or republish it must start at stream dts,
    * not zero dts.
    */
    int64_t stream_dts;
public:
    SrsHls(SrsSource* _source);
    virtual ~SrsHls();
public:
    /**
    * publish stream event, continue to write the m3u8,
    * for the muxer object not destroyed.
    */
    virtual int on_publish(SrsRequest* req);
    /**
    * the unpublish event, only close the muxer, donot destroy the 
    * muxer, for when we continue to publish, the m3u8 will continue.
    */
    virtual void on_unpublish();
    /**
    * get some information from metadata, it's optinal.
    */
    virtual int on_meta_data(SrsAmf0Object* metadata);
    /**
    * mux the audio packets to ts.
    * @param __audio, directly ptr, copy it if need to save it.
    */
    virtual int on_audio(SrsSharedPtrMessage* __audio);
    /**
    * mux the video packets to ts.
    * @param __video, directly ptr, copy it if need to save it.
    */
    virtual int on_video(SrsSharedPtrMessage* __video);
private:
    virtual void hls_mux();
};

#endif

#endif
