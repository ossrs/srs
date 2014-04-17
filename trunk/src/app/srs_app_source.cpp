/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

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

#include <srs_app_source.hpp>

#include <algorithm>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_core_autofree.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_app_codec.hpp>
#include <srs_app_hls.hpp>
#include <srs_app_forward.hpp>
#include <srs_app_config.hpp>
#include <srs_app_encoder.hpp>
#include <srs_protocol_rtmp.hpp>
#include <srs_app_dvr.hpp>
#include <srs_kernel_stream.hpp>

#define CONST_MAX_JITTER_MS         500
#define DEFAULT_FRAME_TIME_MS         40

SrsRtmpJitter::SrsRtmpJitter()
{
    last_pkt_correct_time = last_pkt_time = 0;
}

SrsRtmpJitter::~SrsRtmpJitter()
{
}

int SrsRtmpJitter::correct(SrsSharedPtrMessage* msg, int tba, int tbv)
{
    int ret = ERROR_SUCCESS;

    // set to 0 for metadata.
    if (!msg->header.is_video() && !msg->header.is_audio()) {
        msg->header.timestamp = 0;
        return ret;
    }
    
    int sample_rate = tba;
    int frame_rate = tbv;
    
    /**
    * we use a very simple time jitter detect/correct algorithm:
    * 1. delta: ensure the delta is positive and valid,
    *     we set the delta to DEFAULT_FRAME_TIME_MS,
    *     if the delta of time is nagative or greater than CONST_MAX_JITTER_MS.
    * 2. last_pkt_time: specifies the original packet time,
    *     is used to detect next jitter.
    * 3. last_pkt_correct_time: simply add the positive delta, 
    *     and enforce the time monotonically.
    */
    int64_t time = msg->header.timestamp;
    int64_t delta = time - last_pkt_time;

    // if jitter detected, reset the delta.
    if (delta < 0 || delta > CONST_MAX_JITTER_MS) {
        // calc the right diff by audio sample rate
        if (msg->header.is_audio() && sample_rate > 0) {
            delta = (int64_t)(delta * 1000.0 / sample_rate);
        } else if (msg->header.is_video() && frame_rate > 0) {
            delta = (int64_t)(delta * 1.0 / frame_rate);
        } else {
            delta = DEFAULT_FRAME_TIME_MS;
        }

        // sometimes, the time is absolute time, so correct it again.
        if (delta < 0 || delta > CONST_MAX_JITTER_MS) {
            delta = DEFAULT_FRAME_TIME_MS;
        }
        
        srs_info("jitter detected, last_pts=%"PRId64", pts=%"PRId64", diff=%"PRId64", last_time=%"PRId64", time=%"PRId64", diff=%"PRId64"",
            last_pkt_time, time, time - last_pkt_time, last_pkt_correct_time, last_pkt_correct_time + delta, delta);
    } else {
        srs_verbose("timestamp no jitter. time=%"PRId64", last_pkt=%"PRId64", correct_to=%"PRId64"", 
            time, last_pkt_time, last_pkt_correct_time + delta);
    }
    
    last_pkt_correct_time = srs_max(0, last_pkt_correct_time + delta);
    
    msg->header.timestamp = last_pkt_correct_time;
    last_pkt_time = time;
    
    return ret;
}

int SrsRtmpJitter::get_time()
{
    return (int)last_pkt_correct_time;
}

SrsMessageQueue::SrsMessageQueue()
{
    queue_size_ms = 0;
    av_start_time = av_end_time = -1;
}

SrsMessageQueue::~SrsMessageQueue()
{
    clear();
}

void SrsMessageQueue::set_queue_size(double queue_size)
{
    queue_size_ms = (int)(queue_size * 1000);
}

int SrsMessageQueue::enqueue(SrsSharedPtrMessage* msg)
{
    int ret = ERROR_SUCCESS;
    
    if (msg->header.is_video() || msg->header.is_audio()) {
        if (av_start_time == -1) {
            av_start_time = msg->header.timestamp;
        }
        
        av_end_time = msg->header.timestamp;
    }
    
    msgs.push_back(msg);

    while (av_end_time - av_start_time > queue_size_ms) {
        shrink();
    }
    
    return ret;
}

