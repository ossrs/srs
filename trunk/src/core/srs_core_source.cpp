/*
The MIT License (MIT)

Copyright (c) 2013 winlin

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

#include <srs_core_source.hpp>

#include <algorithm>
using namespace std;

#include <srs_core_log.hpp>
#include <srs_core_protocol.hpp>
#include <srs_core_autofree.hpp>
#include <srs_core_amf0.hpp>
#include <srs_core_codec.hpp>
#include <srs_core_hls.hpp>
#include <srs_core_forward.hpp>
#include <srs_core_config.hpp>
#include <srs_core_encoder.hpp>
#include <srs_core_rtmp.hpp>

#define CONST_MAX_JITTER_MS 		500
#define DEFAULT_FRAME_TIME_MS 		10
#define PAUSED_SHRINK_SIZE			250

SrsRtmpJitter::SrsRtmpJitter()
{
	last_pkt_correct_time = last_pkt_time = 0;
}

SrsRtmpJitter::~SrsRtmpJitter()
{
}

int SrsRtmpJitter::correct(SrsSharedPtrMessage* msg, int tba, int tbv, int64_t* corrected_time)
{
	int ret = ERROR_SUCCESS;
	
	int sample_rate = tba;
	int frame_rate = tbv;
	
	/**
	* we use a very simple time jitter detect/correct algorithm:
	* 1. delta: ensure the delta is positive and valid,
	* 	we set the delta to DEFAULT_FRAME_TIME_MS,
	* 	if the delta of time is nagative or greater than CONST_MAX_JITTER_MS.
	* 2. last_pkt_time: specifies the original packet time,
	* 	is used to detect next jitter.
	* 3. last_pkt_correct_time: simply add the positive delta, 
	* 	and enforce the time monotonically.
	*/
	u_int32_t time = msg->header.timestamp;
	int32_t delta = time - last_pkt_time;

	// if jitter detected, reset the delta.
	if (delta < 0 || delta > CONST_MAX_JITTER_MS) {
		// calc the right diff by audio sample rate
		if (msg->header.is_audio() && sample_rate > 0) {
			delta = (int32_t)(delta * 1000.0 / sample_rate);
		} else if (msg->header.is_video() && frame_rate > 0) {
			delta = (int32_t)(delta * 1.0 / frame_rate);
		} else {
			delta = DEFAULT_FRAME_TIME_MS;
		}

		// sometimes, the time is absolute time, so correct it again.
		if (delta < 0 || delta > CONST_MAX_JITTER_MS) {
			delta = DEFAULT_FRAME_TIME_MS;
		}
		
		srs_info("jitter detected, last_pts=%d, pts=%d, diff=%d, last_time=%d, time=%d, diff=%d",
			last_pkt_time, time, time - last_pkt_time, last_pkt_correct_time, last_pkt_correct_time + delta, delta);
	} else {
		srs_verbose("timestamp no jitter. time=%d, last_pkt=%d, correct_to=%d", 
			time, last_pkt_time, last_pkt_correct_time + delta);
	}
	
	last_pkt_correct_time = srs_max(0, last_pkt_correct_time + delta);
	
	if (corrected_time) {
		*corrected_time = last_pkt_correct_time;
	}
	msg->header.timestamp = last_pkt_correct_time;
	
	last_pkt_time = time;
	
	return ret;
}

int SrsRtmpJitter::get_time()
{
	return (int)last_pkt_correct_time;
}

SrsConsumer::SrsConsumer(SrsSource* _source)
{
	source = _source;
	paused = false;
	jitter = new SrsRtmpJitter();
}

SrsConsumer::~SrsConsumer()
{
	clear();
	
	source->on_consumer_destroy(this);
	srs_freep(jitter);
}

int SrsConsumer::get_time()
{
	return jitter->get_time();
}

int SrsConsumer::enqueue(SrsSharedPtrMessage* msg, int tba, int tbv)
{
	int ret = ERROR_SUCCESS;
	
	if ((ret = jitter->correct(msg, tba, tbv)) != ERROR_SUCCESS) {
		srs_freep(msg);
		return ret;
	}
	
	// TODO: check the queue size and drop packets if overflow.
	msgs.push_back(msg);
	
	return ret;
}

