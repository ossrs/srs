//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_app_http_stream.hpp>

#define SRS_STREAM_CACHE_CYCLE (30 * SRS_UTIME_SECONDS)

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include <sstream>
using namespace std;

#include <srs_protocol_stream.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_st.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_config.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_app_source.hpp>
#include <srs_protocol_rtmp_msg_array.hpp>
#include <srs_kernel_aac.hpp>
#include <srs_kernel_mp3.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_app_source.hpp>
#include <srs_app_server.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_recv_thread.hpp>
#include <srs_app_http_hooks.hpp>

SrsBufferCache::SrsBufferCache(SrsLiveSource* s, SrsRequest* r)
{
    req = r->copy()->as_http();
    source = s;
    queue = new SrsMessageQueue(true);
    trd = new SrsSTCoroutine("http-stream", this);
    
    // TODO: FIXME: support reload.
    fast_cache = _srs_config->get_vhost_http_remux_fast_cache(req->vhost);
}

SrsBufferCache::~SrsBufferCache()
{
    srs_freep(trd);
    
    srs_freep(queue);
    srs_freep(req);
}

srs_error_t SrsBufferCache::update_auth(SrsLiveSource* s, SrsRequest* r)
{
    srs_freep(req);
    req = r->copy();
    source = s;
    
    return srs_success;
}

srs_error_t SrsBufferCache::start()
{
    srs_error_t err = srs_success;
    
    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "corotine");
    }
    
    return err;
}

srs_error_t SrsBufferCache::dump_cache(SrsLiveConsumer* consumer, SrsRtmpJitterAlgorithm jitter)
{
    srs_error_t err = srs_success;
    
    if (fast_cache <= 0) {
        return err;
    }
    
    // the jitter is get from SrsLiveSource, which means the time_jitter of vhost.
    if ((err = queue->dump_packets(consumer, false, jitter)) != srs_success) {
        return srs_error_wrap(err, "dump packets");
    }
    
    srs_trace("http: dump cache %d msgs, duration=%dms, cache=%dms",
        queue->size(), srsu2msi(queue->duration()), srsu2msi(fast_cache));
    
    return err;
}

srs_error_t SrsBufferCache::cycle()
{
    srs_error_t err = srs_success;
    
    // TODO: FIXME: support reload.
    if (fast_cache <= 0) {
        srs_usleep(SRS_STREAM_CACHE_CYCLE);
        return err;
    }
    
    // the stream cache will create consumer to cache stream,
    // which will trigger to fetch stream from origin for edge.
    SrsLiveConsumer* consumer = NULL;
    SrsAutoFree(SrsLiveConsumer, consumer);
    if ((err = source->create_consumer(consumer)) != srs_success) {
        return srs_error_wrap(err, "create consumer");
    }
    if ((err = source->consumer_dumps(consumer, false, false, true)) != srs_success) {
        return srs_error_wrap(err, "dumps consumer");
    }
    
    SrsPithyPrint* pprint = SrsPithyPrint::create_http_stream_cache();
    SrsAutoFree(SrsPithyPrint, pprint);
    
    SrsMessageArray msgs(SRS_PERF_MW_MSGS);
    
    // set the queue size, which used for max cache.
    // TODO: FIXME: support reload.
    queue->set_queue_size(fast_cache);
    
    while (true) {
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "buffer cache");
        }
        
        pprint->elapse();
        
        // get messages from consumer.
        // each msg in msgs.msgs must be free, for the SrsMessageArray never free them.
        int count = 0;
        if ((err = consumer->dump_packets(&msgs, count)) != srs_success) {
            return srs_error_wrap(err, "consumer dump packets");
        }
        
        if (count <= 0) {
            srs_info("http: sleep %dms for no msg", srsu2msi(SRS_CONSTS_RTMP_PULSE));
            // directly use sleep, donot use consumer wait.
            srs_usleep(SRS_CONSTS_RTMP_PULSE);
            
            // ignore when nothing got.
            continue;
        }
        
        if (pprint->can_print()) {
            srs_trace("-> " SRS_CONSTS_LOG_HTTP_STREAM_CACHE " http: got %d msgs, age=%d, min=%d, mw=%d",
                      count, pprint->age(), SRS_PERF_MW_MIN_MSGS, srsu2msi(SRS_CONSTS_RTMP_PULSE));
        }
        
        // free the messages.
        for (int i = 0; i < count; i++) {
            SrsSharedPtrMessage* msg = msgs.msgs[i];
            queue->enqueue(msg);
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
    enc = new SrsTsTransmuxer();
}

