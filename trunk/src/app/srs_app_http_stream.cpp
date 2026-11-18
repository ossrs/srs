//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_http_stream.hpp>

#define SRS_STREAM_CACHE_CYCLE (30 * SRS_UTIME_SECONDS)

#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <sstream>
using namespace std;

#include <srs_app_async_call.hpp>
#include <srs_app_config.hpp>
#include <srs_app_factory.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_app_recv_thread.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_server.hpp>
#include <srs_app_st.hpp>
#include <srs_app_statistic.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_aac.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_mp3.hpp>
#include <srs_kernel_pithy_print.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_rtmp_msg_array.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_stream.hpp>
#include <srs_protocol_utility.hpp>

ISrsBufferCache::ISrsBufferCache()
{
}

ISrsBufferCache::~ISrsBufferCache()
{
}

SrsBufferCache::SrsBufferCache(ISrsRequest *r)
{
    req_ = r->copy()->as_http();
    queue_ = new SrsMessageQueue(true);
    trd_ = NULL;
    fast_cache_ = 0;

    config_ = _srs_config;
    live_sources_ = _srs_sources;
    app_factory_ = _srs_app_factory;
}

void SrsBufferCache::assemble()
{
    // No context id, so the coroutine generates a new one when it starts.
    trd_ = app_factory_->create_coroutine("http-stream", this, SrsContextId());
}

SrsBufferCache::~SrsBufferCache()
{
    srs_freep(trd_);

    srs_freep(queue_);
    srs_freep(req_);

    config_ = NULL;
    live_sources_ = NULL;
    app_factory_ = NULL;
}

srs_error_t SrsBufferCache::update_auth(ISrsRequest *r)
{
    srs_freep(req_);
    req_ = r->copy();

    return srs_success;
}

srs_error_t SrsBufferCache::start()
{
    srs_error_t err = srs_success;

    fast_cache_ = config_->get_vhost_http_remux_fast_cache(req_->vhost_);

    if (fast_cache_ <= 0) {
        return err;
    }

    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "corotine");
    }

    return err;
}

void SrsBufferCache::stop()
{
    if (fast_cache_ <= 0) {
        return;
    }

    trd_->stop();
}

bool SrsBufferCache::alive()
{
    if (fast_cache_ <= 0) {
        return false;
    }

    srs_error_t err = trd_->pull();
    if (err == srs_success) {
        return true;
    }

    srs_freep(err);
    return false;
}

srs_error_t SrsBufferCache::dump_cache(ISrsLiveConsumer *consumer, SrsRtmpJitterAlgorithm jitter)
{
    srs_error_t err = srs_success;

    if (fast_cache_ <= 0) {
        return err;
    }

    // The jitter is the vhost's time_jitter, which SrsLiveSource reads.
    if ((err = queue_->dump_packets(consumer, false, jitter)) != srs_success) {
        return srs_error_wrap(err, "dump packets");
    }

    srs_trace("http: dump cache %d msgs, duration=%dms, cache=%dms",
              queue_->size(), srsu2msi(queue_->duration()), srsu2msi(fast_cache_));

    return err;
}

srs_error_t SrsBufferCache::cycle()
{
    srs_error_t err = srs_success;

    SrsSharedPtr<SrsLiveSource> live_source;
    if ((err = live_sources_->fetch_or_create(req_, live_source)) != srs_success) {
        return srs_error_wrap(err, "source create");
    }
    srs_assert(live_source.get() != NULL);

    // On an edge, creating the consumer starts pulling the stream from the origin.
    SrsLiveConsumer *consumer_raw = NULL;
    if ((err = live_source->create_consumer(consumer_raw)) != srs_success) {
        return srs_error_wrap(err, "create consumer");
    }
    SrsUniquePtr<SrsLiveConsumer> consumer(consumer_raw);

    if ((err = live_source->consumer_dumps(consumer.get(), false, false, true)) != srs_success) {
        return srs_error_wrap(err, "dumps consumer");
    }

    SrsUniquePtr<SrsPithyPrint> pprint(SrsPithyPrint::create_http_stream_cache());

    SrsMessageArray msgs(SRS_PERF_MW_MSGS);

    // TODO: FIXME: support reload.
    queue_->set_queue_size(fast_cache_);

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "buffer cache");
        }

        pprint->elapse();

        int count = 0;
        if ((err = consumer->dump_packets(&msgs, count)) != srs_success) {
            return srs_error_wrap(err, "consumer dump packets");
        }

        if (count <= 0) {
            srs_info("http: sleep %dms for no msg", srsu2msi(SRS_CONSTS_RTMP_PULSE));
            srs_usleep(SRS_CONSTS_RTMP_PULSE);

            continue;
        }

        if (pprint->can_print()) {
            srs_trace("-> " SRS_CONSTS_LOG_HTTP_STREAM_CACHE " http: got %d msgs, age=%d, min=%d, mw=%d",
                      count, pprint->age(), SRS_PERF_MW_MIN_MSGS, srsu2msi(SRS_CONSTS_RTMP_PULSE));
        }

        for (int i = 0; i < count; i++) {
            SrsMediaPacket *msg = msgs.msgs_[i];
            queue_->enqueue(msg);
        }
    }

    return err;
}

ISrsBufferEncoder::ISrsBufferEncoder()
{
}

ISrsBufferEncoder::~ISrsBufferEncoder()
{
}

SrsTsStreamEncoder::SrsTsStreamEncoder()
{
    enc_ = new SrsTsTransmuxer();
}

SrsTsStreamEncoder::~SrsTsStreamEncoder()
{
    srs_freep(enc_);
}