int SrsConsumer::get_packets(int max_count, SrsSharedPtrMessage**& pmsgs, int& count)
{
	int ret = ERROR_SUCCESS;
	
	if (msgs.empty()) {
		return ret;
	}

	if (paused) {
		if ((int)msgs.size() >= PAUSED_SHRINK_SIZE) {
			shrink();
		}
		return ret;
	}
	
	if (max_count == 0) {
		count = (int)msgs.size();
	} else {
		count = srs_min(max_count, (int)msgs.size());
	}
	
	pmsgs = new SrsSharedPtrMessage*[count];
	
	for (int i = 0; i < count; i++) {
		pmsgs[i] = msgs[i];
	}
	
	if (count == (int)msgs.size()) {
		msgs.clear();
	} else {
		msgs.erase(msgs.begin(), msgs.begin() + count);
	}
	
	return ret;
}

int SrsConsumer::on_play_client_pause(bool is_pause)
{
	int ret = ERROR_SUCCESS;
	
	srs_trace("stream consumer change pause state %d=>%d", paused, is_pause);
	paused = is_pause;
	
	return ret;
}

void SrsConsumer::shrink()
{
	int i = 0;
	std::vector<SrsSharedPtrMessage*>::iterator it;
	
	// issue the last video iframe.
	bool has_video = false;
	int frame_to_remove = 0;
	std::vector<SrsSharedPtrMessage*>::iterator iframe = msgs.end();
	for (i = 0, it = msgs.begin(); it != msgs.end(); ++it, i++) {
		SrsSharedPtrMessage* msg = *it;
		if (msg->header.is_video()) {
			has_video = true;
			if (SrsCodec::video_is_keyframe(msg->payload, msg->size)) {
				iframe = it;
				frame_to_remove = i + 1;
			}
		}
	}
	
	// last iframe is the first elem, ignore it.
	if (iframe == msgs.begin()) {
		return;
	}
	
	// recalc the frame to remove
	if (iframe == msgs.end()) {
		frame_to_remove = 0;
	}
	if (!has_video) {
		frame_to_remove = (int)msgs.size();
	}
	
	srs_trace("shrink the cache queue, has_video=%d, has_iframe=%d, size=%d, removed=%d", 
		has_video, iframe != msgs.end(), (int)msgs.size(), frame_to_remove);
	
	// if no video, remove all audio.
	if (!has_video) {
		clear();
		return;
	}
	
	// if exists video Iframe, remove the frames before it.
	if (iframe != msgs.end()) {
		for (it = msgs.begin(); it != iframe; ++it) {
			SrsSharedPtrMessage* msg = *it;
			srs_freep(msg);
		}
		msgs.erase(msgs.begin(), iframe);
	}
}