SrsTsStreamEncoder::~SrsTsStreamEncoder()
{
    srs_freep(enc);
}

srs_error_t SrsTsStreamEncoder::initialize(SrsFileWriter* w, SrsBufferCache* /*c*/)
{
    srs_error_t err = srs_success;
    
    if ((err = enc->initialize(w)) != srs_success) {
        return srs_error_wrap(err, "init encoder");
    }
    
    return err;
}

srs_error_t SrsTsStreamEncoder::write_audio(int64_t timestamp, char* data, int size)
{
    srs_error_t err = srs_success;
    
    if ((err = enc->write_audio(timestamp, data, size)) != srs_success) {
        return srs_error_wrap(err, "write audio");
    }
    
    return err;
}

srs_error_t SrsTsStreamEncoder::write_video(int64_t timestamp, char* data, int size)
{
    srs_error_t err = srs_success;
    
    if ((err = enc->write_video(timestamp, data, size)) != srs_success) {
        return srs_error_wrap(err, "write video");
    }
    
    return err;
}

srs_error_t SrsTsStreamEncoder::write_metadata(int64_t /*timestamp*/, char* /*data*/, int /*size*/)
{
    return srs_success;
}

bool SrsTsStreamEncoder::has_cache()
{
    // for ts stream, use gop cache of SrsLiveSource is ok.
    return false;
}

srs_error_t SrsTsStreamEncoder::dump_cache(SrsLiveConsumer* /*consumer*/, SrsRtmpJitterAlgorithm /*jitter*/)
{
    // for ts stream, ignore cache.
    return srs_success;
}

void SrsTsStreamEncoder::set_has_audio(bool v)
{
    enc->set_has_audio(v);
}
void SrsTsStreamEncoder::set_has_video(bool v)
{
    enc->set_has_video(v);
}

SrsFlvStreamEncoder::SrsFlvStreamEncoder()
{
    header_written = false;
    enc = new SrsFlvTransmuxer();
    has_audio_ = true;
    has_video_ = true;
    guess_has_av_ = true;
}

SrsFlvStreamEncoder::~SrsFlvStreamEncoder()
{
    srs_freep(enc);
}

srs_error_t SrsFlvStreamEncoder::initialize(SrsFileWriter* w, SrsBufferCache* /*c*/)
{
    srs_error_t err = srs_success;
    
    if ((err = enc->initialize(w)) != srs_success) {
        return srs_error_wrap(err, "init encoder");
    }
    
    return err;
}

srs_error_t SrsFlvStreamEncoder::write_audio(int64_t timestamp, char* data, int size)
{
    srs_error_t err = srs_success;

    if ((err = write_header(has_video_, has_audio_))  != srs_success) {
        return srs_error_wrap(err, "write header");
    }

    return enc->write_audio(timestamp, data, size);
}

srs_error_t SrsFlvStreamEncoder::write_video(int64_t timestamp, char* data, int size)
{
    srs_error_t err = srs_success;

    if ((err = write_header(has_video_, has_audio_))  != srs_success) {
        return srs_error_wrap(err, "write header");
    }

    return enc->write_video(timestamp, data, size);
}

srs_error_t SrsFlvStreamEncoder::write_metadata(int64_t timestamp, char* data, int size)
{
    srs_error_t err = srs_success;

    if ((err = write_header(has_video_, has_audio_))  != srs_success) {
        return srs_error_wrap(err, "write header");
    }

    return enc->write_metadata(SrsFrameTypeScript, data, size);
}

void SrsFlvStreamEncoder::set_drop_if_not_match(bool v)
{
    enc->set_drop_if_not_match(v);
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
    // for flv stream, use gop cache of SrsLiveSource is ok.
    return false;
}

srs_error_t SrsFlvStreamEncoder::dump_cache(SrsLiveConsumer* /*consumer*/, SrsRtmpJitterAlgorithm /*jitter*/)
{
    // for flv stream, ignore cache.
    return srs_success;
}