srs_error_t SrsTsStreamEncoder::initialize(SrsFileWriter *w, ISrsBufferCache * /*c*/)
{
    srs_error_t err = srs_success;

    if ((err = enc_->initialize(w)) != srs_success) {
        return srs_error_wrap(err, "init encoder");
    }

    return err;
}

srs_error_t SrsTsStreamEncoder::write_audio(int64_t timestamp, char *data, int size)
{
    srs_error_t err = srs_success;

    if ((err = enc_->write_audio(timestamp, data, size)) != srs_success) {
        return srs_error_wrap(err, "write audio");
    }

    return err;
}

srs_error_t SrsTsStreamEncoder::write_video(int64_t timestamp, char *data, int size)
{
    srs_error_t err = srs_success;

    if ((err = enc_->write_video(timestamp, data, size)) != srs_success) {
        return srs_error_wrap(err, "write video");
    }

    return err;
}

srs_error_t SrsTsStreamEncoder::write_metadata(int64_t /*timestamp*/, char * /*data*/, int /*size*/)
{
    return srs_success;
}

bool SrsTsStreamEncoder::has_cache()
{
    return false;
}

srs_error_t SrsTsStreamEncoder::dump_cache(ISrsLiveConsumer * /*consumer*/, SrsRtmpJitterAlgorithm /*jitter*/)
{
    return srs_success;
}

void SrsTsStreamEncoder::set_has_audio(bool v)
{
    enc_->set_has_audio(v);
}

void SrsTsStreamEncoder::set_has_video(bool v)
{
    enc_->set_has_video(v);
}

void SrsTsStreamEncoder::set_guess_has_av(bool v)
{
    enc_->set_guess_has_av(v);
}

SrsFlvStreamEncoder::SrsFlvStreamEncoder()
{
    header_written_ = false;
    enc_ = new SrsFlvTransmuxer();
    has_audio_ = true;
    has_video_ = true;
    guess_has_av_ = true;
}

SrsFlvStreamEncoder::~SrsFlvStreamEncoder()
{
    srs_freep(enc_);
}

srs_error_t SrsFlvStreamEncoder::initialize(SrsFileWriter *w, ISrsBufferCache * /*c*/)
{
    srs_error_t err = srs_success;

    if ((err = enc_->initialize(w)) != srs_success) {
        return srs_error_wrap(err, "init encoder");
    }

    return err;
}

srs_error_t SrsFlvStreamEncoder::write_audio(int64_t timestamp, char *data, int size)
{
    srs_error_t err = srs_success;

    if ((err = write_header(has_video_, has_audio_)) != srs_success) {
        return srs_error_wrap(err, "write header");
    }

    return enc_->write_audio(timestamp, data, size);
}

srs_error_t SrsFlvStreamEncoder::write_video(int64_t timestamp, char *data, int size)
{
    srs_error_t err = srs_success;

    if ((err = write_header(has_video_, has_audio_)) != srs_success) {
        return srs_error_wrap(err, "write header");
    }

    return enc_->write_video(timestamp, data, size);
}

srs_error_t SrsFlvStreamEncoder::write_metadata(int64_t timestamp, char *data, int size)
{
    srs_error_t err = srs_success;

    if ((err = write_header(has_video_, has_audio_)) != srs_success) {
        return srs_error_wrap(err, "write header");
    }

    return enc_->write_metadata(SrsFrameTypeScript, data, size);
}

void SrsFlvStreamEncoder::set_drop_if_not_match(bool v)
{
    enc_->set_drop_if_not_match(v);
}

void SrsFlvStreamEncoder::set_has_audio(bool v)
{
    has_audio_ = v;
}

void SrsFlvStreamEncoder::set_has_video(bool v)
{
    has_video_ = v;
}

void SrsFlvStreamEncoder::set_guess_has_av(bool v)
{
    guess_has_av_ = v;
}

bool SrsFlvStreamEncoder::has_cache()
{
    return false;
}

srs_error_t SrsFlvStreamEncoder::dump_cache(ISrsLiveConsumer * /*consumer*/, SrsRtmpJitterAlgorithm /*jitter*/)
{
    return srs_success;
}

srs_error_t SrsFlvStreamEncoder::write_tags(SrsMediaPacket **msgs, int count)
{
    srs_error_t err = srs_success;

    if (count <= 0)
        return err;

    // For https://github.com/ossrs/srs/issues/939
    if (!header_written_) {
        bool has_video = has_video_;
        bool has_audio = has_audio_;

        // See https://github.com/ossrs/srs/issues/939#issuecomment-1351385460
        if (guess_has_av_) {
            int nn_video_frames = 0;
            int nn_audio_frames = 0;
            has_audio = has_video = false;

            for (int i = 0; i < count; i++) {
                SrsMediaPacket *msg = msgs[i];
                if (msg->is_video()) {
                    if (!SrsFlvVideo::sh(msg->payload(), msg->size()))
                        nn_video_frames++;
                    has_video = true;
                } else if (msg->is_audio()) {
                    if (!SrsFlvAudio::sh(msg->payload(), msg->size()))
                        nn_audio_frames++;
                    has_audio = true;
                }
            }

            // See https://github.com/ossrs/srs/issues/939#issuecomment-1348541733
            if (nn_video_frames > 0 && nn_audio_frames == 0) {
                if (has_audio)
                    srs_trace("FLV: Reset has_audio for videos=%d and audios=%d", nn_video_frames, nn_audio_frames);
                has_audio = false;
            }
            if (nn_audio_frames > 0 && nn_video_frames == 0) {
                if (has_video)
                    srs_trace("FLV: Reset has_video for videos=%d and audios=%d", nn_video_frames, nn_audio_frames);
                has_video = false;
            }
        }

        if (!has_video && !has_audio) {
            return err;
        }

        if ((err = write_header(has_video, has_audio)) != srs_success) {
            return srs_error_wrap(err, "write header");
        }
    }

    return enc_->write_tags(msgs, count);
}