int SrsMessageQueue::get_packets(int max_count, SrsSharedPtrMessage**& pmsgs, int& count)
{
    int ret = ERROR_SUCCESS;
    
    if (msgs.empty()) {
        return ret;
    }
    
    if (max_count == 0) {
        count = (int)msgs.size();
    } else {
        count = srs_min(max_count, (int)msgs.size());
    }

    if (count <= 0) {
        return ret;
    }
    
    pmsgs = new SrsSharedPtrMessage*[count];
    
    for (int i = 0; i < count; i++) {
        pmsgs[i] = msgs[i];
    }
    
    SrsSharedPtrMessage* last = msgs[count - 1];
    av_start_time = last->header.timestamp;
    
    if (count == (int)msgs.size()) {
        msgs.clear();
    } else {
        msgs.erase(msgs.begin(), msgs.begin() + count);
    }
    
    return ret;
}

void SrsMessageQueue::shrink()
{
    int iframe_index = -1;
    
    // issue the first iframe.
    // skip the first frame, whatever the type of it,
    // for when we shrinked, the first is the iframe,
    // we will directly remove the gop next time.
    for (int i = 1; i < (int)msgs.size(); i++) {
        SrsSharedPtrMessage* msg = msgs[i];
        
        if (msg->header.is_video()) {
            if (SrsCodec::video_is_keyframe(msg->payload, msg->size)) {
                // the max frame index to remove.
                iframe_index = i;
                
                // set the start time, we will remove until this frame.
                av_start_time = msg->header.timestamp;
                
                break;
            }
        }
    }
    
    // no iframe, clear the queue.
    if (iframe_index < 0) {
        clear();
        return;
    }
    
    srs_trace("shrink the cache queue, size=%d, removed=%d, max=%.2f", 
        (int)msgs.size(), iframe_index, queue_size_ms / 1000.0);
    
    // remove the first gop from the front
    for (int i = 0; i < iframe_index; i++) {
        SrsSharedPtrMessage* msg = msgs[i];
        srs_freep(msg);
    }
    msgs.erase(msgs.begin(), msgs.begin() + iframe_index);
}

void SrsMessageQueue::clear()
{
    std::vector<SrsSharedPtrMessage*>::iterator it;

    for (it = msgs.begin(); it != msgs.end(); ++it) {
        SrsSharedPtrMessage* msg = *it;
        srs_freep(msg);
    }

    msgs.clear();
    
    av_start_time = av_end_time = -1;
}

SrsConsumer::SrsConsumer(SrsSource* _source)
{
    source = _source;
    paused = false;
    jitter = new SrsRtmpJitter();
    queue = new SrsMessageQueue();
}

SrsConsumer::~SrsConsumer()
{
    source->on_consumer_destroy(this);
    srs_freep(jitter);
    srs_freep(queue);
}

void SrsConsumer::set_queue_size(double queue_size)
{
    queue->set_queue_size(queue_size);
}

int SrsConsumer::get_time()
{
    return jitter->get_time();
}