srs_error_t SrsFlvStreamEncoder::write_tags(SrsSharedPtrMessage** msgs, int count)
{
    srs_error_t err = srs_success;

    // Ignore if no messages.
    if (count <= 0) return err;

    // For https://github.com/ossrs/srs/issues/939
    if (!header_written) {
        bool has_video = has_video_; bool has_audio = has_audio_;

        // See https://github.com/ossrs/srs/issues/939#issuecomment-1351385460
        if (guess_has_av_) {
            int nn_video_frames = 0; int nn_audio_frames = 0;
            has_audio = has_video = false;

            // Note that we must iterate all messages to count the audio and video frames.
            for (int i = 0; i < count; i++) {
                SrsSharedPtrMessage* msg = msgs[i];
                if (msg->is_video()) {
                    if (!SrsFlvVideo::sh(msg->payload, msg->size)) nn_video_frames++;
                    has_video = true;
                } else if (msg->is_audio()) {
                    if (!SrsFlvAudio::sh(msg->payload, msg->size)) nn_audio_frames++;
                    has_audio = true;
                }
            }

            // See https://github.com/ossrs/srs/issues/939#issuecomment-1348541733
            if (nn_video_frames > 0 && nn_audio_frames == 0) {
                if (has_audio) srs_trace("FLV: Reset has_audio for videos=%d and audios=%d", nn_video_frames, nn_audio_frames);
                has_audio = false;
            }
            if (nn_audio_frames > 0 && nn_video_frames == 0) {
                if (has_video) srs_trace("FLV: Reset has_video for videos=%d and audios=%d", nn_video_frames, nn_audio_frames);
                has_video = false;
            }
        }

        // Drop data if no A+V.
        if (!has_video && !has_audio) {
            return err;
        }

        if ((err = write_header(has_video, has_audio))  != srs_success) {
            return srs_error_wrap(err, "write header");
        }
    }

    // Write tags after header is done.
    return enc->write_tags(msgs, count);
}

srs_error_t SrsFlvStreamEncoder::write_header(bool has_video, bool has_audio)
{
    srs_error_t err = srs_success;

    if (!header_written) {
        header_written = true;

        if ((err = enc->write_header(has_video, has_audio))  != srs_success) {
            return srs_error_wrap(err, "write header");
        }

        srs_trace("FLV: write header audio=%d, video=%d, dinm=%d, config=%d/%d/%d", has_audio, has_video,
            enc->drop_if_not_match(), has_audio_, has_video_, guess_has_av_);
    }

    return err;
}

SrsAacStreamEncoder::SrsAacStreamEncoder()
{
    enc = new SrsAacTransmuxer();
    cache = NULL;
}

SrsAacStreamEncoder::~SrsAacStreamEncoder()
{
    srs_freep(enc);
}

srs_error_t SrsAacStreamEncoder::initialize(SrsFileWriter* w, SrsBufferCache* c)
{
    srs_error_t err = srs_success;
    
    cache = c;
    
    if ((err = enc->initialize(w)) != srs_success) {
        return srs_error_wrap(err, "init encoder");
    }
    
    return err;
}

srs_error_t SrsAacStreamEncoder::write_audio(int64_t timestamp, char* data, int size)
{
    return enc->write_audio(timestamp, data, size);
}

srs_error_t SrsAacStreamEncoder::write_video(int64_t /*timestamp*/, char* /*data*/, int /*size*/)
{
    // aac ignore any flv video.
    return srs_success;
}

srs_error_t SrsAacStreamEncoder::write_metadata(int64_t /*timestamp*/, char* /*data*/, int /*size*/)
{
    // aac ignore any flv metadata.
    return srs_success;
}

bool SrsAacStreamEncoder::has_cache()
{
    return true;
}

srs_error_t SrsAacStreamEncoder::dump_cache(SrsLiveConsumer* consumer, SrsRtmpJitterAlgorithm jitter)
{
    srs_assert(cache);
    return cache->dump_cache(consumer, jitter);
}

SrsMp3StreamEncoder::SrsMp3StreamEncoder()
{
    enc = new SrsMp3Transmuxer();
    cache = NULL;
}

SrsMp3StreamEncoder::~SrsMp3StreamEncoder()
{
    srs_freep(enc);
}

srs_error_t SrsMp3StreamEncoder::initialize(SrsFileWriter* w, SrsBufferCache* c)
{
    srs_error_t err = srs_success;
    
    cache = c;
    
    if ((err = enc->initialize(w)) != srs_success) {
        return srs_error_wrap(err, "init encoder");
    }
    
    if ((err = enc->write_header()) != srs_success) {
        return srs_error_wrap(err, "init encoder");
    }
    
    return err;
}

srs_error_t SrsMp3StreamEncoder::write_audio(int64_t timestamp, char* data, int size)
{
    return enc->write_audio(timestamp, data, size);
}

srs_error_t SrsMp3StreamEncoder::write_video(int64_t /*timestamp*/, char* /*data*/, int /*size*/)
{
    // mp3 ignore any flv video.
    return srs_success;
}