srs_error_t SrsFlvStreamEncoder::write_header(bool has_video, bool has_audio)
{
    srs_error_t err = srs_success;

    if (!header_written_) {
        header_written_ = true;

        if ((err = enc_->write_header(has_video, has_audio)) != srs_success) {
            return srs_error_wrap(err, "write header");
        }

        srs_trace("FLV: write header audio=%d, video=%d, dinm=%d, config=%d/%d/%d", has_audio, has_video,
                  enc_->drop_if_not_match(), has_audio_, has_video_, guess_has_av_);
    }

    return err;
}

SrsAacStreamEncoder::SrsAacStreamEncoder()
{
    enc_ = new SrsAacTransmuxer();
    cache_ = NULL;
}

SrsAacStreamEncoder::~SrsAacStreamEncoder()
{
    srs_freep(enc_);
}

srs_error_t SrsAacStreamEncoder::initialize(SrsFileWriter *w, ISrsBufferCache *c)
{
    srs_error_t err = srs_success;

    cache_ = c;

    if ((err = enc_->initialize(w)) != srs_success) {
        return srs_error_wrap(err, "init encoder");
    }

    return err;
}

srs_error_t SrsAacStreamEncoder::write_audio(int64_t timestamp, char *data, int size)
{
    return enc_->write_audio(timestamp, data, size);
}

srs_error_t SrsAacStreamEncoder::write_video(int64_t /*timestamp*/, char * /*data*/, int /*size*/)
{
    return srs_success;
}

srs_error_t SrsAacStreamEncoder::write_metadata(int64_t /*timestamp*/, char * /*data*/, int /*size*/)
{
    return srs_success;
}

bool SrsAacStreamEncoder::has_cache()
{
    return true;
}

srs_error_t SrsAacStreamEncoder::dump_cache(ISrsLiveConsumer *consumer, SrsRtmpJitterAlgorithm jitter)
{
    srs_assert(cache_);
    return cache_->dump_cache(consumer, jitter);
}

SrsMp3StreamEncoder::SrsMp3StreamEncoder()
{
    enc_ = new SrsMp3Transmuxer();
    cache_ = NULL;
}

SrsMp3StreamEncoder::~SrsMp3StreamEncoder()
{
    srs_freep(enc_);
}

srs_error_t SrsMp3StreamEncoder::initialize(SrsFileWriter *w, ISrsBufferCache *c)
{
    srs_error_t err = srs_success;

    cache_ = c;

    if ((err = enc_->initialize(w)) != srs_success) {
        return srs_error_wrap(err, "init encoder");
    }

    if ((err = enc_->write_header()) != srs_success) {
        return srs_error_wrap(err, "init encoder");
    }

    return err;
}

srs_error_t SrsMp3StreamEncoder::write_audio(int64_t timestamp, char *data, int size)
{
    return enc_->write_audio(timestamp, data, size);
}

srs_error_t SrsMp3StreamEncoder::write_video(int64_t /*timestamp*/, char * /*data*/, int /*size*/)
{
    return srs_success;
}

srs_error_t SrsMp3StreamEncoder::write_metadata(int64_t /*timestamp*/, char * /*data*/, int /*size*/)
{
    return srs_success;
}

bool SrsMp3StreamEncoder::has_cache()
{
    return true;
}

srs_error_t SrsMp3StreamEncoder::dump_cache(ISrsLiveConsumer *consumer, SrsRtmpJitterAlgorithm jitter)
{
    srs_assert(cache_);
    return cache_->dump_cache(consumer, jitter);
}

SrsBufferWriter::SrsBufferWriter(ISrsHttpResponseWriter *w)
{
    writer_ = w;
}

SrsBufferWriter::~SrsBufferWriter()
{
}

srs_error_t SrsBufferWriter::open(std::string /*file*/)
{
    return srs_success;
}

void SrsBufferWriter::close()
{
}

bool SrsBufferWriter::is_open()
{
    return true;
}

int64_t SrsBufferWriter::tellg()
{
    return 0;
}

srs_error_t SrsBufferWriter::write(void *buf, size_t count, ssize_t *pnwrite)
{
    if (pnwrite) {
        *pnwrite = count;
    }
    return writer_->write((char *)buf, (int)count);
}

srs_error_t SrsBufferWriter::writev(const iovec *iov, int iovcnt, ssize_t *pnwrite)
{
    return writer_->writev(iov, iovcnt, pnwrite);
}

ISrsLiveStream::ISrsLiveStream()
{
}

ISrsLiveStream::~ISrsLiveStream()
{
}

SrsLiveStream::SrsLiveStream(ISrsRequest *r, ISrsBufferCache *c)
{
    cache_ = c;
    req_ = r->copy()->as_http();
    security_ = new SrsEnhancedSecurity();

    config_ = _srs_config;
    live_sources_ = _srs_sources;
    stat_ = _srs_stat;
    hooks_ = _srs_hooks;
}

SrsLiveStream::~SrsLiveStream()
{
    srs_freep(req_);
    srs_freep(security_);

    srs_assert(viewers_.empty());

    config_ = NULL;
    live_sources_ = NULL;
    stat_ = NULL;
    hooks_ = NULL;
}

srs_error_t SrsLiveStream::update_auth(ISrsRequest *r)
{
    srs_freep(req_);
    req_ = r->copy()->as_http();

    return srs_success;
}

