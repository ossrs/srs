/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2018 Winlin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef SRS_APP_HTTP_STREAM_HPP
#define SRS_APP_HTTP_STREAM_HPP

#include <srs_core.hpp>

#include <srs_app_http_conn.hpp>

class SrsAacTransmuxer;
class SrsMp3Transmuxer;
class SrsFlvTransmuxer;
class SrsTsTransmuxer;

/**
 * A cache for HTTP Live Streaming encoder, to make android(weixin) happy.
 */
class SrsBufferCache : public ISrsCoroutineHandler
{
private:
    double fast_cache;
private:
    SrsMessageQueue* queue;
    SrsSource* source;
    SrsRequest* req;
    SrsCoroutine* trd;
public:
    SrsBufferCache(SrsSource* s, SrsRequest* r);
    virtual ~SrsBufferCache();
    virtual srs_error_t update(SrsSource* s, SrsRequest* r);
public:
    virtual srs_error_t start();
    virtual srs_error_t dump_cache(SrsConsumer* consumer, SrsRtmpJitterAlgorithm jitter);
// interface ISrsEndlessThreadHandler.
public:
    virtual srs_error_t cycle();
};

/**
 * The encoder to transmux RTMP stream.
 */
class ISrsBufferEncoder
{
public:
    ISrsBufferEncoder();
    virtual ~ISrsBufferEncoder();
public:
    /**
     * initialize the encoder with file writer(to http response) and stream cache.
     * @param w the writer to write to http response.
     * @param c the stream cache for audio stream fast startup.
     */
    virtual srs_error_t initialize(SrsFileWriter* w, SrsBufferCache* c) = 0;
    /**
     * write rtmp video/audio/metadata.
     */
    virtual srs_error_t write_audio(int64_t timestamp, char* data, int size) = 0;
    virtual srs_error_t write_video(int64_t timestamp, char* data, int size) = 0;
    virtual srs_error_t write_metadata(int64_t timestamp, char* data, int size) = 0;
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
    virtual srs_error_t dump_cache(SrsConsumer* consumer, SrsRtmpJitterAlgorithm jitter) = 0;
};

/**
 * Transmux RTMP to HTTP Live Streaming.
 */
class SrsFlvStreamEncoder : public ISrsBufferEncoder
{
protected:
    SrsFlvTransmuxer* enc;
public:
    SrsFlvStreamEncoder();
    virtual ~SrsFlvStreamEncoder();
public:
    virtual srs_error_t initialize(SrsFileWriter* w, SrsBufferCache* c);
    virtual srs_error_t write_audio(int64_t timestamp, char* data, int size);
    virtual srs_error_t write_video(int64_t timestamp, char* data, int size);
    virtual srs_error_t write_metadata(int64_t timestamp, char* data, int size);
public:
    virtual bool has_cache();
    virtual srs_error_t dump_cache(SrsConsumer* consumer, SrsRtmpJitterAlgorithm jitter);
};

#ifdef SRS_PERF_FAST_FLV_ENCODER
/**
 * A Fast HTTP FLV Live Streaming, to write multiple tags by writev.
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
    virtual srs_error_t write_tags(SrsSharedPtrMessage** msgs, int count);
};
#endif

/**
 * Transmux RTMP to HTTP TS Streaming.
 */
class SrsTsStreamEncoder : public ISrsBufferEncoder
{
private:
    SrsTsTransmuxer* enc;
public:
    SrsTsStreamEncoder();
    virtual ~SrsTsStreamEncoder();
public:
    virtual srs_error_t initialize(SrsFileWriter* w, SrsBufferCache* c);
    virtual srs_error_t write_audio(int64_t timestamp, char* data, int size);
    virtual srs_error_t write_video(int64_t timestamp, char* data, int size);
    virtual srs_error_t write_metadata(int64_t timestamp, char* data, int size);
public:
    virtual bool has_cache();
    virtual srs_error_t dump_cache(SrsConsumer* consumer, SrsRtmpJitterAlgorithm jitter);
};

/**
 * Transmux RTMP with AAC stream to HTTP AAC Streaming.
 */