srs_error_t SrsMp3StreamEncoder::write_metadata(int64_t /*timestamp*/, char* /*data*/, int /*size*/)
{
    // mp3 ignore any flv metadata.
    return srs_success;
}

bool SrsMp3StreamEncoder::has_cache()
{
    return true;
}

srs_error_t SrsMp3StreamEncoder::dump_cache(SrsLiveConsumer* consumer, SrsRtmpJitterAlgorithm jitter)
{
    srs_assert(cache);
    return cache->dump_cache(consumer, jitter);
}

SrsBufferWriter::SrsBufferWriter(ISrsHttpResponseWriter* w)
{
    writer = w;
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

srs_error_t SrsBufferWriter::write(void* buf, size_t count, ssize_t* pnwrite)
{
    if (pnwrite) {
        *pnwrite = count;
    }
    return writer->write((char*)buf, (int)count);
}

srs_error_t SrsBufferWriter::writev(const iovec* iov, int iovcnt, ssize_t* pnwrite)
{
    return writer->writev(iov, iovcnt, pnwrite);
}

SrsLiveStream::SrsLiveStream(SrsLiveSource* s, SrsRequest* r, SrsBufferCache* c)
{
    source = s;
    cache = c;
    req = r->copy()->as_http();
}

SrsLiveStream::~SrsLiveStream()
{
    srs_freep(req);
}

srs_error_t SrsLiveStream::update_auth(SrsLiveSource* s, SrsRequest* r)
{
    source = s;
    
    srs_freep(req);
    req = r->copy()->as_http();
    
    return srs_success;
}

srs_error_t SrsLiveStream::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    srs_error_t err = srs_success;

    SrsHttpMessage* hr = dynamic_cast<SrsHttpMessage*>(r);
    SrsHttpConn* hc = dynamic_cast<SrsHttpConn*>(hr->connection());
    SrsHttpxConn* hxc = dynamic_cast<SrsHttpxConn*>(hc->handler());

    // Note that we should enable stat for HTTP streaming client, because each HTTP streaming connection is a real
    // session that should have statistics for itself.
    hxc->set_enable_stat(true);

    // Correct the app and stream by path, which is created from template.
    // @remark Be careful that the stream has extension now, might cause identify fail.
    req->stream = srs_path_basename(r->path());

    // update client ip
    req->ip = hc->remote_ip();

    // We must do stat the client before hooks, because hooks depends on it.
    SrsStatistic* stat = SrsStatistic::instance();
    if ((err = stat->on_client(_srs_context->get_id().c_str(), req, hc, SrsFlvPlay)) != srs_success) {
        return srs_error_wrap(err, "stat on client");
    }

    // We must do hook after stat, because depends on it.
    if ((err = http_hooks_on_play(r)) != srs_success) {
        return srs_error_wrap(err, "http hook");
    }
    
    err = do_serve_http(w, r);
    
    http_hooks_on_stop(r);
    
    return err;
}

