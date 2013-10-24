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

#ifndef SRS_CORE_SOURCE_HPP
#define SRS_CORE_SOURCE_HPP

/*
#include <srs_core_source.hpp>
*/

#include <srs_core.hpp>

#include <map>
#include <vector>
#include <string>

class SrsCodec;
class SrsSource;
class SrsCommonMessage;
class SrsOnMetaDataPacket;
class SrsSharedPtrMessage;

/**
* the consumer for SrsSource, that is a play client.
*/
class SrsConsumer
{
private:
	int32_t last_pkt_time;
	int32_t last_pkt_correct_time;
	SrsSource* source;
	std::vector<SrsSharedPtrMessage*> msgs;
public:
	SrsConsumer(SrsSource* _source);
	virtual ~SrsConsumer();
public:
	/**
	* enqueue an shared ptr message.
	*/
	virtual int enqueue(SrsSharedPtrMessage* msg);
	/**
	* get packets in consumer queue.
	* @pmsgs SrsMessages*[], output the prt array.
	* @count the count in array.
	* @max_count the max count to dequeue, 0 to dequeue all.
	*/
	virtual int get_packets(int max_count, SrsSharedPtrMessage**& pmsgs, int& count);
};

/**
* live streaming source.
*/
class SrsSource
{
private:
	static std::map<std::string, SrsSource*> pool;
public:
	/**
	* find stream by vhost/app/stream.
	* @stream_url the stream url, for example, myserver.xxx.com/app/stream
	* @return the matched source, never be NULL.
	* @remark stream_url should without port and schema.
	*/
	static SrsSource* find(std::string stream_url);
private:
	SrsCodec* codec;
	std::string stream_url;
	std::vector<SrsConsumer*> consumers;
private:
	SrsSharedPtrMessage* cache_metadata;
	// the cached video sequence header.
	SrsSharedPtrMessage* cache_sh_video;
	// the cached audio sequence header.
	SrsSharedPtrMessage* cache_sh_audio;
public:
	SrsSource(std::string _stream_url);
	virtual ~SrsSource();
public:
	virtual int on_meta_data(SrsCommonMessage* msg, SrsOnMetaDataPacket* metadata);
	virtual int on_audio(SrsCommonMessage* audio);
	virtual int on_video(SrsCommonMessage* video);
public:
	virtual int create_consumer(SrsConsumer*& consumer);
	virtual void on_consumer_destroy(SrsConsumer* consumer);
};

#endif