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

#include <srs_app_http_stream.hpp>

#define SRS_STREAM_CACHE_CYCLE_SECONDS 30

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

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
#include <srs_rtmp_stack.hpp>
#include <srs_app_source.hpp>
#include <srs_rtmp_msg_array.hpp>
#include <srs_kernel_aac.hpp>
#include <srs_kernel_mp3.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_app_source.hpp>
#include <srs_app_server.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_recv_thread.hpp>

SrsBufferCache::SrsBufferCache(SrsSource* s, SrsRequest* r)
{
    req = r->copy();
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

srs_error_t SrsBufferCache::update(SrsSource* s, SrsRequest* r)
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

srs_error_t SrsBufferCache::dump_cache(SrsConsumer* consumer, SrsRtmpJitterAlgorithm jitter)
{
    srs_error_t err = srs_success;
    
    if (fast_cache <= 0) {
        return err;
    }
    
    // the jitter is get from SrsSource, which means the time_jitter of vhost.
    if ((err = queue->dump_packets(consumer, false, jitter)) != srs_success) {
        return srs_error_wrap(err, "dump packets");
    }
    
    srs_trace("http: dump cache %d msgs, duration=%dms, cache=%.2fs", queue->size(), queue->duration(), fast_cache);
    
    return err;
}

srs_error_t SrsBufferCache::cycle()
{
    srs_error_t err = srs_success;
    
    // TODO: FIXME: support reload.
    if (fast_cache <= 0) {
        srs_usleep(SRS_STREAM_CACHE_CYCLE_SECONDS * 1000 * 1000);
        return err;
    }
    
    // the stream cache will create consumer to cache stream,
    // which will trigger to fetch stream from origin for edge.
    SrsConsumer* consumer = NULL;
    if ((err = source->create_consumer(NULL, consumer, false, false, true)) != srs_success) {
        return srs_error_wrap(err, "create consumer");
    }
    SrsAutoFree(SrsConsumer, consumer);
    
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
            srs_info("http: sleep %dms for no msg", SRS_CONSTS_RTMP_PULSE_TMMS);
            // directly use sleep, donot use consumer wait.
            srs_usleep(SRS_CONSTS_RTMP_PULSE_TMMS * 1000);
            
            // ignore when nothing got.
            continue;
        }
        
        if (pprint->can_print()) {
            srs_trace("-> " SRS_CONSTS_LOG_HTTP_STREAM_CACHE " http: got %d msgs, age=%d, min=%d, mw=%d",
                      count, pprint->age(), SRS_PERF_MW_MIN_MSGS, SRS_CONSTS_RTMP_PULSE_TMMS);
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
    // for ts stream, use gop cache of SrsSource is ok.
    return false;
}

srs_error_t SrsTsStreamEncoder::dump_cache(SrsConsumer* /*consumer*/, SrsRtmpJitterAlgorithm /*jitter*/)
{
    // for ts stream, ignore cache.
    return srs_success;
}

SrsFlvStreamEncoder::SrsFlvStreamEncoder()
{
    enc = new SrsFlvTransmuxer();
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
    
    // write flv header.
    if ((err = enc->write_header())  != srs_success) {
        return srs_error_wrap(err, "write header");
    }
    
    return err;
}

srs_error_t SrsFlvStreamEncoder::write_audio(int64_t timestamp, char* data, int size)
{
    return enc->write_audio(timestamp, data, size);
}

srs_error_t SrsFlvStreamEncoder::write_video(int64_t timestamp, char* data, int size)
{
    return enc->write_video(timestamp, data, size);
}

srs_error_t SrsFlvStreamEncoder::write_metadata(int64_t timestamp, char* data, int size)
{
    return enc->write_metadata(SrsFrameTypeScript, data, size);
}

bool SrsFlvStreamEncoder::has_cache()
{
    // for flv stream, use gop cache of SrsSource is ok.
    return false;
}

srs_error_t SrsFlvStreamEncoder::dump_cache(SrsConsumer* /*consumer*/, SrsRtmpJitterAlgorithm /*jitter*/)
{
    // for flv stream, ignore cache.
    return srs_success;
}

#ifdef SRS_PERF_FAST_FLV_ENCODER
SrsFastFlvStreamEncoder::SrsFastFlvStreamEncoder()
{
}

SrsFastFlvStreamEncoder::~SrsFastFlvStreamEncoder()
{
}

srs_error_t SrsFastFlvStreamEncoder::write_tags(SrsSharedPtrMessage** msgs, int count)
{
    return enc->write_tags(msgs, count);
}
#endif

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

srs_error_t SrsAacStreamEncoder::dump_cache(SrsConsumer* consumer, SrsRtmpJitterAlgorithm jitter)
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

srs_error_t SrsMp3StreamEncoder::dump_cache(SrsConsumer* consumer, SrsRtmpJitterAlgorithm jitter)
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

SrsLiveStream::SrsLiveStream(SrsSource* s, SrsRequest* r, SrsBufferCache* c)
{
    source = s;
    cache = c;
    req = r->copy();
}

SrsLiveStream::~SrsLiveStream()
{
    srs_freep(req);
}

srs_error_t SrsLiveStream::update(SrsSource* s, SrsRequest* r)
{
    srs_freep(req);
    source = s;
    req = r->copy();
    
    return srs_success;
}

srs_error_t SrsLiveStream::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    srs_error_t err = srs_success;
    
    ISrsBufferEncoder* enc = NULL;
    
    srs_assert(entry);
    if (srs_string_ends_with(entry->pattern, ".flv")) {
        w->header()->set_content_type("video/x-flv");
#ifdef SRS_PERF_FAST_FLV_ENCODER
        enc = new SrsFastFlvStreamEncoder();
#else
        enc = new SrsFlvStreamEncoder();
#endif
    } else if (srs_string_ends_with(entry->pattern, ".aac")) {
        w->header()->set_content_type("audio/x-aac");
        enc = new SrsAacStreamEncoder();
    } else if (srs_string_ends_with(entry->pattern, ".mp3")) {
        w->header()->set_content_type("audio/mpeg");
        enc = new SrsMp3StreamEncoder();
    } else if (srs_string_ends_with(entry->pattern, ".ts")) {
        w->header()->set_content_type("video/MP2T");
        enc = new SrsTsStreamEncoder();
    } else {
        return srs_error_new(ERROR_HTTP_LIVE_STREAM_EXT, "invalid pattern=%s", entry->pattern.c_str());
    }
    SrsAutoFree(ISrsBufferEncoder, enc);
    
    // create consumer of souce, ignore gop cache, use the audio gop cache.
    SrsConsumer* consumer = NULL;
    if ((err = source->create_consumer(NULL, consumer, true, true, !enc->has_cache())) != srs_success) {
        return srs_error_wrap(err, "create consumer");
    }
    SrsAutoFree(SrsConsumer, consumer);
    srs_verbose("http: consumer created success.");
    
    SrsPithyPrint* pprint = SrsPithyPrint::create_http_stream();
    SrsAutoFree(SrsPithyPrint, pprint);
    
    SrsMessageArray msgs(SRS_PERF_MW_MSGS);
    
    // update the statistic when source disconveried.
    SrsStatistic* stat = SrsStatistic::instance();
    if ((err = stat->on_client(_srs_context->get_id(), req, NULL, SrsRtmpConnPlay)) != srs_success) {
        return srs_error_wrap(err, "stat on client");
    }
    
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
    
#ifdef SRS_PERF_FAST_FLV_ENCODER
    SrsFastFlvStreamEncoder* ffe = dynamic_cast<SrsFastFlvStreamEncoder*>(enc);
#endif
    
    // Use receive thread to accept the close event to avoid FD leak.
    // @see https://github.com/ossrs/srs/issues/636#issuecomment-298208427
    SrsHttpMessage* hr = dynamic_cast<SrsHttpMessage*>(r);
    SrsResponseOnlyHttpConn* hc = dynamic_cast<SrsResponseOnlyHttpConn*>(hr->connection());
    
    SrsHttpRecvThread* trd = new SrsHttpRecvThread(hc);
    SrsAutoFree(SrsHttpRecvThread, trd);
    
    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "start recv thread");
    }

    // TODO: free and erase the disabled entry after all related connections is closed.
    while (entry->enabled) {
        pprint->elapse();
        
        // Whether client closed the FD.
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "recv thread");
        }

        // get messages from consumer.
        // each msg in msgs.msgs must be free, for the SrsMessageArray never free them.
        int count = 0;
        if ((err = consumer->dump_packets(&msgs, count)) != srs_success) {
            return srs_error_wrap(err, "consumer dump packets");
        }
        
        if (count <= 0) {
            srs_info("http: sleep %dms for no msg", SRS_CONSTS_RTMP_PULSE_TMMS);
            // directly use sleep, donot use consumer wait.
            srs_usleep(SRS_CONSTS_RTMP_PULSE_TMMS * 1000);
            
            // ignore when nothing got.
            continue;
        }
        
        if (pprint->can_print()) {
            srs_info("-> " SRS_CONSTS_LOG_HTTP_STREAM " http: got %d msgs, age=%d, min=%d, mw=%d",
                count, pprint->age(), SRS_PERF_MW_MIN_MSGS, SRS_CONSTS_RTMP_PULSE_TMMS);
        }
        
        // sendout all messages.