srs_error_t SrsLiveStream::do_serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    srs_error_t err = srs_success;
    
    string enc_desc;
    ISrsBufferEncoder* enc = NULL;

    srs_assert(entry);
    bool drop_if_not_match = _srs_config->get_vhost_http_remux_drop_if_not_match(req->vhost);
    bool has_audio = _srs_config->get_vhost_http_remux_has_audio(req->vhost);
    bool has_video = _srs_config->get_vhost_http_remux_has_video(req->vhost);
    bool guess_has_av = _srs_config->get_vhost_http_remux_guess_has_av(req->vhost);

    if (srs_string_ends_with(entry->pattern, ".flv")) {
        w->header()->set_content_type("video/x-flv");
        enc_desc = "FLV";
        enc = new SrsFlvStreamEncoder();
        ((SrsFlvStreamEncoder*)enc)->set_drop_if_not_match(drop_if_not_match);
        ((SrsFlvStreamEncoder*)enc)->set_has_audio(has_audio);
        ((SrsFlvStreamEncoder*)enc)->set_has_video(has_video);
        ((SrsFlvStreamEncoder*)enc)->set_guess_has_av(guess_has_av);
    } else if (srs_string_ends_with(entry->pattern, ".aac")) {
        w->header()->set_content_type("audio/x-aac");
        enc_desc = "AAC";
        enc = new SrsAacStreamEncoder();
    } else if (srs_string_ends_with(entry->pattern, ".mp3")) {
        w->header()->set_content_type("audio/mpeg");
        enc_desc = "MP3";
        enc = new SrsMp3StreamEncoder();
    } else if (srs_string_ends_with(entry->pattern, ".ts")) {
        w->header()->set_content_type("video/MP2T");
        enc_desc = "TS";
        enc = new SrsTsStreamEncoder();
        ((SrsTsStreamEncoder*)enc)->set_has_audio(has_audio);
        ((SrsTsStreamEncoder*)enc)->set_has_video(has_video);
    } else {
        return srs_error_new(ERROR_HTTP_LIVE_STREAM_EXT, "invalid pattern=%s", entry->pattern.c_str());
    }
    SrsAutoFree(ISrsBufferEncoder, enc);

    // Enter chunked mode, because we didn't set the content-length.
    w->write_header(SRS_CONSTS_HTTP_OK);
    
    // create consumer of souce, ignore gop cache, use the audio gop cache.
    SrsLiveConsumer* consumer = NULL;
    SrsAutoFree(SrsLiveConsumer, consumer);
    if ((err = source->create_consumer(consumer)) != srs_success) {
        return srs_error_wrap(err, "create consumer");
    }
    if ((err = source->consumer_dumps(consumer, true, true, !enc->has_cache())) != srs_success) {
        return srs_error_wrap(err, "dumps consumer");
    }

    SrsPithyPrint* pprint = SrsPithyPrint::create_http_stream();
    SrsAutoFree(SrsPithyPrint, pprint);
    
    SrsMessageArray msgs(SRS_PERF_MW_MSGS);

    // Use receive thread to accept the close event to avoid FD leak.
    // @see https://github.com/ossrs/srs/issues/636#issuecomment-298208427
    SrsHttpMessage* hr = dynamic_cast<SrsHttpMessage*>(r);
    SrsHttpConn* hc = dynamic_cast<SrsHttpConn*>(hr->connection());
    
    // the memory writer.
    SrsBufferWriter writer(w);
    if ((err = enc->initialize(&writer, cache)) != srs_success) {
        return srs_error_wrap(err, "init encoder");
    }
    
    // if gop cache enabled for encoder, dump to consumer.
    if (enc->has_cache()) {
        if ((err = enc->dump_cache(consumer, source->jitter())) != srs_success) {
            return srs_error_wrap(err, "encoder dump cache");
        }
    }

    // Try to use fast flv encoder, remember that it maybe NULL.
    SrsFlvStreamEncoder* ffe = dynamic_cast<SrsFlvStreamEncoder*>(enc);

    // Note that the handler of hc now is hxc.
    SrsHttpxConn* hxc = dynamic_cast<SrsHttpxConn*>(hc->handler());
    srs_assert(hxc);

    // Start a thread to receive all messages from client, then drop them.
    SrsHttpRecvThread* trd = new SrsHttpRecvThread(hxc);
    SrsAutoFree(SrsHttpRecvThread, trd);
    
    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "start recv thread");
    }

    srs_utime_t mw_sleep = _srs_config->get_mw_sleep(req->vhost);
    srs_trace("FLV %s, encoder=%s, mw_sleep=%dms, cache=%d, msgs=%d, dinm=%d, guess_av=%d/%d/%d",
        entry->pattern.c_str(), enc_desc.c_str(), srsu2msi(mw_sleep), enc->has_cache(), msgs.max, drop_if_not_match,
        has_audio, has_video, guess_has_av);

    // TODO: free and erase the disabled entry after all related connections is closed.
    // TODO: FXIME: Support timeout for player, quit infinite-loop.
    while (entry->enabled) {
        // Whether client closed the FD.
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "recv thread");
        }

        pprint->elapse();

        // get messages from consumer.
        // each msg in msgs.msgs must be free, for the SrsMessageArray never free them.
        int count = 0;
        if ((err = consumer->dump_packets(&msgs, count)) != srs_success) {
            return srs_error_wrap(err, "consumer dump packets");
        }

        // TODO: FIXME: Support merged-write wait.
        if (count <= 0) {
            // Directly use sleep, donot use consumer wait, because we couldn't awake consumer.
            srs_usleep(mw_sleep);
            // ignore when nothing got.
            continue;
        }
        
        if (pprint->can_print()) {
            srs_trace("-> " SRS_CONSTS_LOG_HTTP_STREAM " http: got %d msgs, age=%d, min=%d, mw=%d",
                count, pprint->age(), SRS_PERF_MW_MIN_MSGS, srsu2msi(mw_sleep));
        }
        
        // sendout all messages.
        if (ffe) {
            err = ffe->write_tags(msgs.msgs, count);
        } else {
            err = streaming_send_messages(enc, msgs.msgs, count);
        }

        // TODO: FIXME: Update the stat.

        // free the messages.
        for (int i = 0; i < count; i++) {
            SrsSharedPtrMessage* msg = msgs.msgs[i];
            srs_freep(msg);
        }
        
        // check send error code.
        if (err != srs_success) {
            return srs_error_wrap(err, "send messages");
        }
    }

    // Here, the entry is disabled by encoder un-publishing or reloading,
    // so we must return a io.EOF error to disconnect the client, or the client will never quit.
    return srs_error_new(ERROR_HTTP_STREAM_EOF, "Stream EOF");
}