srs_error_t SrsLiveStream::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    srs_error_t err = srs_success;

    SrsHttpMessage *hr = dynamic_cast<SrsHttpMessage *>(r);
    SrsHttpConn *hc = dynamic_cast<SrsHttpConn *>(hr->connection());

    // Refuse a viewer during the teardown before it counts as alive: the teardown expires only the viewers it
    // finds, and one that waits for its on_play hook would keep the stream alive until the teardown aborts.
    if (!entry_->enabled) {
        err = srs_error_new(ERROR_RTMP_STREAM_NOT_FOUND, "stream not found");
        srs_http_stream_serve_error(w, err);
        return err;
    }

    viewers_.push_back(hc);

    err = serve_http_impl(w, r);

    vector<ISrsExpire *>::iterator it = std::find(viewers_.begin(), viewers_.end(), hc);
    srs_assert(it != viewers_.end());
    viewers_.erase(it);

    // This is the root of HTTP streaming, so it owns telling the viewer why it was refused. Never
    // respond in the branch that raised the error, see srs_http_stream_serve_error.
    srs_http_stream_serve_error(w, err);

    return err;
}

srs_error_t SrsLiveStream::serve_http_impl(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    srs_error_t err = srs_success;

    SrsHttpMessage *hr = dynamic_cast<SrsHttpMessage *>(r);
    SrsHttpConn *hc = dynamic_cast<SrsHttpConn *>(hr->connection());
    SrsHttpxConn *hxc = dynamic_cast<SrsHttpxConn *>(hc->handler());

    // Each HTTP streaming connection is a session of its own, so it has its own statistics.
    srs_assert(hxc);
    hxc->set_enable_stat(true);

    SrsUniquePtr<ISrsRequest> req(req_->copy()->as_http());

    SrsPath path;
    req->stream_ = path.filepath_base(r->path());
    req->stream_ = path.filepath_filename(req->stream_);

    // The copy has the publisher's URLs, so set the player's own: the URL of its app on the HTTP
    // server it asked, and its Referer as the pageUrl, as its on_play reports. An HTTP player has
    // no swfUrl.
    req->tcUrl_ = hr->app_url(req->app_);
    req->pageUrl_ = hr->header()->get("Referer");
    req->swfUrl_ = "";

    // Update client IP, preferring the original address forwarded by a proxy.
    req->ip_ = hc->remote_ip();
    string original_ip = srs_get_original_ip(r);
    if (!original_ip.empty()) {
        req->ip_ = original_ip;
    }

    // The player's type names the format of its mount, as HLS and HTTP-FLV players have their own.
    SrsRtmpConnType type = SrsFlvPlay;
    if (srs_strings_ends_with(entry_->pattern, ".ts")) {
        type = SrsTsPlay;
    } else if (srs_strings_ends_with(entry_->pattern, ".mp3")) {
        type = SrsMp3Play;
    } else if (srs_strings_ends_with(entry_->pattern, ".aac")) {
        type = SrsAacPlay;
    }

    // The hooks read the statistic, so add the client first.
    if ((err = stat_->on_client(_srs_context->get_id().c_str(), req.get(), hc, type)) != srs_success) {
        return srs_error_wrap(err, "stat on client");
    }

    if ((err = security_->check(type, req->ip_, req.get())) != srs_success) {
        return srs_error_wrap(err, "flv: security check");
    }

    // ERROR_SYSTEM_AUTH makes the root error handler in serve_http() answer 401 rather than 500,
    // since a refused viewer is not a server fault.
    if ((err = http_hooks_on_play(r, type)) != srs_success) {
        return srs_error_transform(ERROR_SYSTEM_AUTH, err, "http hook");
    }

    if (!entry_->enabled) {
        return srs_error_new(ERROR_RTMP_STREAM_NOT_FOUND, "stream not found");
    }

    // The HTTP handler does not create the source.
    SrsSharedPtr<SrsLiveSource> live_source;
    if ((err = live_sources_->fetch_or_create(req.get(), live_source)) != srs_success) {
        return srs_error_wrap(err, "source create");
    }
    srs_assert(live_source.get() != NULL);

    bool enabled_cache = config_->get_gop_cache(req->vhost_);
    int gcmf = config_->get_gop_cache_max_frames(req->vhost_);
    live_source->set_cache(enabled_cache);
    live_source->set_gop_cache_max_frames(gcmf);

    SrsLiveConsumer *consumer_raw = NULL;
    if ((err = live_source->create_consumer(consumer_raw)) != srs_success) {
        return srs_error_wrap(err, "create consumer");
    }
    // On an edge, freeing the last consumer stops the ingest, which unpublishes the source and
    // unmounts this stream.
    SrsUniquePtr<SrsLiveConsumer> consumer(consumer_raw);

    if (!entry_->enabled) {
        return srs_error_new(ERROR_RTMP_STREAM_NOT_FOUND, "stream not found");
    }

    err = do_serve_http(live_source.get(), consumer.get(), w, r);

    http_hooks_on_stop(r, type);

    return err;
}

bool SrsLiveStream::alive()
{
    return !viewers_.empty();
}

void SrsLiveStream::expire()
{
    vector<ISrsExpire *>::iterator it;
    for (it = viewers_.begin(); it != viewers_.end(); ++it) {
        ISrsExpire *conn = *it;
        conn->expire();
    }
}