int SrsConsumer::enqueue(SrsSharedPtrMessage* msg, int tba, int tbv)
{
    int ret = ERROR_SUCCESS;
    
    if (!source->is_atc()) {
        if ((ret = jitter->correct(msg, tba, tbv)) != ERROR_SUCCESS) {
            srs_freep(msg);
            return ret;
        }
    }
    
    if ((ret = queue->enqueue(msg)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsConsumer::get_packets(int max_count, SrsSharedPtrMessage**& pmsgs, int& count)
{
    // paused, return nothing.
    if (paused) {
        return ERROR_SUCCESS;
    }
    
    return queue->get_packets(max_count, pmsgs, count);
}

int SrsConsumer::on_play_client_pause(bool is_pause)
{
    int ret = ERROR_SUCCESS;
    
    srs_trace("stream consumer change pause state %d=>%d", paused, is_pause);
    paused = is_pause;
    
    return ret;
}

SrsGopCache::SrsGopCache()
{
    cached_video_count = 0;
    enable_gop_cache = true;
}

SrsGopCache::~SrsGopCache()
{
    clear();
}

void SrsGopCache::set(bool enabled)
{
    enable_gop_cache = enabled;
    
    if (!enabled) {
        srs_info("disable gop cache, clear %d packets.", (int)gop_cache.size());
        clear();
        return;
    }
    
    srs_info("enable gop cache");
}

int SrsGopCache::cache(SrsSharedPtrMessage* msg)
{
    int ret = ERROR_SUCCESS;
    
    if (!enable_gop_cache) {
        srs_verbose("gop cache is disabled.");
        return ret;
    }
    
    // got video, update the video count if acceptable
    if (msg->header.is_video()) {
        cached_video_count++;
    }
    
    // no acceptable video or pure audio, disable the cache.
    if (cached_video_count == 0) {
        srs_verbose("ignore any frame util got a h264 video frame.");
        return ret;
    }
    
    // clear gop cache when got key frame
    if (msg->header.is_video() && SrsCodec::video_is_keyframe(msg->payload, msg->size)) {
        srs_info("clear gop cache when got keyframe. vcount=%d, count=%d",
            cached_video_count, (int)gop_cache.size());
            
        clear();
        
        // curent msg is video frame, so we set to 1.
        cached_video_count = 1;
    }
    
    // cache the frame.
    gop_cache.push_back(msg->copy());
    
    return ret;
}

void SrsGopCache::clear()
{
    std::vector<SrsSharedPtrMessage*>::iterator it;
    for (it = gop_cache.begin(); it != gop_cache.end(); ++it) {
        SrsSharedPtrMessage* msg = *it;
        srs_freep(msg);
    }
    gop_cache.clear();

    cached_video_count = 0;
}
    
int SrsGopCache::dump(SrsConsumer* consumer, int tba, int tbv)
{
    int ret = ERROR_SUCCESS;
    
    std::vector<SrsSharedPtrMessage*>::iterator it;
    for (it = gop_cache.begin(); it != gop_cache.end(); ++it) {
        SrsSharedPtrMessage* msg = *it;
        if ((ret = consumer->enqueue(msg->copy(), tba, tbv)) != ERROR_SUCCESS) {
            srs_error("dispatch cached gop failed. ret=%d", ret);
            return ret;
        }
    }
    srs_trace("dispatch cached gop success. count=%d, duration=%d", (int)gop_cache.size(), consumer->get_time());
    
    return ret;
}

bool SrsGopCache::empty()
{
    return gop_cache.empty();
}

int64_t SrsGopCache::get_start_time()
{
    if (empty()) {
        return 0;
    }
    
    SrsSharedPtrMessage* msg = gop_cache[0];
    srs_assert(msg);
    
    return msg->header.timestamp;
}

std::map<std::string, SrsSource*> SrsSource::pool;

SrsSource* SrsSource::find(SrsRequest* req)
{
    string stream_url = req->get_stream_url();
    string vhost = req->vhost;
    
    if (pool.find(stream_url) == pool.end()) {
        pool[stream_url] = new SrsSource(req);
        srs_info("create new source for url=%s, vhost=%s", stream_url.c_str(), vhost.c_str());
    }
    
    return pool[stream_url];
}

SrsSource::SrsSource(SrsRequest* _req)
{
    req = _req->copy();
    
#ifdef SRS_AUTO_HLS
    hls = new SrsHls(this);
#endif
#ifdef SRS_AUTO_DVR
    dvr = new SrsDvr(this);
#endif
#ifdef SRS_AUTO_TRANSCODE
    encoder = new SrsEncoder();
#endif
    
    cache_metadata = cache_sh_video = cache_sh_audio = NULL;
    
    frame_rate = sample_rate = 0;
    _can_publish = true;
    
    gop_cache = new SrsGopCache();
    
    _srs_config->subscribe(this);
    atc = _srs_config->get_atc(req->vhost);
}

SrsSource::~SrsSource()
{
    _srs_config->unsubscribe(this);
    
    if (true) {
        std::vector<SrsConsumer*>::iterator it;
        for (it = consumers.begin(); it != consumers.end(); ++it) {
            SrsConsumer* consumer = *it;
            srs_freep(consumer);
        }
        consumers.clear();
    }

    if (true) {
        std::vector<SrsForwarder*>::iterator it;
        for (it = forwarders.begin(); it != forwarders.end(); ++it) {
            SrsForwarder* forwarder = *it;
            srs_freep(forwarder);
        }
        forwarders.clear();
    }
    
    srs_freep(cache_metadata);
    srs_freep(cache_sh_video);
    srs_freep(cache_sh_audio);
    
    srs_freep(gop_cache);
    
#ifdef SRS_AUTO_HLS
    srs_freep(hls);
#endif
#ifdef SRS_AUTO_DVR
    srs_freep(dvr);
#endif
#ifdef SRS_AUTO_TRANSCODE
    srs_freep(encoder);
#endif

    srs_freep(req);
}

int SrsSource::on_reload_vhost_atc(string vhost)
{
    int ret = ERROR_SUCCESS;
    
    if (req->vhost != vhost) {
        return ret;
    }
    
    // atc changed.
    bool enabled_atc = _srs_config->get_atc(vhost);
    
    srs_warn("vhost %s atc changed to %d, connected client may corrupt.", 
        vhost.c_str(), enabled_atc);
    
    gop_cache->clear();
    
    return ret;
}

int SrsSource::on_reload_vhost_gop_cache(string vhost)
{
    int ret = ERROR_SUCCESS;
    
    if (req->vhost != vhost) {
        return ret;
    }
    
    // gop cache changed.
    bool enabled_cache = _srs_config->get_gop_cache(vhost);
    
    srs_trace("vhost %s gop_cache changed to %d, source url=%s", 
        vhost.c_str(), enabled_cache, req->get_stream_url().c_str());
    
    set_cache(enabled_cache);
    
    return ret;
}

int SrsSource::on_reload_vhost_queue_length(string vhost)
{
    int ret = ERROR_SUCCESS;
    
    if (req->vhost != vhost) {
        return ret;
    }

    double queue_size = _srs_config->get_queue_length(req->vhost);
    
    if (true) {
        std::vector<SrsConsumer*>::iterator it;
        
        for (it = consumers.begin(); it != consumers.end(); ++it) {
            SrsConsumer* consumer = *it;
            consumer->set_queue_size(queue_size);
        }

        srs_trace("consumers reload queue size success.");
    }
    
    if (true) {
        std::vector<SrsForwarder*>::iterator it;
        
        for (it = forwarders.begin(); it != forwarders.end(); ++it) {
            SrsForwarder* forwarder = *it;
            forwarder->set_queue_size(queue_size);
        }

        srs_trace("forwarders reload queue size success.");
    }
    
    return ret;
}

int SrsSource::on_reload_vhost_forward(string vhost)
{
    int ret = ERROR_SUCCESS;
    
    if (req->vhost != vhost) {
        return ret;
    }

    // forwarders
    destroy_forwarders();
    if ((ret = create_forwarders()) != ERROR_SUCCESS) {
        srs_error("create forwarders failed. ret=%d", ret);
        return ret;
    }

    srs_trace("vhost %s forwarders reload success", vhost.c_str());
    
    return ret;
}

int SrsSource::on_reload_vhost_hls(string vhost)
{
    int ret = ERROR_SUCCESS;
    
    if (req->vhost != vhost) {
        return ret;
    }
    
#ifdef SRS_AUTO_HLS
    hls->on_unpublish();
    if ((ret = hls->on_publish(req)) != ERROR_SUCCESS) {
        srs_error("hls publish failed. ret=%d", ret);
        return ret;
    }
    srs_trace("vhost %s hls reload success", vhost.c_str());
#endif
    
    return ret;
}

int SrsSource::on_reload_vhost_dvr(string vhost)
{
    int ret = ERROR_SUCCESS;
    
    if (req->vhost != vhost) {
        return ret;
    }
    
#ifdef SRS_AUTO_DVR
    dvr->on_unpublish();
    if ((ret = dvr->on_publish(req)) != ERROR_SUCCESS) {
        srs_error("dvr publish failed. ret=%d", ret);
        return ret;
    }
    srs_trace("vhost %s dvr reload success", vhost.c_str());
#endif
    
    return ret;
}

int SrsSource::on_reload_vhost_transcode(string vhost)
{
    int ret = ERROR_SUCCESS;
    
    if (req->vhost != vhost) {
        return ret;
    }
    
#ifdef SRS_AUTO_TRANSCODE
    encoder->on_unpublish();
    if ((ret = encoder->on_publish(req)) != ERROR_SUCCESS) {
        srs_error("start encoder failed. ret=%d", ret);
        return ret;
    }
    srs_trace("vhost %s transcode reload success", vhost.c_str());
#endif
    
    return ret;
}

int SrsSource::on_forwarder_start(SrsForwarder* forwarder)
{
    int ret = ERROR_SUCCESS;
        
    // feed the forwarder the metadata/sequence header,
    // when reload to enable the forwarder.
    if (cache_metadata && (ret = forwarder->on_meta_data(cache_metadata->copy())) != ERROR_SUCCESS) {
        srs_error("forwarder process onMetaData message failed. ret=%d", ret);
        return ret;
    }
    if (cache_sh_video && (ret = forwarder->on_video(cache_sh_video->copy())) != ERROR_SUCCESS) {
        srs_error("forwarder process video sequence header message failed. ret=%d", ret);
        return ret;
    }
    if (cache_sh_audio && (ret = forwarder->on_audio(cache_sh_audio->copy())) != ERROR_SUCCESS) {
        srs_error("forwarder process audio sequence header message failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsSource::on_hls_start()
{
    int ret = ERROR_SUCCESS;
    
#ifdef SRS_AUTO_HLS
    // feed the hls the metadata/sequence header,
    // when reload to start hls, hls will never get the sequence header in stream,
    // use the SrsSource.on_hls_start to push the sequence header to HLS.
    // TODO: maybe need to decode the metadata?
    if (cache_sh_video && (ret = hls->on_video(cache_sh_video->copy())) != ERROR_SUCCESS) {
        srs_error("hls process video sequence header message failed. ret=%d", ret);
        return ret;
    }
    if (cache_sh_audio && (ret = hls->on_audio(cache_sh_audio->copy())) != ERROR_SUCCESS) {
        srs_error("hls process audio sequence header message failed. ret=%d", ret);
        return ret;
    }
#endif
    
    return ret;
}

int SrsSource::on_dvr_start()
{
    int ret = ERROR_SUCCESS;
    
#ifdef SRS_AUTO_DVR
    // feed the dvr the metadata/sequence header,
    // when reload to start dvr, dvr will never get the sequence header in stream,
    // use the SrsSource.on_dvr_start to push the sequence header to DVR.
    if (cache_metadata) {
        char* payload = (char*)cache_metadata->payload;
        int size = (int)cache_metadata->size;
        
        SrsStream stream;
        if ((ret = stream.initialize(payload, size)) != ERROR_SUCCESS) {
            srs_error("dvr decode metadata stream failed. ret=%d", ret);
            return ret;
        }
        
        SrsOnMetaDataPacket pkt;
        if ((ret = pkt.decode(&stream)) != ERROR_SUCCESS) {
            srs_error("dvr decode metadata packet failed.");
            return ret;
        }
        
        if ((ret = dvr->on_meta_data(&pkt)) != ERROR_SUCCESS) {
            srs_error("dvr process onMetaData message failed. ret=%d", ret);
            return ret;
        }
    }
    
    if (cache_sh_video && (ret = dvr->on_video(cache_sh_video->copy())) != ERROR_SUCCESS) {
        srs_error("dvr process video sequence header message failed. ret=%d", ret);
        return ret;
    }
    if (cache_sh_audio && (ret = dvr->on_audio(cache_sh_audio->copy())) != ERROR_SUCCESS) {
        srs_error("dvr process audio sequence header message failed. ret=%d", ret);
        return ret;
    }
#endif
    
    return ret;
}

bool SrsSource::can_publish()
{
    return _can_publish;
}

int SrsSource::on_meta_data(SrsCommonMessage* msg, SrsOnMetaDataPacket* metadata)
{
    int ret = ERROR_SUCCESS;
    
#ifdef SRS_AUTO_HLS
    if (metadata && (ret = hls->on_meta_data(metadata->metadata)) != ERROR_SUCCESS) {
        srs_error("hls process onMetaData message failed. ret=%d", ret);
        return ret;
    }
#endif
    
#ifdef SRS_AUTO_DVR
    if (metadata && (ret = dvr->on_meta_data(metadata)) != ERROR_SUCCESS) {
        srs_error("dvr process onMetaData message failed. ret=%d", ret);
        return ret;
    }
#endif
    
    metadata->metadata->set("server", SrsAmf0Any::str(RTMP_SIG_SRS_KEY" "RTMP_SIG_SRS_VERSION" ("RTMP_SIG_SRS_URL_SHORT")"));
    metadata->metadata->set("contributor", SrsAmf0Any::str(RTMP_SIG_SRS_PRIMARY_AUTHROS));
    
    SrsAmf0Any* prop = NULL;
    if ((prop = metadata->metadata->get_property("audiosamplerate")) != NULL) {
        if (prop->is_number()) {
            sample_rate = (int)prop->to_number();
        }
    }
    if ((prop = metadata->metadata->get_property("framerate")) != NULL) {
        if (prop->is_number()) {
            frame_rate = (int)prop->to_number();
        }
    }
    
    // encode the metadata to payload
    int size = metadata->get_payload_length();
    if (size <= 0) {
        srs_warn("ignore the invalid metadata. size=%d", size);
        return ret;
    }
    srs_verbose("get metadata size success.");
    
    char* payload = NULL;
    if ((ret = metadata->encode(size, payload)) != ERROR_SUCCESS) {
        srs_error("encode metadata error. ret=%d", ret);
        srs_freepa(payload);
        return ret;
    }
    srs_verbose("encode metadata success.");
    
    // create a shared ptr message.
    srs_freep(cache_metadata);
    cache_metadata = new SrsSharedPtrMessage();
    
    // dump message to shared ptr message.
    if ((ret = cache_metadata->initialize(&msg->header, payload, size)) != ERROR_SUCCESS) {
        srs_error("initialize the cache metadata failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("initialize shared ptr metadata success.");
    
    // copy to all consumer
    if (true) {
        std::vector<SrsConsumer*>::iterator it;
        for (it = consumers.begin(); it != consumers.end(); ++it) {
            SrsConsumer* consumer = *it;
            if ((ret = consumer->enqueue(cache_metadata->copy(), sample_rate, frame_rate)) != ERROR_SUCCESS) {
                srs_error("dispatch the metadata failed. ret=%d", ret);
                return ret;
            }
        }
        srs_trace("dispatch metadata success.");
    }
    
    // copy to all forwarders
    if (true) {
        std::vector<SrsForwarder*>::iterator it;
        for (it = forwarders.begin(); it != forwarders.end(); ++it) {
            SrsForwarder* forwarder = *it;
            if ((ret = forwarder->on_meta_data(cache_metadata->copy())) != ERROR_SUCCESS) {
                srs_error("forwarder process onMetaData message failed. ret=%d", ret);
                return ret;
            }
        }
    }
    
    return ret;
}

int SrsSource::on_audio(SrsCommonMessage* audio)
{
    int ret = ERROR_SUCCESS;
    
    SrsSharedPtrMessage* msg = new SrsSharedPtrMessage();
    SrsAutoFree(SrsSharedPtrMessage, msg, false);
    if ((ret = msg->initialize(audio)) != ERROR_SUCCESS) {
        srs_error("initialize the audio failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("initialize shared ptr audio success.");
    
#ifdef SRS_AUTO_HLS
    if ((ret = hls->on_audio(msg->copy())) != ERROR_SUCCESS) {
        srs_warn("hls process audio message failed, ignore and disable hls. ret=%d", ret);
        
        // unpublish, ignore ret.
        hls->on_unpublish();
        
        // ignore.
        ret = ERROR_SUCCESS;
    }
#endif
    
#ifdef SRS_AUTO_DVR
    if ((ret = dvr->on_audio(msg->copy())) != ERROR_SUCCESS) {
        srs_warn("dvr process audio message failed, ignore and disable dvr. ret=%d", ret);
        
        // unpublish, ignore ret.
        dvr->on_unpublish();
        
        // ignore.
        ret = ERROR_SUCCESS;
    }
#endif
    
    // copy to all consumer
    if (true) {
        std::vector<SrsConsumer*>::iterator it;
        for (it = consumers.begin(); it != consumers.end(); ++it) {
            SrsConsumer* consumer = *it;
            if ((ret = consumer->enqueue(msg->copy(), sample_rate, frame_rate)) != ERROR_SUCCESS) {
                srs_error("dispatch the audio failed. ret=%d", ret);
                return ret;
            }
        }
        srs_info("dispatch audio success.");
    }

    // copy to all forwarders.
    if (true) {
        std::vector<SrsForwarder*>::iterator it;
        for (it = forwarders.begin(); it != forwarders.end(); ++it) {
            SrsForwarder* forwarder = *it;
            if ((ret = forwarder->on_audio(msg->copy())) != ERROR_SUCCESS) {
                srs_error("forwarder process audio message failed. ret=%d", ret);
                return ret;
            }
        }
    }

    // cache the sequence header if h264
    if (SrsCodec::audio_is_sequence_header(msg->payload, msg->size)) {
        srs_freep(cache_sh_audio);
        cache_sh_audio = msg->copy();
        srs_trace("update audio sequence header success. size=%d", msg->header.payload_length);
        return ret;
    }
    
    // cache the last gop packets
    if ((ret = gop_cache->cache(msg)) != ERROR_SUCCESS) {
        srs_error("shrink gop cache failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("cache gop success.");
    
    // if atc, update the sequence header to abs time.
    if (atc) {
        if (cache_sh_audio) {
            cache_sh_audio->header.timestamp = msg->header.timestamp;
        }
        if (cache_metadata) {
            cache_metadata->header.timestamp = msg->header.timestamp;
        }
    }
    
    return ret;
}

int SrsSource::on_video(SrsCommonMessage* video)
{
    int ret = ERROR_SUCCESS;
    
    SrsSharedPtrMessage* msg = new SrsSharedPtrMessage();
    SrsAutoFree(SrsSharedPtrMessage, msg, false);
    if ((ret = msg->initialize(video)) != ERROR_SUCCESS) {
        srs_error("initialize the video failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("initialize shared ptr video success.");
    
#ifdef SRS_AUTO_HLS
    if ((ret = hls->on_video(msg->copy())) != ERROR_SUCCESS) {
        srs_warn("hls process video message failed, ignore and disable hls. ret=%d", ret);
        
        // unpublish, ignore ret.
        hls->on_unpublish();
        
        // ignore.
        ret = ERROR_SUCCESS;
    }
#endif
    
#ifdef SRS_AUTO_DVR
    if ((ret = dvr->on_video(msg->copy())) != ERROR_SUCCESS) {
        srs_warn("dvr process video message failed, ignore and disable dvr. ret=%d", ret);
        
        // unpublish, ignore ret.
        dvr->on_unpublish();
        
        // ignore.
        ret = ERROR_SUCCESS;
    }
#endif
    
    // copy to all consumer
    if (true) {
        std::vector<SrsConsumer*>::iterator it;
        for (it = consumers.begin(); it != consumers.end(); ++it) {
            SrsConsumer* consumer = *it;
            if ((ret = consumer->enqueue(msg->copy(), sample_rate, frame_rate)) != ERROR_SUCCESS) {
                srs_error("dispatch the video failed. ret=%d", ret);
                return ret;
            }
        }
        srs_info("dispatch video success.");
    }

    // copy to all forwarders.
    if (true) {
        std::vector<SrsForwarder*>::iterator it;
        for (it = forwarders.begin(); it != forwarders.end(); ++it) {
            SrsForwarder* forwarder = *it;
            if ((ret = forwarder->on_video(msg->copy())) != ERROR_SUCCESS) {
                srs_error("forwarder process video message failed. ret=%d", ret);
                return ret;
            }
        }
    }

    // cache the sequence header if h264
    if (SrsCodec::video_is_sequence_header(msg->payload, msg->size)) {
        srs_freep(cache_sh_video);
        cache_sh_video = msg->copy();
        srs_trace("update video sequence header success. size=%d", msg->header.payload_length);
        return ret;
    }

    // cache the last gop packets
    if ((ret = gop_cache->cache(msg)) != ERROR_SUCCESS) {
        srs_error("gop cache msg failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("cache gop success.");
    
    // if atc, update the sequence header to abs time.
    if (atc) {
        if (cache_sh_video) {
            cache_sh_video->header.timestamp = msg->header.timestamp;
        }
        if (cache_metadata) {
            cache_metadata->header.timestamp = msg->header.timestamp;
        }
    }
    
    return ret;
}

int SrsSource::on_publish(SrsRequest* _req)
{
    int ret = ERROR_SUCCESS;
    
    // update the request object.
    srs_freep(req);
    req = _req->copy();
    srs_assert(req);
    
    _can_publish = false;
    
    // create forwarders
    if ((ret = create_forwarders()) != ERROR_SUCCESS) {
        srs_error("create forwarders failed. ret=%d", ret);
        return ret;
    }
    
#ifdef SRS_AUTO_TRANSCODE
    if ((ret = encoder->on_publish(req)) != ERROR_SUCCESS) {
        srs_error("start encoder failed. ret=%d", ret);
        return ret;
    }
#endif
    
#ifdef SRS_AUTO_HLS
    if ((ret = hls->on_publish(req)) != ERROR_SUCCESS) {
        srs_error("start hls failed. ret=%d", ret);
        return ret;
    }
#endif
    
#ifdef SRS_AUTO_DVR
    if ((ret = dvr->on_publish(req)) != ERROR_SUCCESS) {
        srs_error("start dvr failed. ret=%d", ret);
        return ret;
    }
#endif

    return ret;
}

void SrsSource::on_unpublish()
{
    // destroy all forwarders
    destroy_forwarders();

#ifdef SRS_AUTO_TRANSCODE
    encoder->on_unpublish();
#endif

#ifdef SRS_AUTO_HLS
    hls->on_unpublish();
#endif
    
#ifdef SRS_AUTO_DVR
    dvr->on_unpublish();
#endif

    gop_cache->clear();

    srs_freep(cache_metadata);
    frame_rate = sample_rate = 0;
    
    srs_freep(cache_sh_video);
    srs_freep(cache_sh_audio);
    
    srs_trace("clear cache/metadata/sequence-headers when unpublish.");
    
    _can_publish = true;
}

 int SrsSource::create_consumer(SrsConsumer*& consumer)
{
    int ret = ERROR_SUCCESS;
    
    consumer = new SrsConsumer(this);
    consumers.push_back(consumer);
    
    double queue_size = _srs_config->get_queue_length(req->vhost);
    consumer->set_queue_size(queue_size);

    if (cache_metadata && (ret = consumer->enqueue(cache_metadata->copy(), sample_rate, frame_rate)) != ERROR_SUCCESS) {
        srs_error("dispatch metadata failed. ret=%d", ret);
        return ret;
    }
    srs_info("dispatch metadata success");
    
    // if atc, update the sequence header to gop cache time.
    if (atc && !gop_cache->empty()) {
        if (cache_sh_video) {
            cache_sh_video->header.timestamp = gop_cache->get_start_time();
        }
        if (cache_sh_audio) {
            cache_sh_audio->header.timestamp = gop_cache->get_start_time();
        }
    }
    
    // copy sequence header
    if (cache_sh_video && (ret = consumer->enqueue(cache_sh_video->copy(), sample_rate, frame_rate)) != ERROR_SUCCESS) {
        srs_error("dispatch video sequence header failed. ret=%d", ret);
        return ret;
    }
    srs_info("dispatch video sequence header success");
    
    if (cache_sh_audio && (ret = consumer->enqueue(cache_sh_audio->copy(), sample_rate, frame_rate)) != ERROR_SUCCESS) {
        srs_error("dispatch audio sequence header failed. ret=%d", ret);
        return ret;
    }
    srs_info("dispatch audio sequence header success");
    
    // copy gop cache to client.
    if ((ret = gop_cache->dump(consumer, sample_rate, frame_rate)) != ERROR_SUCCESS) {
        return ret;
    }
    
    srs_trace("create consumer, queue_size=%.2f, tba=%d, tbv=%d", queue_size, sample_rate, frame_rate);
    
    return ret;
}

void SrsSource::on_consumer_destroy(SrsConsumer* consumer)
{
    std::vector<SrsConsumer*>::iterator it;
    it = std::find(consumers.begin(), consumers.end(), consumer);
    if (it != consumers.end()) {
        consumers.erase(it);
    }
    srs_info("handle consumer destroy success.");
}

void SrsSource::set_cache(bool enabled)
{
    gop_cache->set(enabled);
}

bool SrsSource::is_atc()
{
    return atc;
}

int SrsSource::create_forwarders()
{
    int ret = ERROR_SUCCESS;
    
    SrsConfDirective* conf = _srs_config->get_forward(req->vhost);
    for (int i = 0; conf && i < (int)conf->args.size(); i++) {
        std::string forward_server = conf->args.at(i);
        
        SrsForwarder* forwarder = new SrsForwarder(this);
        forwarders.push_back(forwarder);
    
        double queue_size = _srs_config->get_queue_length(req->vhost);
        forwarder->set_queue_size(queue_size);
        
        if ((ret = forwarder->on_publish(req, forward_server)) != ERROR_SUCCESS) {
            srs_error("start forwarder failed. "
                "vhost=%s, app=%s, stream=%s, forward-to=%s",
                req->vhost.c_str(), req->app.c_str(), req->stream.c_str(),
                forward_server.c_str());
            return ret;
        }
    }

    return ret;
}

void SrsSource::destroy_forwarders()
{
    std::vector<SrsForwarder*>::iterator it;
    for (it = forwarders.begin(); it != forwarders.end(); ++it) {
        SrsForwarder* forwarder = *it;
        forwarder->on_unpublish();
        srs_freep(forwarder);
    }
    forwarders.clear();
}