srs_error_t SrsLiveStream::http_hooks_on_play(ISrsHttpMessage* r)
{
    srs_error_t err = srs_success;
    
    if (!_srs_config->get_vhost_http_hooks_enabled(req->vhost)) {
        return err;
    }

    // Create request to report for the specified connection.
    SrsHttpMessage* hr = dynamic_cast<SrsHttpMessage*>(r);
    SrsRequest* nreq = hr->to_request(req->vhost);
    SrsAutoFree(SrsRequest, nreq);
    
    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;
    
    if (true) {
        SrsConfDirective* conf = _srs_config->get_vhost_on_play(nreq->vhost);
        
        if (!conf) {
            return err;
        }
        
        hooks = conf->args;
    }
    
    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        if ((err = SrsHttpHooks::on_play(url, nreq)) != srs_success) {
            return srs_error_wrap(err, "http on_play %s", url.c_str());
        }
    }
    
    return err;
}

void SrsLiveStream::http_hooks_on_stop(ISrsHttpMessage* r)
{
    if (!_srs_config->get_vhost_http_hooks_enabled(req->vhost)) {
        return;
    }

    // Create request to report for the specified connection.
    SrsHttpMessage* hr = dynamic_cast<SrsHttpMessage*>(r);
    SrsRequest* nreq = hr->to_request(req->vhost);
    SrsAutoFree(SrsRequest, nreq);
    
    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;
    
    if (true) {
        SrsConfDirective* conf = _srs_config->get_vhost_on_stop(nreq->vhost);
        
        if (!conf) {
            srs_info("ignore the empty http callback: on_stop");
            return;
        }
        
        hooks = conf->args;
    }
    
    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        SrsHttpHooks::on_stop(url, nreq);
    }
    
    return;
}

srs_error_t SrsLiveStream::streaming_send_messages(ISrsBufferEncoder* enc, SrsSharedPtrMessage** msgs, int nb_msgs)
{
    srs_error_t err = srs_success;

    // TODO: In gop cache, we know both the audio and video codec, so we should notice the encoder, which might depends
    // on setting the correct codec information, for example, HTTP-TS or HLS will write PMT.
    for (int i = 0; i < nb_msgs; i++) {
        SrsSharedPtrMessage* msg = msgs[i];
        
        if (msg->is_audio()) {
            err = enc->write_audio(msg->timestamp, msg->payload, msg->size);
        } else if (msg->is_video()) {
            err = enc->write_video(msg->timestamp, msg->payload, msg->size);
        } else {
            err = enc->write_metadata(msg->timestamp, msg->payload, msg->size);
        }
        
        if (err != srs_success) {
            return srs_error_wrap(err, "send messages");
        }
    }
    
    return err;
}

SrsLiveEntry::SrsLiveEntry(std::string m)
{
    mount = m;
    
    stream = NULL;
    cache = NULL;
    
    req = NULL;
    source = NULL;
    
    std::string ext = srs_path_filext(m);
    _is_flv = (ext == ".flv");
    _is_ts = (ext == ".ts");
    _is_mp3 = (ext == ".mp3");
    _is_aac = (ext == ".aac");
}

SrsLiveEntry::~SrsLiveEntry()
{
    srs_freep(req);
}

bool SrsLiveEntry::is_flv()
{
    return _is_flv;
}

bool SrsLiveEntry::is_ts()
{
    return _is_ts;
}

bool SrsLiveEntry::is_aac()
{
    return _is_aac;
}

bool SrsLiveEntry::is_mp3()
{
    return _is_mp3;
}

SrsHttpStreamServer::SrsHttpStreamServer(SrsServer* svr)
{
    server = svr;
    
    mux.hijack(this);
    _srs_config->subscribe(this);
}

