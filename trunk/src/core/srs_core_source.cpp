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

#include <srs_core_log.hpp>
#include <srs_core_protocol.hpp>
#include <srs_core_auto_free.hpp>
#include <srs_core_amf0.hpp>

#define CONST_MAX_JITTER_MS 500
#define DEFAULT_FRAME_TIME_MS 10

std::map<std::string, SrsSource*> SrsSource::pool;

SrsSource* SrsSource::find(std::string stream_url)
{
	if (pool.find(stream_url) == pool.end()) {
		pool[stream_url] = new SrsSource(stream_url);
		srs_verbose("create new source for url=%s", stream_url.c_str());
	}
	
	return pool[stream_url];
}

SrsConsumer::SrsConsumer(SrsSource* _source)
{
	source = _source;
	last_pkt_correct_time = last_pkt_time = 0;
}

SrsConsumer::~SrsConsumer()
{
	std::vector<SrsSharedPtrMessage*>::iterator it;
	for (it = msgs.begin(); it != msgs.end(); ++it) {
		SrsSharedPtrMessage* msg = *it;
		srs_freep(msg);
	}
	msgs.clear();
	
	source->on_consumer_destroy(this);
}

int SrsConsumer::enqueue(SrsSharedPtrMessage* msg)
{
	int ret = ERROR_SUCCESS;
	
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
	int32_t time = msg->header.timestamp;
	int32_t delta = time - last_pkt_time;

	// if jitter detected, reset the delta.
	if (delta < 0 || delta > CONST_MAX_JITTER_MS) {
		delta = DEFAULT_FRAME_TIME_MS;
		
		srs_info("jitter detected, delta=%d, last_pkt=%d, time=%d, correct_to=%d",
			delta, last_pkt_time, time, last_pkt_correct_time + delta);
	} else {
		srs_verbose("timestamp no jitter. time=%d, last_pkt=%d, correct_to=%d", 
			time, last_pkt_time, last_pkt_correct_time + delta);
	}
	
	last_pkt_correct_time = srs_max(0, last_pkt_correct_time + delta);
	msg->header.timestamp = last_pkt_correct_time;
	last_pkt_time = time;
	
	msgs.push_back(msg);
	
	return ret;
}

int SrsConsumer::get_packets(int max_count, SrsSharedPtrMessage**& pmsgs, int& count)
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

SrsSource::SrsSource(std::string _stream_url)
{
	stream_url = _stream_url;
	cache_metadata = NULL;
	cache_sh_video = NULL;
	cache_sh_audio = NULL;
}

SrsSource::~SrsSource()
{
	std::vector<SrsConsumer*>::iterator it;
	for (it = consumers.begin(); it != consumers.end(); ++it) {
		SrsConsumer* consumer = *it;
		srs_freep(consumer);
	}
	consumers.clear();
	
	srs_freep(cache_metadata);
	srs_freep(cache_sh_video);
	srs_freep(cache_sh_audio);
}

int SrsSource::on_meta_data(SrsCommonMessage* msg, SrsOnMetaDataPacket* metadata)
{
	int ret = ERROR_SUCCESS;
	
	metadata->metadata->set("server", 
		new SrsAmf0String(RTMP_SIG_SRS_NAME""RTMP_SIG_SRS_VERSION));
	
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
	std::vector<SrsConsumer*>::iterator it;
	for (it = consumers.begin(); it != consumers.end(); ++it) {
		SrsConsumer* consumer = *it;
		if ((ret = consumer->enqueue(cache_metadata->copy())) != ERROR_SUCCESS) {
			srs_error("dispatch the metadata failed. ret=%d", ret);
			return ret;
		}
	}
	srs_trace("dispatch metadata success.");
	
	return ret;
}

int SrsSource::on_audio(SrsCommonMessage* audio)
{
	int ret = ERROR_SUCCESS;
	
	SrsSharedPtrMessage* msg = new SrsSharedPtrMessage();
	SrsAutoFree(SrsSharedPtrMessage, msg, false);
	if ((ret = msg->initialize(audio, (char*)audio->payload, audio->size)) != ERROR_SUCCESS) {
		srs_error("initialize the audio failed. ret=%d", ret);
		return ret;
	}
	srs_verbose("initialize shared ptr audio success.");
	
	// detach the original audio
	audio->payload = NULL;
	audio->size = 0;
	
	// copy to all consumer
	std::vector<SrsConsumer*>::iterator it;
	for (it = consumers.begin(); it != consumers.end(); ++it) {
		SrsConsumer* consumer = *it;
		if ((ret = consumer->enqueue(msg->copy())) != ERROR_SUCCESS) {
			srs_error("dispatch the audio failed. ret=%d", ret);
			return ret;
		}
	}
	srs_info("dispatch audio success.");

	// TODO: always update the sh.
	// TODO: cache last gop.
	if (!cache_sh_audio) {
		srs_freep(cache_sh_audio);
		cache_sh_audio = msg->copy();
	}
	
	return ret;
}

int SrsSource::on_video(SrsCommonMessage* video)
{
	int ret = ERROR_SUCCESS;
	
	SrsSharedPtrMessage* msg = new SrsSharedPtrMessage();
	SrsAutoFree(SrsSharedPtrMessage, msg, false);
	if ((ret = msg->initialize(video, (char*)video->payload, video->size)) != ERROR_SUCCESS) {
		srs_error("initialize the video failed. ret=%d", ret);
		return ret;
	}
	srs_verbose("initialize shared ptr video success.");
	
	// detach the original audio
	video->payload = NULL;
	video->size = 0;
	
	// copy to all consumer
	std::vector<SrsConsumer*>::iterator it;
	for (it = consumers.begin(); it != consumers.end(); ++it) {
		SrsConsumer* consumer = *it;
		if ((ret = consumer->enqueue(msg->copy())) != ERROR_SUCCESS) {
			srs_error("dispatch the video failed. ret=%d", ret);
			return ret;
		}
	}
	srs_info("dispatch video success.");

	// TODO: always update the sh.
	// TODO: cache last gop.
	if (!cache_sh_video) {
		srs_freep(cache_sh_video);
		cache_sh_video = msg->copy();
	}
	
	return ret;
}

 int SrsSource::create_consumer(SrsConsumer*& consumer)
{
	int ret = ERROR_SUCCESS;
	
	consumer = new SrsConsumer(this);
	consumers.push_back(consumer);

	if (cache_metadata && (ret = consumer->enqueue(cache_metadata->copy())) != ERROR_SUCCESS) {
		srs_error("dispatch metadata failed. ret=%d", ret);
		return ret;
	}
	srs_info("dispatch metadata success");
	
	if (cache_sh_video && (ret = consumer->enqueue(cache_sh_video->copy())) != ERROR_SUCCESS) {
		srs_error("dispatch video sequence header failed. ret=%d", ret);
		return ret;
	}
	srs_info("dispatch video sequence header success");
	
	if (cache_sh_audio && (ret = consumer->enqueue(cache_sh_audio->copy())) != ERROR_SUCCESS) {
		srs_error("dispatch audio sequence header failed. ret=%d", ret);
		return ret;
	}
	srs_info("dispatch audio sequence header success");
	
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