srs_error_t SrsLiveStream::do_serve_http(SrsLiveSource *source, ISrsLiveConsumer *consumer, ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    srs_error_t err = srs_success;

    string enc_desc;
    ISrsBufferEncoder *enc_raw = NULL;

    srs_assert(entry_);
    bool drop_if_not_match = config_->get_vhost_http_remux_drop_if_not_match(req_->vhost_);
    bool has_audio = config_->get_vhost_http_remux_has_audio(req_->vhost_);
    bool has_video = config_->get_vhost_http_remux_has_video(req_->vhost_);
    bool guess_has_av = config_->get_vhost_http_remux_guess_has_av(req_->vhost_);

    if (srs_strings_ends_with(entry_->pattern, ".flv")) {
        w->header()->set_content_type("video/x-flv");
        enc_desc = "FLV";
        enc_raw = new SrsFlvStreamEncoder();
        ((SrsFlvStreamEncoder *)enc_raw)->set_drop_if_not_match(drop_if_not_match);
        ((SrsFlvStreamEncoder *)enc_raw)->set_has_audio(has_audio);
        ((SrsFlvStreamEncoder *)enc_raw)->set_has_video(has_video);
        ((SrsFlvStreamEncoder *)enc_raw)->set_guess_has_av(guess_has_av);
    } else if (srs_strings_ends_with(entry_->pattern, ".aac")) {
        w->header()->set_content_type("audio/x-aac");
        enc_desc = "AAC";
        enc_raw = new SrsAacStreamEncoder();
    } else if (srs_strings_ends_with(entry_->pattern, ".mp3")) {
        w->header()->set_content_type("audio/mpeg");
        enc_desc = "MP3";
        enc_raw = new SrsMp3StreamEncoder();
    } else if (srs_strings_ends_with(entry_->pattern, ".ts")) {
        w->header()->set_content_type("video/MP2T");
        enc_desc = "TS";
        enc_raw = new SrsTsStreamEncoder();
        ((SrsTsStreamEncoder *)enc_raw)->set_has_audio(has_audio);
        ((SrsTsStreamEncoder *)enc_raw)->set_has_video(has_video);
        ((SrsTsStreamEncoder *)enc_raw)->set_guess_has_av(guess_has_av);
    } else {
        return srs_error_new(ERROR_HTTP_LIVE_STREAM_EXT, "invalid pattern=%s", entry_->pattern.c_str());
    }
    SrsUniquePtr<ISrsBufferEncoder> enc(enc_raw);

    // No Content-Length is set, so the response is chunked.
    w->write_header(SRS_CONSTS_HTTP_OK);

    if ((err = source->consumer_dumps(consumer, true, true, !enc->has_cache())) != srs_success) {
        return srs_error_wrap(err, "dumps consumer");
    }

    SrsUniquePtr<SrsPithyPrint> pprint(SrsPithyPrint::create_http_stream());

    SrsMessageArray msgs(SRS_PERF_MW_MSGS);

    SrsHttpMessage *hr = dynamic_cast<SrsHttpMessage *>(r);
    SrsHttpConn *hc = dynamic_cast<SrsHttpConn *>(hr->connection());

    SrsBufferWriter writer(w);
    if ((err = enc->initialize(&writer, cache_)) != srs_success) {
        return srs_error_wrap(err, "init encoder");
    }

    if (enc->has_cache()) {
        if ((err = enc->dump_cache(consumer, source->jitter())) != srs_success) {
            return srs_error_wrap(err, "encoder dump cache");
        }
    }

    SrsFlvStreamEncoder *ffe = dynamic_cast<SrsFlvStreamEncoder *>(enc.get());

    SrsHttpxConn *hxc = dynamic_cast<SrsHttpxConn *>(hc->handler());
    srs_assert(hxc);

    // A receive thread reads and drops the client's bytes, to see the close event and avoid an FD leak.
    // @see https://github.com/ossrs/srs/issues/636#issuecomment-298208427
    SrsUniquePtr<SrsHttpRecvThread> trd(new SrsHttpRecvThread(hxc));
    trd->assemble();

    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "start recv thread");
    }

    srs_utime_t mw_sleep = config_->get_mw_sleep(req_->vhost_);

    // Without a wait, HTTP-FLV spins.
    // See https://github.com/ossrs/srs/issues/3963 for more details.
    if (mw_sleep == 0) {
        mw_sleep = 10 * SRS_UTIME_MILLISECONDS;
    }

    srs_trace("FLV %s, encoder=%s, mw_sleep=%dms, cache=%d, msgs=%d, dinm=%d, guess_av=%d/%d/%d",
              entry_->pattern.c_str(), enc_desc.c_str(), srsu2msi(mw_sleep), enc->has_cache(), msgs.max_, drop_if_not_match,
              has_audio, has_video, guess_has_av);

    // TODO: free and erase the disabled entry after all related connections is closed.
    // TODO: FIXME: Support timeout for player, quit infinite-loop.
    while (entry_->enabled) {
        // Whether the connection is interrupted, such as a DELETE by the HTTP API. The recv thread
        // checks it only at its 3s read timeout.
        if ((err = hc->pull()) != srs_success) {
            return srs_error_wrap(err, "http conn");
        }

        // Whether client closed the FD.
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "recv thread");
        }

        pprint->elapse();

        int count = 0;
        if ((err = consumer->dump_packets(&msgs, count)) != srs_success) {
            return srs_error_wrap(err, "consumer dump packets");
        }

        // TODO: FIXME: Support merged-write wait.
        if (count <= 0) {
            // Sleep, not consumer wait, because we could not wake the consumer.
            srs_usleep(mw_sleep);
            continue;
        }

        if (pprint->can_print()) {
            srs_trace("-> " SRS_CONSTS_LOG_HTTP_STREAM " http: got %d msgs, age=%d, min=%d, mw=%d",
                      count, pprint->age(), SRS_PERF_MW_MIN_MSGS, srsu2msi(mw_sleep));
        }

        if (ffe) {
            err = ffe->write_tags(msgs.msgs_, count);
        } else {
            err = streaming_send_messages(enc.get(), msgs.msgs_, count);
        }

        // TODO: FIXME: Update the stat.

        for (int i = 0; i < count; i++) {
            SrsMediaPacket *msg = msgs.msgs_[i];
            srs_freep(msg);
        }

        if (err != srs_success) {
            return srs_error_wrap(err, "send messages");
        }
    }

    // The entry is disabled by an unpublish or a reload. Without the error, the client never quits.
    return srs_error_new(ERROR_HTTP_STREAM_EOF, "Stream EOF");
}