SrsHttpStreamServer::~SrsHttpStreamServer()
{
    mux.unhijack(this);
    _srs_config->unsubscribe(this);
    
    if (true) {
        std::map<std::string, SrsLiveEntry*>::iterator it;
        for (it = tflvs.begin(); it != tflvs.end(); ++it) {
            SrsLiveEntry* entry = it->second;
            srs_freep(entry);
        }
        tflvs.clear();
    }
    if (true) {
        std::map<std::string, SrsLiveEntry*>::iterator it;
        for (it = sflvs.begin(); it != sflvs.end(); ++it) {
            SrsLiveEntry* entry = it->second;
            srs_freep(entry);
        }
        sflvs.clear();
    }
}

srs_error_t SrsHttpStreamServer::initialize()
{
    srs_error_t err = srs_success;
    
    // remux rtmp to flv live streaming
    if ((err = initialize_flv_streaming()) != srs_success) {
        return srs_error_wrap(err, "http flv stream");
    }
    
    return err;
}

// TODO: FIXME: rename for HTTP FLV mount.
srs_error_t SrsHttpStreamServer::http_mount(SrsLiveSource* s, SrsRequest* r)
{
    srs_error_t err = srs_success;
    
    // the id to identify stream.
    std::string sid = r->get_stream_url();
    SrsLiveEntry* entry = NULL;
    
    // create stream from template when not found.
    if (sflvs.find(sid) == sflvs.end()) {
        if (tflvs.find(r->vhost) == tflvs.end()) {
            return err;
        }
        
        SrsLiveEntry* tmpl = tflvs[r->vhost];
        
        std::string mount = tmpl->mount;
        
        // replace the vhost variable
        mount = srs_string_replace(mount, "[vhost]", r->vhost);
        mount = srs_string_replace(mount, "[app]", r->app);
        mount = srs_string_replace(mount, "[stream]", r->stream);
        
        // remove the default vhost mount
        mount = srs_string_replace(mount, SRS_CONSTS_RTMP_DEFAULT_VHOST"/", "/");
        
        entry = new SrsLiveEntry(mount);

        entry->source = s;
        entry->req = r->copy()->as_http();
        entry->cache = new SrsBufferCache(s, r);
        entry->stream = new SrsLiveStream(s, r, entry->cache);
        
        // TODO: FIXME: maybe refine the logic of http remux service.
        // if user push streams followed:
        //     rtmp://test.com/live/stream1
        //     rtmp://test.com/live/stream2
        // and they will using the same template, such as: [vhost]/[app]/[stream].flv
        // so, need to free last request object, otherwise, it will cause memory leak.
        srs_freep(tmpl->req);
        
        tmpl->source = s;
        tmpl->req = r->copy()->as_http();
        
        sflvs[sid] = entry;
        
        // mount the http flv stream.
        // we must register the handler, then start the thread,
        // for the thread will cause thread switch context.
        if ((err = mux.handle(mount, entry->stream)) != srs_success) {
            return srs_error_wrap(err, "http: mount flv stream for vhost=%s failed", sid.c_str());
        }
        
        // start http stream cache thread
        if ((err = entry->cache->start()) != srs_success) {
            return srs_error_wrap(err, "http: start stream cache failed");
        }
        srs_trace("http: mount flv stream for sid=%s, mount=%s", sid.c_str(), mount.c_str());
    } else {
        // The entry exists, we reuse it and update the request of stream and cache.
        entry = sflvs[sid];
        entry->stream->update_auth(s, r);
        entry->cache->update_auth(s, r);
    }
    
    if (entry->stream) {
        entry->stream->entry->enabled = true;
        return err;
    }
    
    return err;
}

void SrsHttpStreamServer::http_unmount(SrsLiveSource* s, SrsRequest* r)
{
    std::string sid = r->get_stream_url();
    
    if (sflvs.find(sid) == sflvs.end()) {
        return;
    }
    
    SrsLiveEntry* entry = sflvs[sid];
    entry->stream->entry->enabled = false;
}

