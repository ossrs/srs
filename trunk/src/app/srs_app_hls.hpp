/*
The MIT License (MIT)

Copyright (c) 2013-2015 SRS(simple-rtmp-server)

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

#include <string>
#include <vector>

#include <srs_kernel_codec.hpp>
#include <srs_kernel_file.hpp>
#include <srs_app_async_call.hpp>

class SrsSharedPtrMessage;
class SrsCodecSample;
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
class SrsHlsSegment;
class SrsTsCache;
class SrsTsContext;

/**
* the handler for hls event.
* for example, we use memory only hls for
*/
class ISrsHlsHandler
{
public:
    ISrsHlsHandler();
    virtual ~ISrsHlsHandler();
public:
    /**
    * when publish stream
    */
    virtual int on_hls_publish(SrsRequest* req) = 0;
    /**
    * when update the m3u8 file.
    */
    virtual int on_update_m3u8(SrsRequest* r, std::string m3u8) = 0;
    /**
    * when reap new ts file.
    */
    virtual int on_update_ts(SrsRequest* r, std::string uri, std::string ts) = 0;
    /**
    * when unpublish stream
    */
    virtual int on_hls_unpublish(SrsRequest* req) = 0;
};

/**
 * * the HLS section, only available when HLS enabled.
 * */
#ifdef SRS_AUTO_HLS

/**
* write to file and cache.
*/
class SrsHlsCacheWriter : public SrsFileWriter
{
private:
    SrsFileWriter impl;
    std::string data;
    bool should_write_cache;
    bool should_write_file;
public:
    SrsHlsCacheWriter(bool write_cache, bool write_file);
    virtual ~SrsHlsCacheWriter();
public:
    /**
    * open file writer, can open then close then open...
    */
    virtual int open(std::string file);
    virtual void close();
public:
    virtual bool is_open();
    virtual int64_t tellg();
public:
    /**
    * write to file. 
    * @param pnwrite the output nb_write, NULL to ignore.
    */
    virtual int write(void* buf, size_t count, ssize_t* pnwrite);
public:
    /**
    * get the string cache.
    */
    virtual std::string cache();
};

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
    SrsHlsCacheWriter* writer;
    SrsTSMuxer* muxer;
    // current segment start dts for m3u8
    int64_t segment_start_dts;
    // whether current segement is sequence header.
    bool is_sequence_header;
public:
    SrsHlsSegment(SrsTsContext* c, bool write_cache, bool write_file, SrsCodecAudio ac, SrsCodecVideo vc);
    virtual ~SrsHlsSegment();
public:
    /**
    * update the segment duration.
    * @current_frame_dts the dts of frame, in tbn of ts.
    */
    virtual void update_duration(int64_t current_frame_dts);
};

/**
 * the hls async call: on_hls
 */
class SrsDvrAsyncCallOnHls : public ISrsAsyncCallTask
{
private:
    std::string path;
    std::string ts_url;
    std::string m3u8;
    std::string m3u8_url;
    int seq_no;
    SrsRequest* req;
    double duration;
public:
    SrsDvrAsyncCallOnHls(SrsRequest* r, std::string p, std::string t, std::string m, std::string mu, int s, double d);
    virtual ~SrsDvrAsyncCallOnHls();
public:
    virtual int call();
    virtual std::string to_string();
};

/**
 * the hls async call: on_hls_notify
 */
