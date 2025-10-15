//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_HTTP_STREAM_HPP
#define SRS_APP_HTTP_STREAM_HPP

#include <srs_app_async_call.hpp>
#include <srs_app_http_conn.hpp>
#include <srs_app_security.hpp>
#include <srs_core.hpp>

#include <vector>

class SrsAacTransmuxer;
class SrsMp3Transmuxer;
class ISrsFlvTransmuxer;
class SrsTsTransmuxer;
class SrsAsyncCallWorker;
class ISrsAppConfig;
class ISrsLiveSourceManager;
class ISrsStatistic;
class ISrsHttpHooks;
class ISrsMessageQueue;
class ISrsLiveConsumer;
class ISrsTsTransmuxer;
class ISrsAacTransmuxer;
class ISrsBufferCache;
class ISrsMp3Transmuxer;
class ISrsCommonHttpHandler;

// The cache for HTTP Live Streaming encoder.
class ISrsBufferCache
{
public:
    ISrsBufferCache();
    virtual ~ISrsBufferCache();

public:
    virtual srs_error_t start() = 0;
    virtual void stop() = 0;

public:
    virtual bool alive() = 0;
    virtual srs_error_t dump_cache(ISrsLiveConsumer *consumer, SrsRtmpJitterAlgorithm jitter) = 0;
    virtual srs_error_t update_auth(ISrsRequest *r) = 0;
};

// A cache for HTTP Live Streaming encoder, to make android(weixin) happy.
class SrsBufferCache : public ISrsCoroutineHandler, public ISrsBufferCache
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsLiveSourceManager *live_sources_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_utime_t fast_cache_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsMessageQueue *queue_;
    ISrsRequest *req_;
    ISrsCoroutine *trd_;

public:
    SrsBufferCache(ISrsRequest *r);
    virtual ~SrsBufferCache();
    virtual srs_error_t update_auth(ISrsRequest *r);

public:
    virtual srs_error_t start();
    virtual void stop();
    virtual bool alive();
    virtual srs_error_t dump_cache(ISrsLiveConsumer *consumer, SrsRtmpJitterAlgorithm jitter);
    // Interface ISrsEndlessThreadHandler.
public:
    virtual srs_error_t cycle();
};

// The encoder to transmux RTMP stream.
class ISrsBufferEncoder
{
public:
    ISrsBufferEncoder();
    virtual ~ISrsBufferEncoder();

public:
    // Initialize the encoder with file writer(to http response) and stream cache.
    // @param w the writer to write to http response.
    // @param c the stream cache for audio stream fast startup.
    virtual srs_error_t initialize(SrsFileWriter *w, ISrsBufferCache *c) = 0;
    // Write rtmp video/audio/metadata.
    virtual srs_error_t write_audio(int64_t timestamp, char *data, int size) = 0;
    virtual srs_error_t write_video(int64_t timestamp, char *data, int size) = 0;
    virtual srs_error_t write_metadata(int64_t timestamp, char *data, int size) = 0;

public:
    // For some stream, for example, mp3 and aac, the audio stream,
    // we use large gop cache in encoder, for the gop cache of SrsLiveSource is ignore audio.
    // @return true to use gop cache of encoder; otherwise, use SrsLiveSource.
    virtual bool has_cache() = 0;
    // Dumps the cache of encoder to consumer.
    virtual srs_error_t dump_cache(ISrsLiveConsumer *consumer, SrsRtmpJitterAlgorithm jitter) = 0;
};

// Transmux RTMP to HTTP Live Streaming.
class SrsFlvStreamEncoder : public ISrsBufferEncoder
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsFlvTransmuxer *enc_;
    bool header_written_;
    bool has_audio_;
    bool has_video_;
    bool guess_has_av_;

public:
    SrsFlvStreamEncoder();
    virtual ~SrsFlvStreamEncoder();

public:
    virtual srs_error_t initialize(SrsFileWriter *w, ISrsBufferCache *c);
    virtual srs_error_t write_audio(int64_t timestamp, char *data, int size);
    virtual srs_error_t write_video(int64_t timestamp, char *data, int size);
    virtual srs_error_t write_metadata(int64_t timestamp, char *data, int size);