srs_error_t SrsHttpStreamServer::hijack(ISrsHttpMessage* request, ISrsHttpHandler** ph)
{
    srs_error_t err = srs_success;
    
    // when handler not the root, we think the handler is ok.
    ISrsHttpHandler* h = *ph? *ph : NULL;
    if (h && h->entry && h->entry->pattern != "/") {
        return err;
    }
    
    // only hijack for http streaming, http-flv/ts/mp3/aac.
    std::string ext = request->ext();
    if (ext.empty()) {
        return err;
    }
    
    // find the actually request vhost.
    SrsConfDirective* vhost = _srs_config->get_vhost(request->host());
    if (!vhost || !_srs_config->get_vhost_enabled(vhost)) {
        return err;
    }
    
    // find the entry template for the stream.
    SrsLiveEntry* entry = NULL;
    if (true) {
        // no http streaming on vhost, ignore.
        std::map<std::string, SrsLiveEntry*>::iterator it = tflvs.find(vhost->arg0());
        if (it == tflvs.end()) {
            return err;
        }

        // hstrs always enabled.
        // for origin, the http stream will be mount already when publish,
        //      so it must never enter this line for stream already mounted.
        // for edge, the http stream is trigger by hstrs and mount by it,
        //      so we only hijack when only edge and hstrs is on.
        entry = it->second;
        
        // check entry and request extension.
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

    // For HTTP-FLV stream, the template must have the same schema with upath.
    // The template is defined in config, the mout of http stream. The upath is specified by http request path.
    // If template is "[vhost]/[app]/[stream].flv", the upath should be:
    //      matched for "/live/livestream.flv"
    //      matched for "ossrs.net/live/livestream.flv"
    //      not-matched for "/livestream.flv", which is actually "/__defaultApp__/livestream.flv", HTTP not support default app.
    //      not-matched for "/live/show/livestream.flv"
    string upath = request->path();
    if (srs_string_count(upath, "/") != srs_string_count(entry->mount, "/")) {
        return err;
    }

    // convert to concreate class.
    SrsHttpMessage* hreq = dynamic_cast<SrsHttpMessage*>(request);
    srs_assert(hreq);
    
    // hijack for entry.
    SrsRequest* r = hreq->to_request(vhost->arg0());
    SrsAutoFree(SrsRequest, r);
    
    std::string sid = r->get_stream_url();
    // check whether the http remux is enabled,
    // for example, user disable the http flv then reload.
    if (sflvs.find(sid) != sflvs.end()) {
        SrsLiveEntry* s_entry = sflvs[sid];
        if (!s_entry->stream->entry->enabled) {
            // only when the http entry is disabled, check the config whether http flv disable,
            // for the http flv edge use hijack to trigger the edge ingester, we always mount it
            // eventhough the origin does not exists the specified stream.
            if (!_srs_config->get_vhost_http_remux_enabled(r->vhost)) {
                return srs_error_new(ERROR_HTTP_HIJACK, "stream disabled");
            }
        }
    }
    
    SrsLiveSource* s = NULL;
    if ((err = _srs_sources->fetch_or_create(r, server, &s)) != srs_success) {
        return srs_error_wrap(err, "source create");
    }
    srs_assert(s != NULL);
    
    bool enabled_cache = _srs_config->get_gop_cache(r->vhost);
    int gcmf = _srs_config->get_gop_cache_max_frames(r->vhost);
    s->set_cache(enabled_cache);
    s->set_gop_cache_max_frames(gcmf);

    // create http streaming handler.
    if ((err = http_mount(s, r)) != srs_success) {
        return srs_error_wrap(err, "http mount");
    }
    
    // use the handler if exists.
    if (ph) {
        if (sflvs.find(sid) != sflvs.end()) {
            entry = sflvs[sid];
            *ph = entry->stream;
        }
    }
    
    // trigger edge to fetch from origin.
    bool vhost_is_edge = _srs_config->get_vhost_is_edge(r->vhost);
    srs_trace("flv: source url=%s, is_edge=%d, source_id=%s/%s",
        r->get_stream_url().c_str(), vhost_is_edge, s->source_id().c_str(), s->pre_source_id().c_str());

    return err;
}

srs_error_t SrsHttpStreamServer::initialize_flv_streaming()
{
    srs_error_t err = srs_success;
    
    // http flv live stream mount for each vhost.
    SrsConfDirective* root = _srs_config->get_root();
    for (int i = 0; i < (int)root->directives.size(); i++) {
        SrsConfDirective* conf = root->at(i);
        
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

    if (!_srs_config->get_vhost_http_remux_enabled(vhost)) {
        return err;
    }
    
    SrsLiveEntry* entry = new SrsLiveEntry(_srs_config->get_vhost_http_remux_mount(vhost));
    
    tflvs[vhost] = entry;
    srs_trace("http flv live stream, vhost=%s, mount=%s", vhost.c_str(), entry->mount.c_str());
    
    return err;
}