void SrsConsumer::clear()
{
	std::vector<SrsSharedPtrMessage*>::iterator it;
	for (it = msgs.begin(); it != msgs.end(); ++it) {
		SrsSharedPtrMessage* msg = *it;
		srs_freep(msg);
	}
	msgs.clear();
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

std::map<std::string, SrsSource*> SrsSource::pool;

SrsSource* SrsSource::find(string stream_url, string vhost)
{
	if (pool.find(stream_url) == pool.end()) {
		pool[stream_url] = new SrsSource(stream_url, vhost);
		srs_verbose("create new source for "
			"url=%s, vhost=%s", stream_url.c_str(), vhost.c_str());
	}
	
	return pool[stream_url];
}

SrsSource::SrsSource(string _stream_url, string _vhost)
{
	stream_url = _stream_url;
	vhost = _vhost;
	
#ifdef SRS_HLS
	hls = new SrsHls();
#endif
#ifdef SRS_FFMPEG
	encoder = new SrsEncoder();
#endif
	
	cache_metadata = cache_sh_video = cache_sh_audio = NULL;
	
	frame_rate = sample_rate = 0;
	_can_publish = true;
	
	gop_cache = new SrsGopCache();
	
	config->subscribe(this);
}

SrsSource::~SrsSource()
{
	config->unsubscribe(this);
	
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
	
#ifdef SRS_HLS
	srs_freep(hls);
#endif
#ifdef SRS_FFMPEG
	srs_freep(encoder);
#endif
}

int SrsSource::on_reload_gop_cache(string _vhost)
{
	int ret = ERROR_SUCCESS;
	
	if (vhost != _vhost) {
		return ret;
	}
	
	// gop cache changed.
	bool enabled_cache = config->get_gop_cache(vhost);
	
	srs_trace("vhost %s gop_cache changed to %d, source url=%s", 
		vhost.c_str(), enabled_cache, stream_url.c_str());
	
	set_cache(enabled_cache);
	
	return ret;
}

bool SrsSource::can_publish()
{
	return _can_publish;
}

int SrsSource::on_meta_data(SrsCommonMessage* msg, SrsOnMetaDataPacket* metadata)
{
	int ret = ERROR_SUCCESS;
	
#ifdef SRS_HLS
	if ((ret = hls->on_meta_data(metadata)) != ERROR_SUCCESS) {
		srs_error("hls process onMetaData message failed. ret=%d", ret);
		return ret;
	}
#endif
	
	metadata->metadata->set("server", new SrsAmf0String(
		RTMP_SIG_SRS_KEY" "RTMP_SIG_SRS_VERSION" ("RTMP_SIG_SRS_URL_SHORT")"));
	metadata->metadata->set("contributor", 
		new SrsAmf0String(RTMP_SIG_SRS_CONTRIBUTOR));
	
	SrsAmf0Any* prop = NULL;
	if ((prop = metadata->metadata->get_property("audiosamplerate")) != NULL) {
		if (prop->is_number()) {
			sample_rate = (int)(srs_amf0_convert<SrsAmf0Number>(prop)->value);
		}
	}
	if ((prop = metadata->metadata->get_property("framerate")) != NULL) {
		if (prop->is_number()) {
			frame_rate = (int)(srs_amf0_convert<SrsAmf0Number>(prop)->value);
		}
	}
	
	// encode the metadata to payload
	int size = metadata->get_payload_length();
	if (size <= 0) {
		srs_warn("ignore the invalid metadata. size=%d", size);
		return ret;
	}
	srs_verbose("get metadata size success.");
	
	char* payload = new char[size];
	memset(payload, 0, size);
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
	if ((ret = cache_metadata->initialize(msg, payload, size)) != ERROR_SUCCESS) {
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
	
#ifdef SRS_HLS
	if ((ret = hls->on_audio(msg->copy())) != ERROR_SUCCESS) {
		srs_warn("hls process audio message failed, ignore and disable hls. ret=%d", ret);
		
		// unpublish, ignore ret.
		hls->on_unpublish();
		
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
	
#ifdef SRS_HLS
	if ((ret = hls->on_video(msg->copy())) != ERROR_SUCCESS) {
		srs_warn("hls process video message failed, ignore and disable hls. ret=%d", ret);
		
		// unpublish, ignore ret.
		hls->on_unpublish();
		
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
		srs_error("shrink gop cache failed. ret=%d", ret);
		return ret;
	}
	srs_verbose("cache gop success.");
	
	return ret;
}

int SrsSource::on_publish(SrsRequest* req)
{
	int ret = ERROR_SUCCESS;
	
	_can_publish = false;

	// TODO: support reload.
	
	// create forwarders
	SrsConfDirective* conf = config->get_forward(req->vhost);
	for (int i = 0; conf && i < (int)conf->args.size(); i++) {
		std::string forward_server = conf->args.at(i);
		
		SrsForwarder* forwarder = new SrsForwarder();
		forwarders.push_back(forwarder);
		
		if ((ret = forwarder->on_publish(req, forward_server)) != ERROR_SUCCESS) {
			srs_error("start forwarder failed. "
				"vhost=%s, app=%s, stream=%s, forward-to=%s",
				req->vhost.c_str(), req->app.c_str(), req->stream.c_str(),
				forward_server.c_str());
			return ret;
		}
	}

#ifdef SRS_FFMPEG
	if ((ret = encoder->on_publish(req)) != ERROR_SUCCESS) {
		return ret;
	}
#endif
	
#ifdef SRS_HLS
	if ((ret = hls->on_publish(req)) != ERROR_SUCCESS) {
		return ret;
	}
#endif

	return ret;
}

void SrsSource::on_unpublish()
{
	// close all forwarders
	std::vector<SrsForwarder*>::iterator it;
	for (it = forwarders.begin(); it != forwarders.end(); ++it) {
		SrsForwarder* forwarder = *it;
		forwarder->on_unpublish();
		srs_freep(forwarder);
	}
	forwarders.clear();

#ifdef SRS_FFMPEG
	encoder->on_unpublish();
#endif

	// TODO: HLS should continue previous sequence and stream.
#ifdef SRS_HLS
	hls->on_unpublish();
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

	if (cache_metadata && (ret = consumer->enqueue(cache_metadata->copy(), sample_rate, frame_rate)) != ERROR_SUCCESS) {
		srs_error("dispatch metadata failed. ret=%d", ret);
		return ret;
	}
	srs_info("dispatch metadata success");
	
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
	
	if ((ret = gop_cache->dump(consumer, sample_rate, frame_rate)) != ERROR_SUCCESS) {
		return ret;
	}
	
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