srs_error_t SrsLiveStream::http_hooks_on_play(ISrsHttpMessage *r, SrsRtmpConnType type)
{
    srs_error_t err = srs_success;

    if (!config_->get_vhost_http_hooks_enabled(req_->vhost_)) {
        return err;
    }

    SrsHttpMessage *hr = dynamic_cast<SrsHttpMessage *>(r);
    SrsUniquePtr<ISrsRequest> nreq(hr->to_request(req_->vhost_));
    // The player's own tcUrl, the URL of its app on the HTTP server it asked, as the API lists it.
    nreq->tcUrl_ = hr->app_url(nreq->app_);

    // The hooks switch coroutines, during which the directive may be freed, so copy its URLs first.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;

    if (true) {
        SrsConfDirective *conf = config_->get_vhost_on_play(nreq->vhost_);

        if (!conf) {
            return err;
        }

        hooks = conf->args_;
    }

    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        if ((err = hooks_->on_play(url, nreq.get(), type)) != srs_success) {
            return srs_error_wrap(err, "http on_play %s", url.c_str());
        }
    }

    return err;
}

void SrsLiveStream::http_hooks_on_stop(ISrsHttpMessage *r, SrsRtmpConnType type)
{
    if (!config_->get_vhost_http_hooks_enabled(req_->vhost_)) {
        return;
    }

    SrsHttpMessage *hr = dynamic_cast<SrsHttpMessage *>(r);
    SrsUniquePtr<ISrsRequest> nreq(hr->to_request(req_->vhost_));
    // The player's own tcUrl, the URL of its app on the HTTP server it asked, as the API lists it.
    nreq->tcUrl_ = hr->app_url(nreq->app_);

    // The hooks switch coroutines, during which the directive may be freed, so copy its URLs first.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;

    if (true) {
        SrsConfDirective *conf = config_->get_vhost_on_stop(nreq->vhost_);

        if (!conf) {
            srs_info("ignore the empty http callback: on_stop");
            return;
        }

        hooks = conf->args_;
    }

    SrsStatisticStreamSnapshot stream(stat_, nreq.get());
    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        hooks_->on_stop(url, nreq.get(), type, stream);
    }

    return;
}

srs_error_t SrsLiveStream::streaming_send_messages(ISrsBufferEncoder *enc, SrsMediaPacket **msgs, int nb_msgs)
{
    srs_error_t err = srs_success;

    // TODO: In gop cache, we know both the audio and video codec, so we should notice the encoder, which might depends
    // on setting the correct codec information, for example, HTTP-TS or HLS will write PMT.
    for (int i = 0; i < nb_msgs; i++) {
        SrsMediaPacket *msg = msgs[i];

        if (msg->is_audio()) {
            err = enc->write_audio(msg->timestamp_, msg->payload(), msg->size());
        } else if (msg->is_video()) {
            err = enc->write_video(msg->timestamp_, msg->payload(), msg->size());
        } else {
            err = enc->write_metadata(msg->timestamp_, msg->payload(), msg->size());
        }

        if (err != srs_success) {
            return srs_error_wrap(err, "send messages");
        }
    }

    return err;
}

SrsLiveEntry::SrsLiveEntry(std::string m)
{
    mount_ = m;
    disposing_ = false;

    stream_ = NULL;
    cache_ = NULL;

    req_ = NULL;

    SrsPath path;
    std::string ext = path.filepath_ext(m);
    is_flv_ = (ext == ".flv");
    is_ts_ = (ext == ".ts");
    is_mp3_ = (ext == ".mp3");
    is_aac_ = (ext == ".aac");
}

SrsLiveEntry::~SrsLiveEntry()
{
    srs_freep(req_);
}

bool SrsLiveEntry::is_flv()
{
    return is_flv_;
}

bool SrsLiveEntry::is_ts()
{
    return is_ts_;
}

bool SrsLiveEntry::is_aac()
{
    return is_aac_;
}

bool SrsLiveEntry::is_mp3()
{
    return is_mp3_;
}

ISrsHttpStreamServer::ISrsHttpStreamServer()
{
}

ISrsHttpStreamServer::~ISrsHttpStreamServer()
{
}

SrsHttpStreamServer::SrsHttpStreamServer()
{
    mux_ = new SrsHttpServeMux();

    config_ = _srs_config;
}

void SrsHttpStreamServer::assemble()
{
    SrsHttpServeMux *mux = dynamic_cast<SrsHttpServeMux *>(mux_);
    if (mux) {
        mux->add_dynamic_matcher(this);
    }
}

SrsHttpStreamServer::~SrsHttpStreamServer()
{
    SrsHttpServeMux *mux = dynamic_cast<SrsHttpServeMux *>(mux_);
    if (mux) {
        mux->remove_dynamic_matcher(this);
    }

    // Wait for every teardown to end, for they use the stream handlers.
    for (int i = 0; i < (int)destroyers_.size(); i++) {
        SrsHttpStreamDestroy *destroyer = destroyers_.at(i);
        srs_freep(destroyer);
    }
    destroyers_.clear();

    if (true) {
        std::map<std::string, SrsLiveEntry *>::iterator it;
        for (it = templateHandlers_.begin(); it != templateHandlers_.end(); ++it) {
            SrsLiveEntry *entry = it->second;
            srs_freep(entry);
        }
        templateHandlers_.clear();
    }
    if (true) {
        std::map<std::string, SrsLiveEntry *>::iterator it;
        for (it = streamHandlers_.begin(); it != streamHandlers_.end(); ++it) {
            SrsLiveEntry *entry = it->second;
            srs_freep(entry);
        }
        streamHandlers_.clear();
    }

    config_ = NULL;
}