#ifdef SRS_PERF_FAST_FLV_ENCODER
        if (ffe) {
            err = ffe->write_tags(msgs.msgs, count);
        } else {
            err = streaming_send_messages(enc, msgs.msgs, count);
        }
#else
        err = streaming_send_messages(enc, msgs.msgs, count);
#endif
        
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
    
    return err;
}

srs_error_t SrsLiveStream::streaming_send_messages(ISrsBufferEncoder* enc, SrsSharedPtrMessage** msgs, int nb_msgs)
{
    srs_error_t err = srs_success;
    
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
            srs_freep(entry->req);
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
srs_error_t SrsHttpStreamServer::http_mount(SrsSource* s, SrsRequest* r)
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
        tmpl->req = r->copy();
        
        sflvs[sid] = entry;
        
        // mount the http flv stream.
        // we must register the handler, then start the thread,
        // for the thread will cause thread switch context.
        // @see https://github.com/ossrs/srs/issues/404
        if ((err = mux.handle(mount, entry->stream)) != srs_success) {
            return srs_error_wrap(err, "http: mount flv stream for vhost=%s failed", sid.c_str());
        }
        
        // start http stream cache thread
        if ((err = entry->cache->start()) != srs_success) {
            return srs_error_wrap(err, "http: start stream cache failed");
        }
        srs_trace("http: mount flv stream for sid=%s, mount=%s", sid.c_str(), mount.c_str());
    } else {
        entry = sflvs[sid];
        entry->stream->update(s, r);
        entry->cache->update(s, r);
    }
    
    if (entry->stream) {
        entry->stream->entry->enabled = true;
        return err;
    }
    
    return err;
}