public:
    void set_drop_if_not_match(bool v);
    void set_has_audio(bool v);
    void set_has_video(bool v);
    void set_guess_has_av(bool v);

public:
    virtual bool has_cache();
    virtual srs_error_t dump_cache(ISrsLiveConsumer *consumer, SrsRtmpJitterAlgorithm jitter);

public:
    // Write the tags in a time.
    virtual srs_error_t write_tags(SrsMediaPacket **msgs, int count);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t write_header(bool has_video, bool has_audio);
};

// Transmux RTMP to HTTP TS Streaming.
class SrsTsStreamEncoder : public ISrsBufferEncoder
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsTsTransmuxer *enc_;

public:
    SrsTsStreamEncoder();
    virtual ~SrsTsStreamEncoder();

public:
    virtual srs_error_t initialize(SrsFileWriter *w, ISrsBufferCache *c);
    virtual srs_error_t write_audio(int64_t timestamp, char *data, int size);
    virtual srs_error_t write_video(int64_t timestamp, char *data, int size);
    virtual srs_error_t write_metadata(int64_t timestamp, char *data, int size);

public:
    virtual bool has_cache();
    virtual srs_error_t dump_cache(ISrsLiveConsumer *consumer, SrsRtmpJitterAlgorithm jitter);

public:
    void set_has_audio(bool v);
    void set_has_video(bool v);
    void set_guess_has_av(bool v);
};

// Transmux RTMP with AAC stream to HTTP AAC Streaming.
class SrsAacStreamEncoder : public ISrsBufferEncoder
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAacTransmuxer *enc_;
    ISrsBufferCache *cache_;

public:
    SrsAacStreamEncoder();
    virtual ~SrsAacStreamEncoder();

public:
    virtual srs_error_t initialize(SrsFileWriter *w, ISrsBufferCache *c);
    virtual srs_error_t write_audio(int64_t timestamp, char *data, int size);
    virtual srs_error_t write_video(int64_t timestamp, char *data, int size);
    virtual srs_error_t write_metadata(int64_t timestamp, char *data, int size);

public:
    virtual bool has_cache();
    virtual srs_error_t dump_cache(ISrsLiveConsumer *consumer, SrsRtmpJitterAlgorithm jitter);
};

// Transmux RTMP with MP3 stream to HTTP MP3 Streaming.
class SrsMp3StreamEncoder : public ISrsBufferEncoder
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsMp3Transmuxer *enc_;
    ISrsBufferCache *cache_;

public:
    SrsMp3StreamEncoder();
    virtual ~SrsMp3StreamEncoder();

public:
    virtual srs_error_t initialize(SrsFileWriter *w, ISrsBufferCache *c);
    virtual srs_error_t write_audio(int64_t timestamp, char *data, int size);
    virtual srs_error_t write_video(int64_t timestamp, char *data, int size);
    virtual srs_error_t write_metadata(int64_t timestamp, char *data, int size);

public:
    virtual bool has_cache();
    virtual srs_error_t dump_cache(ISrsLiveConsumer *consumer, SrsRtmpJitterAlgorithm jitter);
};

// Write stream to http response direclty.
class SrsBufferWriter : public SrsFileWriter
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsHttpResponseWriter *writer_;

public:
    SrsBufferWriter(ISrsHttpResponseWriter *w);
    virtual ~SrsBufferWriter();

public:
    virtual srs_error_t open(std::string file);
    virtual void close();

public:
    virtual bool is_open();
    virtual int64_t tellg();

public:
    virtual srs_error_t write(void *buf, size_t count, ssize_t *pnwrite);
    virtual srs_error_t writev(const iovec *iov, int iovcnt, ssize_t *pnwrite);
};

// Interface for HTTP Live Streaming.
class ISrsLiveStream : public ISrsHttpHandler, public ISrsExpire
{
public:
    ISrsLiveStream();
    virtual ~ISrsLiveStream();

public:
    virtual srs_error_t update_auth(ISrsRequest *r) = 0;
    virtual bool alive() = 0;
};

