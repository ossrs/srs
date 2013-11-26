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

#ifndef SRS_CORE_HLS_HPP
#define SRS_CORE_HLS_HPP

/*
#include <srs_core_hls.hpp>
*/
#include <srs_core.hpp>

#include <string>
#include <vector>

class SrsOnMetaDataPacket;
class SrsSharedPtrMessage;
class SrsCodecSample;
class SrsCodecBuffer;
class SrsMpegtsFrame;
class SrsRtmpJitter;
class SrsTSMuxer;
class SrsCodec;

/**
* 3.3.2.  EXTINF
* The EXTINF tag specifies the duration of a media segment.
*/
struct SrsM3u8Segment
{
	// duration in seconds in m3u8.
	double duration;
	// sequence number in m3u8.
	int sequence_no;
	// ts uri in m3u8.
	std::string uri;
	// ts full file to write.
	std::string full_path;
	// the muxer to write ts.
	SrsTSMuxer* muxer;
	// current segment start dts for m3u8
	int64_t segment_start_dts;
	
	SrsM3u8Segment();
	virtual ~SrsM3u8Segment();
};

/**
* write m3u8 hls.
*/
class SrsHLS
{
private:
	std::string vhost;
	std::string stream;
	std::string hls_path;
private:
	int file_index;
	std::string m3u8;
private:
	/**
	* m3u8 segments.
	*/
	std::vector<SrsM3u8Segment*> segments;
	/**
	* current writing segment.
	*/
	SrsM3u8Segment* current;
	// current frame and buffer
	SrsMpegtsFrame* audio_frame;
	SrsCodecBuffer* audio_buffer;
	SrsMpegtsFrame* video_frame;
	SrsCodecBuffer* video_buffer;
	// last known dts
	int64_t stream_dts;
	// last segment dts in m3u8
	int64_t m3u8_dts;
private:
	bool hls_enabled;
	SrsCodec* codec;
	SrsCodecSample* sample;
	SrsRtmpJitter* jitter;
public:
	SrsHLS();
	virtual ~SrsHLS();
public:
	virtual int on_publish(std::string _vhost, std::string _stream);
	virtual void on_unpublish();
	virtual int on_meta_data(SrsOnMetaDataPacket* metadata);
	virtual int on_audio(SrsSharedPtrMessage* audio);
	virtual int on_video(SrsSharedPtrMessage* video);
private:
	virtual int reopen();
	virtual int refresh_m3u8();
	virtual int _refresh_m3u8(int& fd);
};

class SrsTSMuxer
{
private:
	int fd;
	std::string path;
public:
	SrsTSMuxer();
	virtual ~SrsTSMuxer();
public:
	virtual int open(std::string _path);
	virtual int write_audio(SrsMpegtsFrame* audio_frame, SrsCodecBuffer* audio_buffer, SrsCodec* codec, SrsCodecSample* sample);
	virtual int write_video(SrsMpegtsFrame* video_frame, SrsCodecBuffer* video_buffer, SrsCodec* codec, SrsCodecSample* sample);
	virtual void close();
};

#endif