srs_error_t SrsHttpStreamServer::initialize()
{
    srs_error_t err = srs_success;

    if ((err = initialize_flv_streaming()) != srs_success) {
        return srs_error_wrap(err, "http flv stream");
    }

    return err;
}

ISrsHttpServeMux *SrsHttpStreamServer::mux()
{
    return mux_;
}

// TODO: FIXME: rename for HTTP FLV mount.
srs_error_t SrsHttpStreamServer::http_mount(ISrsRequest *r)
{
    srs_error_t err = srs_success;

    std::string sid = r->get_stream_url();
    SrsLiveEntry *entry = NULL;

    if (streamHandlers_.find(sid) == streamHandlers_.end()) {
        if (templateHandlers_.find(r->vhost_) == templateHandlers_.end()) {
            return err;
        }

        SrsLiveEntry *tmpl = templateHandlers_[r->vhost_];

        std::string mount = tmpl->mount_;

        mount = srs_strings_replace(mount, "[vhost]", r->vhost_);
        mount = srs_strings_replace(mount, "[app]", r->app_);
        mount = srs_strings_replace(mount, "[stream]", r->stream_);

        mount = srs_strings_replace(mount, SRS_CONSTS_RTMP_DEFAULT_VHOST "/", "/");

        entry = new SrsLiveEntry(mount);

        entry->req_ = r->copy()->as_http();
        SrsBufferCache *cache = new SrsBufferCache(r);
        cache->assemble();
        entry->cache_ = cache;
        entry->stream_ = new SrsLiveStream(r, entry->cache_);

        // TODO: FIXME: maybe refine the logic of http remux service.
        // if user push streams followed:
        //     rtmp://test.com/live/stream1
        //     rtmp://test.com/live/stream2
        // and they will using the same template, such as: [vhost]/[app]/[stream].flv
        // so, need to free last request object, otherwise, it will cause memory leak.
        srs_freep(tmpl->req_);

        tmpl->req_ = r->copy()->as_http();

        // Register the handler before the cache starts, since starting it switches coroutines.
        if ((err = mux_->handle(mount, entry->stream_)) != srs_success) {
            // Keep no entry of a stream not mounted, or its unmount removes the stream of another app at the mount.
            srs_freep(entry->stream_);
            srs_freep(entry->cache_);
            srs_freep(entry);
            return srs_error_wrap(err, "http: mount flv stream for vhost=%s failed", sid.c_str());
        }

        streamHandlers_[sid] = entry;

        if ((err = entry->cache_->start()) != srs_success) {
            return srs_error_wrap(err, "http: start stream cache failed");
        }
        srs_trace("http: mount flv stream for sid=%s, mount=%s", sid.c_str(), mount.c_str());
    } else {
        entry = streamHandlers_[sid];

        if (entry->disposing_) {
            return srs_error_new(ERROR_STREAM_DISPOSING, "stream is disposing");
        }

        entry->stream_->update_auth(r);
        entry->cache_->update_auth(r);
    }

    if (entry->stream_) {
        entry->stream_->entry_->enabled = true;
        return err;
    }

    return err;
}

void SrsHttpStreamServer::http_unmount(ISrsRequest *r)
{
    std::string sid = r->get_stream_url();

    std::map<std::string, SrsLiveEntry *>::iterator it = streamHandlers_.find(sid);
    if (it == streamHandlers_.end()) {
        return;
    }

    // A disposing entry is not reused by http_mount.
    SrsLiveEntry *entry = it->second;
    if (entry->disposing_) {
        return;
    }
    entry->disposing_ = true;

    for (int i = (int)destroyers_.size() - 1; i >= 0; i--) {
        SrsHttpStreamDestroy *destroyer = destroyers_.at(i);
        if (destroyer->done()) {
            srs_freep(destroyer);
            destroyers_.erase(destroyers_.begin() + i);
        }
    }

    // Destroy the stream on a coroutine of its own, not in the caller, which may be a viewer of
    // the stream, and not after another stream's teardown, which may wait for its viewers.
    SrsHttpStreamDestroy *destroyer = new SrsHttpStreamDestroy(mux_, &streamHandlers_, sid);
    destroyers_.push_back(destroyer);

    srs_error_t err = srs_success;
    if ((err = destroyer->start()) != srs_success) {
        srs_warn("http: ignore unmount stream failed, sid=%s, err=%s", sid.c_str(), srs_error_desc(err).c_str());
        srs_freep(err);
    }
}