// HTTP Live Streaming, to transmux RTMP to HTTP FLV or other format.
// TODO: FIXME: Rename to SrsHttpLive
class SrsLiveStream : public ISrsLiveStream
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsLiveSourceManager *live_sources_;
    ISrsStatistic *stat_;
    ISrsHttpHooks *hooks_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRequest *req_;
    ISrsBufferCache *cache_;
    ISrsSecurity *security_;
    // For multiple viewers, which means there will more than one alive viewers for a live stream, so we must
    // use an int value to represent if there is any viewer is alive. We should never do cleanup unless all
    // viewers closed the connection.
    std::vector<ISrsExpire *> viewers_;

public:
    SrsLiveStream(ISrsRequest *r, ISrsBufferCache *c);
    virtual ~SrsLiveStream();
    virtual srs_error_t update_auth(ISrsRequest *r);

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t serve_http_impl(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);

public:
    virtual bool alive();
    // Interface ISrsExpire
public:
    virtual void expire();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t do_serve_http(SrsLiveSource *source, ISrsLiveConsumer *consumer, ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
    virtual srs_error_t http_hooks_on_play(ISrsHttpMessage *r);
    virtual void http_hooks_on_stop(ISrsHttpMessage *r);
    virtual srs_error_t streaming_send_messages(ISrsBufferEncoder *enc, SrsMediaPacket **msgs, int nb_msgs);
};

// The Live Entry, to handle HTTP Live Streaming.
struct SrsLiveEntry {
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    bool is_flv_;
    bool is_ts_;
    bool is_aac_;
    bool is_mp3_;

public:
    // We will free the request.
    ISrsRequest *req_;

public:
    // For template, the mount contains variables.
    // For concrete stream, the mount is url to access.
    std::string mount_;

    ISrsLiveStream *stream_;
    ISrsBufferCache *cache_;

    // Whether is disposing the entry.
    bool disposing_;

    SrsLiveEntry(std::string m);
    virtual ~SrsLiveEntry();

    bool is_flv();
    bool is_ts();
    bool is_mp3();
    bool is_aac();
};

// The HTTP Live Streaming Server, to serve FLV/TS/MP3/AAC stream.
class ISrsHttpStreamServer : public ISrsHttpDynamicMatcher
{
public:
    ISrsHttpStreamServer();
    virtual ~ISrsHttpStreamServer();

public:
    virtual void assemble() = 0;
    virtual srs_error_t initialize() = 0;
    // HTTP flv/ts/mp3/aac stream
    virtual srs_error_t http_mount(ISrsRequest *r) = 0;
    virtual void http_unmount(ISrsRequest *r) = 0;
    virtual ISrsHttpServeMux *mux() = 0;
};

// The HTTP Live Streaming Server, to serve FLV/TS/MP3/AAC stream.
// TODO: Support multiple stream.
class SrsHttpStreamServer : public ISrsHttpStreamServer
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsHttpServeMux *mux_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAsyncCallWorker *async_;

public:
    // The http live streaming template, to create streams.
    std::map<std::string, SrsLiveEntry *> templateHandlers_;
    // The http live streaming streams, created by template.
    std::map<std::string, SrsLiveEntry *> streamHandlers_;

public:
    SrsHttpStreamServer();
    void assemble();
    virtual ~SrsHttpStreamServer();

public:
    virtual srs_error_t initialize();
    virtual ISrsHttpServeMux *mux();

public:
    // HTTP flv/ts/mp3/aac stream
    virtual srs_error_t http_mount(ISrsRequest *r);
    virtual void http_unmount(ISrsRequest *r);

    // Interface ISrsHttpDynamicMatcher
public:
    virtual srs_error_t dynamic_match(ISrsHttpMessage *request, ISrsHttpHandler **ph);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t initialize_flv_streaming();
    virtual srs_error_t initialize_flv_entry(std::string vhost);
};

class SrsHttpStreamDestroy : public ISrsAsyncCallTask
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::string sid_;
    std::map<std::string, SrsLiveEntry *> *streamHandlers_;
    ISrsHttpServeMux *mux_;

public:
    SrsHttpStreamDestroy(ISrsHttpServeMux *mux, std::map<std::string, SrsLiveEntry *> *handlers, std::string sid);
    virtual ~SrsHttpStreamDestroy();

public:
    virtual srs_error_t call();
    virtual std::string to_string();
};

#endif