class SrsDvrAsyncCallOnHlsNotify : public ISrsAsyncCallTask
{
private:
    std::string ts_url;
    SrsRequest* req;
public:
    SrsDvrAsyncCallOnHlsNotify(SrsRequest* r, std::string u);
    virtual ~SrsDvrAsyncCallOnHlsNotify();
public:
    virtual int call();
    virtual std::string to_string();
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
    SrsRequest* req;
private:
    std::string hls_entry_prefix;
    std::string hls_path;
    std::string hls_ts_file;
    bool hls_cleanup;
    bool hls_wait_keyframe;
    std::string m3u8_dir;
    double hls_aof_ratio;
    double hls_fragment;
    double hls_window;
    SrsAsyncCallWorker* async;
private:
    // whether use floor algorithm for timestamp.
    bool hls_ts_floor;
    // the deviation in piece to adjust the fragment to be more
    // bigger or smaller.
    int deviation_ts;
    // the previous reap floor timestamp,
    // used to detect the dup or jmp or ts.
    int64_t accept_floor_ts;
    int64_t previous_floor_ts;
private:
    int _sequence_no;
    int target_duration;
    std::string m3u8;
    std::string m3u8_url;
private:
    ISrsHlsHandler* handler;
    // TODO: FIXME: supports reload.
    bool should_write_cache;
    bool should_write_file;
private:
    /**
    * m3u8 segments.
    */
    std::vector<SrsHlsSegment*> segments;
    /**
    * current writing segment.
    */
    SrsHlsSegment* current;
    /**
    * the current audio codec, when open new muxer,
    * set the muxer audio codec.
    * @see https://github.com/simple-rtmp-server/srs/issues/301
    */
    SrsCodecAudio acodec;
    /**
     * the ts context, to keep cc continous between ts.
     * @see https://github.com/simple-rtmp-server/srs/issues/375
     */
    SrsTsContext* context;
public:
    SrsHlsMuxer();
    virtual ~SrsHlsMuxer();
public:
    virtual int sequence_no();
    virtual std::string ts_url();
    virtual double duration();
    virtual int deviation();
public:
    /**
    * initialize the hls muxer.
    */
    virtual int initialize(ISrsHlsHandler* h);
    /**
    * when publish, update the config for muxer.
    */
    virtual int update_config(SrsRequest* r, std::string entry_prefix,
        std::string path, std::string m3u8_file, std::string ts_file,
        double fragment, double window, bool ts_floor, double aof_ratio,
        bool cleanup, bool wait_keyframe);
    /**
    * open a new segment(a new ts file),
    * @param segment_start_dts use to calc the segment duration,
    *       use 0 for the first segment of HLS.
    */
    virtual int segment_open(int64_t segment_start_dts);
    virtual int on_sequence_header();
    /**
    * whether segment overflow,
    * that is whether the current segment duration>=(the segment in config)
    */
    virtual bool is_segment_overflow();
    /**
     * whether wait keyframe to reap the ts.
     */
    virtual bool wait_keyframe();
    /**
    * whether segment absolutely overflow, for pure audio to reap segment,
    * that is whether the current segment duration>=2*(the segment in config)
    * @see https://github.com/simple-rtmp-server/srs/issues/151#issuecomment-71155184
    */
    virtual bool is_segment_absolutely_overflow();
public:
    virtual int update_acodec(SrsCodecAudio ac);
    virtual int flush_audio(SrsTsCache* cache);
    virtual int flush_video(SrsTsCache* cache);
    /**
    * close segment(ts).
    * @param log_desc the description for log.
    */
    virtual int segment_close(std::string log_desc);
private:
    virtual int refresh_m3u8();
    virtual int _refresh_m3u8(std::string m3u8_file);
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
    ISrsHlsHandler* handler;
private:
    bool hls_enabled;
    SrsSource* source;
    SrsAvcAacCodec* codec;
    SrsCodecSample* sample;
    SrsRtmpJitter* jitter;
    SrsPithyPrint* pprint;
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
    SrsHls();
    virtual ~SrsHls();
public:
    virtual void dispose();
    virtual int cycle();
public:
    /**
    * initialize the hls by handler and source.
    */
    virtual int initialize(SrsSource* s, ISrsHlsHandler* h);
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
    * @param shared_audio, directly ptr, copy it if need to save it.
    */
    virtual int on_audio(SrsSharedPtrMessage* shared_audio);
    /**
    * mux the video packets to ts.
    * @param shared_video, directly ptr, copy it if need to save it.
    */
    virtual int on_video(SrsSharedPtrMessage* shared_video);
private:
    virtual void hls_show_mux_log();
};

#endif

#endif