void SrsHttpStreamServer::http_unmount(SrsSource* s, SrsRequest* r)
{
    std::string sid = r->get_stream_url();
    
    if (sflvs.find(sid) == sflvs.end()) {
        return;
    }
    
    SrsLiveEntry* entry = sflvs[sid];
    entry->stream->entry->enabled = false;
}

srs_error_t SrsHttpStreamServer::on_reload_vhost_added(string vhost)
{
    srs_error_t err = srs_success;
    
    if ((err = on_reload_vhost_http_remux_updated(vhost)) != srs_success) {
        return srs_error_wrap(err, "reload vhost added");
    }
    
    return err;
}

srs_error_t SrsHttpStreamServer::on_reload_vhost_http_remux_updated(string vhost)
{
    srs_error_t err = srs_success;
    
    if (tflvs.find(vhost) == tflvs.end()) {
        if ((err = initialize_flv_entry(vhost)) != srs_success) {
            return srs_error_wrap(err, "init flv entry");
        }
        
        // http mount need SrsRequest and SrsSource param, only create a mapping template entry
        // and do mount automatically on playing http flv if this stream is a new http_remux stream.
        return err;
    }
    
    SrsLiveEntry* tmpl = tflvs[vhost];
    SrsRequest* req = tmpl->req;
    SrsSource* source = tmpl->source;
    
    if (source && req) {
        // cleanup the exists http remux.
        http_unmount(source, req);
    }
    
    if (!_srs_config->get_vhost_http_remux_enabled(vhost)) {
        return err;
    }
    
    string old_tmpl_mount = tmpl->mount;
    string new_tmpl_mount = _srs_config->get_vhost_http_remux_mount(vhost);
    
    /**
     * TODO: not support to reload different mount url for the time being.
     * if the mount is change, need more logical thing to deal with.
     * such as erase stream from sflvs and free all related resource.
     */
    srs_assert(old_tmpl_mount == new_tmpl_mount);
    
    // do http mount directly with SrsRequest and SrsSource if stream is played already.
    if (req) {
        std::string sid = req->get_stream_url();
        
        // remount stream.
        if ((err = http_mount(source, req)) != srs_success) {
            return srs_error_wrap(err, "vhost %s http_remux reload failed", vhost.c_str());
        }
    } else {
        // for without SrsRequest and SrsSource if stream is not played yet, do http mount automatically
        // when start play this http flv stream.
    }
    
    srs_trace("vhost %s http_remux reload success", vhost.c_str());
    
    return err;
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
    
    SrsSource* s = NULL;
    if ((err = SrsSource::fetch_or_create(r, server, &s)) != srs_success) {
        return srs_error_wrap(err, "source create");
    }
    srs_assert(s != NULL);
    
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
    srs_trace("flv: source url=%s, is_edge=%d, source_id=%d[%d]",
        r->get_stream_url().c_str(), vhost_is_edge, s->source_id(), s->source_id());
    
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

