/*
The MIT License (MIT)

Copyright (c) 2013-2015 SRS(ossrs)

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

#ifndef SRS_APP_HTTP_STREAM_HPP
#define SRS_APP_HTTP_STREAM_HPP

/*
#include <srs_app_http_stream.hpp>
*/

#include <srs_core.hpp>

#include <srs_app_http_conn.hpp>

#ifdef SRS_AUTO_HTTP_SERVER

/**
* for the srs http stream cache, 
* for example, the audio stream cache to make android(weixin) happy.
* we start a thread to shrink the queue.
*/
class SrsStreamCache : public ISrsEndlessThreadHandler
{
private:
    double fast_cache;
private:
    SrsMessageQueue* queue;
    SrsSource* source;
    SrsRequest* req;
    SrsEndlessThread* pthread;
public:
    SrsStreamCache(SrsSource* s, SrsRequest* r);
    virtual ~SrsStreamCache();
    virtual int update(SrsSource* s, SrsRequest* r);
public:
    virtual int start();
    virtual int dump_cache(SrsConsumer* consumer, SrsRtmpJitterAlgorithm jitter);
// interface ISrsEndlessThreadHandler.
public:
    virtual int cycle();
};

/**
* the stream encoder in some codec, for example, flv or aac.
*/
class ISrsStreamEncoder
{
public:
    ISrsStreamEncoder();
    virtual ~ISrsStreamEncoder();
public:
    /**
    * initialize the encoder with file writer(to http response) and stream cache.
    * @param w the writer to write to http response.
    * @param c the stream cache for audio stream fast startup.
    */
    virtual int initialize(SrsFileWriter* w, SrsStreamCache* c) = 0;
    /**
    * write rtmp video/audio/metadata.
    */
    virtual int write_audio(int64_t timestamp, char* data, int size) = 0;
    virtual int write_video(int64_t timestamp, char* data, int size) = 0;
    virtual int write_metadata(int64_t timestamp, char* data, int size) = 0;
public:
    /**
    * for some stream, for example, mp3 and aac, the audio stream,
    * we use large gop cache in encoder, for the gop cache of SrsSource is ignore audio.
    * @return true to use gop cache of encoder; otherwise, use SrsSource.
    */
    virtual bool has_cache() = 0;
    /**
    * dumps the cache of encoder to consumer.
    */
    virtual int dump_cache(SrsConsumer* consumer, SrsRtmpJitterAlgorithm jitter) = 0;
};

/**
* the flv stream encoder, remux rtmp stream to flv stream.
*/
class SrsFlvStreamEncoder : public ISrsStreamEncoder
{
protected:
    SrsFlvEncoder* enc;
public:
    SrsFlvStreamEncoder();
    virtual ~SrsFlvStreamEncoder();
public:
    virtual int initialize(SrsFileWriter* w, SrsStreamCache* c);
    virtual int write_audio(int64_t timestamp, char* data, int size);
    virtual int write_video(int64_t timestamp, char* data, int size);
    virtual int write_metadata(int64_t timestamp, char* data, int size);
public:
    virtual bool has_cache();
    virtual int dump_cache(SrsConsumer* consumer, SrsRtmpJitterAlgorithm jitter);
};

#ifdef SRS_PERF_FAST_FLV_ENCODER
/**
 * the fast flv stream encoder.
 * @see https://github.com/ossrs/srs/issues/405
 */
class SrsFastFlvStreamEncoder : public SrsFlvStreamEncoder
{
public:
    SrsFastFlvStreamEncoder();
    virtual ~SrsFastFlvStreamEncoder();
public:
    /**
     * write the tags in a time.
     */
    virtual int write_tags(SrsSharedPtrMessage** msgs, int count);
};
#endif

/**
* the ts stream encoder, remux rtmp stream to ts stream.
*/
class SrsTsStreamEncoder : public ISrsStreamEncoder
{
private:
    SrsTsEncoder* enc;
public:
    SrsTsStreamEncoder();
    virtual ~SrsTsStreamEncoder();
public:
    virtual int initialize(SrsFileWriter* w, SrsStreamCache* c);
    virtual int write_audio(int64_t timestamp, char* data, int size);
    virtual int write_video(int64_t timestamp, char* data, int size);
    virtual int write_metadata(int64_t timestamp, char* data, int size);
public:
    virtual bool has_cache();
    virtual int dump_cache(SrsConsumer* consumer, SrsRtmpJitterAlgorithm jitter);
};

/**
* the aac stream encoder, remux rtmp stream to aac stream.
*/
class SrsAacStreamEncoder : public ISrsStreamEncoder
{
private:
    SrsAacEncoder* enc;
    SrsStreamCache* cache;
public:
    SrsAacStreamEncoder();
    virtual ~SrsAacStreamEncoder();
public:
    virtual int initialize(SrsFileWriter* w, SrsStreamCache* c);
    virtual int write_audio(int64_t timestamp, char* data, int size);
    virtual int write_video(int64_t timestamp, char* data, int size);
    virtual int write_metadata(int64_t timestamp, char* data, int size);
public:
    virtual bool has_cache();
    virtual int dump_cache(SrsConsumer* consumer, SrsRtmpJitterAlgorithm jitter);
};

