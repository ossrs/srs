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

int SrsConsumer::get_packets(int max_count, SrsMessage**& msgs, int& count)
{
	msgs = NULL;
	count = 0;
	
	int ret = ERROR_SUCCESS;
	return ret;
}

SrsSource::SrsSource(std::string _stream_url)
{
	stream_url = _stream_url;
}

SrsSource::~SrsSource()
{
}

int SrsSource::on_meta_data(SrsMessage* msg, SrsOnMetaDataPacket* metadata)
{
	int ret = ERROR_SUCCESS;
	
	metadata->metadata->set("server", 
		new SrsAmf0String(RTMP_SIG_SRS_NAME""RTMP_SIG_SRS_VERSION));
	
	return ret;
}

int SrsSource::on_audio(SrsMessage* audio)
{
	int ret = ERROR_SUCCESS;
	return ret;
}

int SrsSource::on_video(SrsMessage* audio)
{
	int ret = ERROR_SUCCESS;
	return ret;
}

SrsConsumer* SrsSource::create_consumer()
{
	SrsConsumer* consumer = new SrsConsumer();
	return consumer;
}

