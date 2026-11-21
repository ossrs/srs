//
// Copyright (c) 2013-2026 The SRS Authors
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
class ISrsAppConfig;
class ISrsLiveSourceManager;
class ISrsAppFactory;
class ISrsStatistic;
class ISrsHttpHooks;
class ISrsMessageQueue;
class ISrsLiveConsumer;
class ISrsTsTransmuxer;
class ISrsAacTransmuxer;
class ISrsBufferCache;
class ISrsMp3Transmuxer;
class ISrsCommonHttpHandler;
class SrsHttpStreamDestroy;

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

// The fast startup cache of the AAC and MP3 streams, for Android WeChat players.
class SrsBufferCache : public ISrsCoroutineHandler, public ISrsBufferCache
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsLiveSourceManager *live_sources_;
    ISrsAppFactory *app_factory_;

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
    void assemble(); // Creates the members, to avoid calling functions in the constructor.
    virtual ~SrsBufferCache();
    virtual srs_error_t update_auth(ISrsRequest *r);

public:
    virtual srs_error_t start();
    virtual void stop();
    virtual bool alive();
    virtual srs_error_t dump_cache(ISrsLiveConsumer *consumer, SrsRtmpJitterAlgorithm jitter);

public:
    virtual srs_error_t cycle();
};

class ISrsBufferEncoder
{
public:
    ISrsBufferEncoder();
    virtual ~ISrsBufferEncoder();

public:
    virtual srs_error_t initialize(SrsFileWriter *w, ISrsBufferCache *c) = 0;
    virtual srs_error_t write_audio(int64_t timestamp, char *data, int size) = 0;
    virtual srs_error_t write_video(int64_t timestamp, char *data, int size) = 0;
    virtual srs_error_t write_metadata(int64_t timestamp, char *data, int size) = 0;

public:
    // The gop cache of SrsLiveSource holds nothing for a pure audio stream, so the AAC and MP3
    // encoders use their own cache.
    virtual bool has_cache() = 0;
    virtual srs_error_t dump_cache(ISrsLiveConsumer *consumer, SrsRtmpJitterAlgorithm jitter) = 0;
};

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
    virtual srs_error_t write_tags(SrsMediaPacket **msgs, int count);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t write_header(bool has_video, bool has_audio);
};

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

class ISrsLiveStream : public ISrsHttpHandler, public ISrsExpire
{
public:
    ISrsLiveStream();
    virtual ~ISrsLiveStream();

public:
    virtual srs_error_t update_auth(ISrsRequest *r) = 0;
    virtual bool alive() = 0;
};

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

public:
    virtual void expire();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t do_serve_http(SrsLiveSource *source, ISrsLiveConsumer *consumer, ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
    virtual srs_error_t http_hooks_on_play(ISrsHttpMessage *r, SrsRtmpConnType type);
    virtual void http_hooks_on_stop(ISrsHttpMessage *r, SrsRtmpConnType type);
    virtual srs_error_t streaming_send_messages(ISrsBufferEncoder *enc, SrsMediaPacket **msgs, int nb_msgs);
};

struct SrsLiveEntry {
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    bool is_flv_;
    bool is_ts_;
    bool is_aac_;
    bool is_mp3_;

public:
    ISrsRequest *req_;

public:
    // A template's mount has variables; a stream's mount is its URL path.
    std::string mount_;

    ISrsLiveStream *stream_;
    ISrsBufferCache *cache_;

    bool disposing_;

    SrsLiveEntry(std::string m);
    virtual ~SrsLiveEntry();

    bool is_flv();
    bool is_ts();
    bool is_mp3();
    bool is_aac();
};

class ISrsHttpStreamServer : public ISrsHttpDynamicMatcher
{
public:
    ISrsHttpStreamServer();
    virtual ~ISrsHttpStreamServer();

public:
    virtual void assemble() = 0;
    virtual srs_error_t initialize() = 0;
    virtual srs_error_t http_mount(ISrsRequest *r) = 0;
    virtual void http_unmount(ISrsRequest *r) = 0;
    virtual ISrsHttpServeMux *mux() = 0;
};

// TODO: Support multiple stream.
class SrsHttpStreamServer : public ISrsHttpStreamServer
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsHttpServeMux *mux_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The teardowns of unmounted streams, each on its own coroutine, so one waiting for its
    // viewers never delays another.
    std::vector<SrsHttpStreamDestroy *> destroyers_;

public:
    std::map<std::string, SrsLiveEntry *> templateHandlers_;
    std::map<std::string, SrsLiveEntry *> streamHandlers_;

public:
    SrsHttpStreamServer();
    void assemble();
    virtual ~SrsHttpStreamServer();

public:
    virtual srs_error_t initialize();
    virtual ISrsHttpServeMux *mux();

public:
    virtual srs_error_t http_mount(ISrsRequest *r);
    virtual void http_unmount(ISrsRequest *r);

public:
    virtual srs_error_t dynamic_match(ISrsHttpMessage *request, ISrsHttpHandler **ph);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t initialize_flv_streaming();
    virtual srs_error_t initialize_flv_entry(std::string vhost);
};

class SrsHttpStreamDestroy : public ISrsAsyncCallTask, public ISrsCoroutineHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::string sid_;
    std::map<std::string, SrsLiveEntry *> *streamHandlers_;
    ISrsHttpServeMux *mux_;
    ISrsCoroutine *trd_;
    bool done_;

public:
    SrsHttpStreamDestroy(ISrsHttpServeMux *mux, std::map<std::string, SrsLiveEntry *> *handlers, std::string sid);
    // Free it after the call is done, or it waits for the call to end.
    virtual ~SrsHttpStreamDestroy();

public:
    virtual srs_error_t start();
    virtual bool done();
    virtual srs_error_t call();
    virtual std::string to_string();

public:
    virtual srs_error_t cycle();
};

#endif