/**
* the mp3 stream encoder, remux rtmp stream to mp3 stream.
*/
class SrsMp3StreamEncoder : public ISrsStreamEncoder
{
private:
    SrsMp3Encoder* enc;
    SrsStreamCache* cache;
public:
    SrsMp3StreamEncoder();
    virtual ~SrsMp3StreamEncoder();
public:
    virtual int initialize(SrsFileWriter* w, SrsStreamCache* c);
    virtual int write_audio(int64_t timestamp, char* data, int size);
    virtual int write_video(int64_t timestamp, char* data, int size);
    virtual int write_metadata(int64_t timestamp, char* data, int size);
public:
    virtual bool has_cache();
    virtual int dump_cache(SrsConsumer* consumer, SrsRtmpJitterAlgorithm jitter);
};

/**
* write stream to http response direclty.
*/
class SrsStreamWriter : public SrsFileWriter
{
private:
    ISrsHttpResponseWriter* writer;
public:
    SrsStreamWriter(ISrsHttpResponseWriter* w);
    virtual ~SrsStreamWriter();
public:
    virtual int open(std::string file);
    virtual void close();
public:
    virtual bool is_open();
    virtual int64_t tellg();
public:
    virtual int write(void* buf, size_t count, ssize_t* pnwrite);
    virtual int writev(iovec* iov, int iovcnt, ssize_t* pnwrite);
};

/**
* the flv live stream supports access rtmp in flv over http.
* srs will remux rtmp to flv streaming.
*/
class SrsLiveStream : public ISrsHttpHandler
{
private:
    SrsRequest* req;
    SrsSource* source;
    SrsStreamCache* cache;
public:
    SrsLiveStream(SrsSource* s, SrsRequest* r, SrsStreamCache* c);
    virtual ~SrsLiveStream();
    virtual int update(SrsSource* s, SrsRequest* r);
public:
    virtual int serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
private:
    virtual int streaming_send_messages(ISrsStreamEncoder* enc, SrsSharedPtrMessage** msgs, int nb_msgs);
};

/**
* the srs live entry
*/
struct SrsLiveEntry
{
private:
    bool _is_flv;
    bool _is_ts;
    bool _is_aac;
    bool _is_mp3;
public:
    SrsRequest* req;
    SrsSource* source;
public:
    // for template, the mount contains variables.
    // for concrete stream, the mount is url to access.
    std::string mount;
    // whether hstrs(http stream trigger rtmp source)
    bool hstrs;
    
    SrsLiveStream* stream;
    SrsStreamCache* cache;
    
    SrsLiveEntry(std::string m, bool h);
    void reset_hstrs(bool h);

    bool is_flv();
    bool is_ts();
    bool is_mp3();
    bool is_aac();
};

/**
* the m3u8 stream handler.
*/
class SrsHlsM3u8Stream : public ISrsHttpHandler
{
private:
    std::string m3u8;
public:
    SrsHlsM3u8Stream();
    virtual ~SrsHlsM3u8Stream();
public:
    virtual void set_m3u8(std::string v);
public:
    virtual int serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
};

/**
* the ts stream handler.
*/
class SrsHlsTsStream : public ISrsHttpHandler
{
private:
    std::string ts;
public:
    SrsHlsTsStream();
    virtual ~SrsHlsTsStream();
public:
    virtual void set_ts(std::string v);
public:
    virtual int serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
};

/**
* the srs hls entry.
*/
// TODO: FIXME: use hte hls template and entry.
struct SrsHlsEntry
{
    // for template, the mount contains variables.
    // for concrete stream, the mount is url to access.
    std::string mount;
    
    // the template to create the entry
    SrsHlsEntry* tmpl;

    // key: the m3u8/ts file path.
    // value: the http handler.
    std::map<std::string, ISrsHttpHandler*> streams;
    
    SrsHlsEntry();
};

/**
* the http stream server instance,
* serve http stream, for example, flv/ts/mp3/aac live stream.
*/
class SrsHttpStreamServer : virtual public ISrsReloadHandler
    , virtual public ISrsHttpMatchHijacker
{
private:
    SrsServer* server;
public:
    SrsHttpServeMux mux;
    // the http live streaming template, to create streams.
    std::map<std::string, SrsLiveEntry*> tflvs;
    // the http live streaming streams, crote by template.
    std::map<std::string, SrsLiveEntry*> sflvs;
public:
    SrsHttpStreamServer(SrsServer* svr);
    virtual ~SrsHttpStreamServer();
public:
    virtual int initialize();
// http flv/ts/mp3/aac stream
public:
    virtual int http_mount(SrsSource* s, SrsRequest* r);
    virtual void http_unmount(SrsSource* s, SrsRequest* r);
// interface ISrsReloadHandler.
public:
    virtual int on_reload_vhost_added(std::string vhost);
    virtual int on_reload_vhost_http_remux_updated(std::string vhost);
// interface ISrsHttpMatchHijacker
public:
    virtual int hijack(ISrsHttpMessage* request, ISrsHttpHandler** ph);
private:
    virtual int initialize_flv_streaming();
    virtual int initialize_flv_entry(std::string vhost);
};

#endif

#endif