class SrsAacStreamEncoder : public ISrsBufferEncoder
{
private:
    SrsAacTransmuxer* enc;
    SrsBufferCache* cache;
public:
    SrsAacStreamEncoder();
    virtual ~SrsAacStreamEncoder();
public:
    virtual srs_error_t initialize(SrsFileWriter* w, SrsBufferCache* c);
    virtual srs_error_t write_audio(int64_t timestamp, char* data, int size);
    virtual srs_error_t write_video(int64_t timestamp, char* data, int size);
    virtual srs_error_t write_metadata(int64_t timestamp, char* data, int size);
public:
    virtual bool has_cache();
    virtual srs_error_t dump_cache(SrsConsumer* consumer, SrsRtmpJitterAlgorithm jitter);
};

/**
 * Transmux RTMP with MP3 stream to HTTP MP3 Streaming.
 */
class SrsMp3StreamEncoder : public ISrsBufferEncoder
{
private:
    SrsMp3Transmuxer* enc;
    SrsBufferCache* cache;
public:
    SrsMp3StreamEncoder();
    virtual ~SrsMp3StreamEncoder();
public:
    virtual srs_error_t initialize(SrsFileWriter* w, SrsBufferCache* c);
    virtual srs_error_t write_audio(int64_t timestamp, char* data, int size);
    virtual srs_error_t write_video(int64_t timestamp, char* data, int size);
    virtual srs_error_t write_metadata(int64_t timestamp, char* data, int size);
public:
    virtual bool has_cache();
    virtual srs_error_t dump_cache(SrsConsumer* consumer, SrsRtmpJitterAlgorithm jitter);
};

/**
 * write stream to http response direclty.
 */
class SrsBufferWriter : public SrsFileWriter
{
private:
    ISrsHttpResponseWriter* writer;
public:
    SrsBufferWriter(ISrsHttpResponseWriter* w);
    virtual ~SrsBufferWriter();
public:
    virtual srs_error_t open(std::string file);
    virtual void close();
public:
    virtual bool is_open();
    virtual int64_t tellg();
public:
    virtual srs_error_t write(void* buf, size_t count, ssize_t* pnwrite);
    virtual srs_error_t writev(const iovec* iov, int iovcnt, ssize_t* pnwrite);
};

/**
 * HTTP Live Streaming, to transmux RTMP to HTTP FLV or other format.
 */
class SrsLiveStream : public ISrsHttpHandler
{
private:
    SrsRequest* req;
    SrsSource* source;
    SrsBufferCache* cache;
public:
    SrsLiveStream(SrsSource* s, SrsRequest* r, SrsBufferCache* c);
    virtual ~SrsLiveStream();
    virtual srs_error_t update(SrsSource* s, SrsRequest* r);
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
private:
    virtual srs_error_t streaming_send_messages(ISrsBufferEncoder* enc, SrsSharedPtrMessage** msgs, int nb_msgs);
};

/**
 * The Live Entry, to handle HTTP Live Streaming.
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
    
    SrsLiveStream* stream;
    SrsBufferCache* cache;
    
    SrsLiveEntry(std::string m);
    
    bool is_flv();
    bool is_ts();
    bool is_mp3();
    bool is_aac();
};

/**
 * The HTTP Live Streaming Server, to serve FLV/TS/MP3/AAC stream.
 */
// TODO: Support multiple stream.
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
    virtual srs_error_t initialize();
    // http flv/ts/mp3/aac stream
public:
    virtual srs_error_t http_mount(SrsSource* s, SrsRequest* r);
    virtual void http_unmount(SrsSource* s, SrsRequest* r);
// interface ISrsReloadHandler.
public:
    virtual srs_error_t on_reload_vhost_added(std::string vhost);
    virtual srs_error_t on_reload_vhost_http_remux_updated(std::string vhost);
// interface ISrsHttpMatchHijacker
public:
    virtual srs_error_t hijack(ISrsHttpMessage* request, ISrsHttpHandler** ph);
private:
    virtual srs_error_t initialize_flv_streaming();
    virtual srs_error_t initialize_flv_entry(std::string vhost);
};

#endif