srs_error_t SrsHttpStreamServer::dynamic_match(ISrsHttpMessage *request, ISrsHttpHandler **ph)
{
    srs_error_t err = srs_success;

    ISrsHttpHandler *h = *ph ? *ph : NULL;
    if (h && h->entry_ && h->entry_->pattern != "/") {
        return err;
    }

    std::string ext = request->ext();
    if (ext.empty()) {
        return err;
    }

    SrsConfDirective *vhost = config_->get_vhost(request->host());
    if (!vhost || !config_->get_vhost_enabled(vhost)) {
        return err;
    }

    SrsLiveEntry *entry = NULL;
    if (true) {
        std::map<std::string, SrsLiveEntry *>::iterator it = templateHandlers_.find(vhost->arg0());
        if (it == templateHandlers_.end()) {
            return err;
        }

        // An origin mounts a stream when it is published. An edge pulls a stream only for a player,
        // so the player's request mounts it here (the hstrs option, dropped in SRS 3 as always on).
        entry = it->second;

        if (entry->is_flv()) {
            if (ext != ".flv") {
                return err;
            }
        } else if (entry->is_ts()) {
            if (ext != ".ts") {
                return err;
            }
        } else if (entry->is_mp3()) {
            if (ext != ".mp3") {
                return err;
            }
        } else if (entry->is_aac()) {
            if (ext != ".aac") {
                return err;
            }
        } else {
            return err;
        }
    }

    // The request path must fit the mount template of the config. For "[vhost]/[app]/[stream].flv":
    //      matched for "/live/livestream.flv"
    //      matched for "ossrs.net/live/livestream.flv"
    //      not-matched for "/livestream.flv", which is actually "/__defaultApp__/livestream.flv", since HTTP has no default app.
    //      matched for "/tenant/live/livestream.flv", app is "tenant/live" because the app is like a folder path.
    // Only the [app] can span multiple segments, so a template without it must match exactly.
    string upath = request->path();
    int upath_slashes = srs_strings_count(upath, "/");
    int mount_slashes = srs_strings_count(entry->mount_, "/");
    if (upath_slashes < mount_slashes) {
        return err;
    }
    if (upath_slashes > mount_slashes && entry->mount_.find("[app]") == string::npos) {
        return err;
    }

    SrsHttpMessage *hreq = dynamic_cast<SrsHttpMessage *>(request);
    srs_assert(hreq);

    SrsUniquePtr<ISrsRequest> r(hreq->to_request(vhost->arg0()));

    std::string sid = r->get_stream_url();
    // The user may disable HTTP-FLV and reload.
    if (streamHandlers_.find(sid) != streamHandlers_.end()) {
        SrsLiveEntry *s_entry = streamHandlers_[sid];
        if (!s_entry->stream_->entry_->enabled) {
            // The config is checked only for a disabled entry, since an edge mounts the stream to start
            // its ingest even when the origin has no such stream.
            if (!config_->get_vhost_http_remux_enabled(r->vhost_)) {
                return srs_error_new(ERROR_HTTP_DYNAMIC_MATCH, "stream disabled");
            }
        }
    }

    if ((err = http_mount(r.get())) != srs_success) {
        return srs_error_wrap(err, "http mount");
    }

    if (streamHandlers_.find(sid) != streamHandlers_.end()) {
        entry = streamHandlers_[sid];
        *ph = entry->stream_;
    }

    srs_trace("flv: dynamic match %s ok", upath.c_str());

    return err;
}

srs_error_t SrsHttpStreamServer::initialize_flv_streaming()
{
    srs_error_t err = srs_success;

    SrsConfDirective *root = config_->get_root();
    for (int i = 0; i < (int)root->directives_.size(); i++) {
        SrsConfDirective *conf = root->at(i);

        if (!conf->is_vhost()) {
            continue;
        }

        if ((err = initialize_flv_entry(conf->arg0())) != srs_success) {
            return srs_error_wrap(err, "init flv entries");
        }
    }

    return err;
}

srs_error_t SrsHttpStreamServer::initialize_flv_entry(std::string vhost)
{
    srs_error_t err = srs_success;

    if (!config_->get_vhost_http_remux_enabled(vhost)) {
        return err;
    }

    SrsLiveEntry *entry = new SrsLiveEntry(config_->get_vhost_http_remux_mount(vhost));

    templateHandlers_[vhost] = entry;
    srs_trace("http flv live stream, vhost=%s, mount=%s", vhost.c_str(), entry->mount_.c_str());

    return err;
}

SrsHttpStreamDestroy::SrsHttpStreamDestroy(ISrsHttpServeMux *mux, map<std::string, SrsLiveEntry *> *handlers, string sid)
{
    mux_ = mux;
    sid_ = sid;
    streamHandlers_ = handlers;
    trd_ = new SrsDummyCoroutine();
    done_ = false;
}

SrsHttpStreamDestroy::~SrsHttpStreamDestroy()
{
    srs_freep(trd_);
}

srs_error_t SrsHttpStreamDestroy::start()
{
    srs_error_t err = srs_success;

    srs_freep(trd_);
    trd_ = new SrsSTCoroutine("http-destroy", this, _srs_context->get_id());

    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "coroutine");
    }

    return err;
}

bool SrsHttpStreamDestroy::done()
{
    return done_;
}

srs_error_t SrsHttpStreamDestroy::cycle()
{
    srs_error_t err = call();
    done_ = true;
    return err;
}

srs_error_t SrsHttpStreamDestroy::call()
{
    srs_error_t err = srs_success;

    std::map<std::string, SrsLiveEntry *>::iterator it = streamHandlers_->find(sid_);
    if (it == streamHandlers_->end()) {
        return err;
    }

    SrsUniquePtr<SrsLiveEntry> entry(it->second);
    srs_assert(entry->disposing_);

    SrsUniquePtr<ISrsLiveStream> stream(entry->stream_);
    SrsUniquePtr<ISrsBufferCache> cache(entry->cache_);

    if (stream->entry_)
        stream->entry_->enabled = false;
    stream->expire();
    cache->stop();

    int i = 0;
    for (; i < 3000; i++) {
        if (!cache->alive() && !stream->alive()) {
            break;
        }
        srs_usleep(100 * SRS_UTIME_MILLISECONDS);
    }

    if (cache->alive() || stream->alive()) {
        srs_error("http: try to free an alive stream, sid=%s, cache=%d, stream=%d",
                  sid_.c_str(), cache->alive(), stream->alive());
        srs_assert(false);
    }

    streamHandlers_->erase(it);

    // This frees the mux entry, which the stream reads, so it runs only after the cache and stream stop.
    mux_->unhandle(entry->mount_, stream.get());

    srs_trace("http: unmount flv stream for sid=%s, i=%d", sid_.c_str(), i);
    return err;
}

string SrsHttpStreamDestroy::to_string()
{
    return "destroy";
}
