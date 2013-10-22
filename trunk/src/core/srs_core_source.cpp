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

#include <srs_core_log.hpp>
#include <srs_core_protocol.hpp>
#include <srs_core_auto_free.hpp>
#include <srs_core_amf0.hpp>

std::map<std::string, SrsSource*> SrsSource::pool;

SrsSource* SrsSource::find(std::string stream_url)
{
	if (pool.find(stream_url) == pool.end()) {
		pool[stream_url] = new SrsSource(stream_url);
		srs_verbose("create new source for url=%s", stream_url.c_str());
	}
	
	return pool[stream_url];
}

SrsConsumer::SrsConsumer()
{
}

SrsConsumer::~SrsConsumer()
{
}

int SrsConsumer::enqueue(SrsSharedPtrMessage* msg)
{
	int ret = ERROR_SUCCESS;
	return ret;
}

int SrsConsumer::get_packets(int max_count, SrsCommonMessage**& msgs, int& count)
{
	msgs = NULL;
	count = 0;
	
	int ret = ERROR_SUCCESS;
	return ret;
}

SrsSource::SrsSource(std::string _stream_url)
{
	stream_url = _stream_url;
	cache_metadata = new SrsSharedPtrMessage();
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
	if ((ret = cache_metadata->initialize(&msg->header, payload, size, msg->get_perfer_cid())) != ERROR_SUCCESS) {
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
	if ((ret = msg->initialize(&audio->header, (char*)audio->payload, audio->size, audio->get_perfer_cid())) != ERROR_SUCCESS) {
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
	
	return ret;
}

int SrsSource::on_video(SrsCommonMessage* video)
{
	int ret = ERROR_SUCCESS;
	
	SrsSharedPtrMessage* msg = new SrsSharedPtrMessage();
	SrsAutoFree(SrsSharedPtrMessage, msg, false);
	if ((ret = msg->initialize(&video->header, (char*)video->payload, video->size, video->get_perfer_cid())) != ERROR_SUCCESS) {
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
	
	return ret;
}

SrsConsumer* SrsSource::create_consumer()
{
	SrsConsumer* consumer = new SrsConsumer();
	consumers.push_back(consumer);
	return consumer;
}

