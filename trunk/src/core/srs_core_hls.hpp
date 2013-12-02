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

#ifdef SRS_HLS

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
class SrsRequest;

/**
* jitter correct for audio,
* the sample rate 44100/32000 will lost precise,
* when mp4/ts(tbn=90000) covert to flv/rtmp(1000),
* so the Hls on ipad or iphone will corrupt,
* @see nginx-rtmp: est_pts
*/
class SrsHlsAacJitter
{
private:
	int64_t base_pts;
	int64_t nb_samples;
	int sync_ms;
public:
	SrsHlsAacJitter();
	virtual ~SrsHlsAacJitter();
	/**
	* when buffer start, calc the "correct" pts for ts,
	* @param flv_pts, the flv pts calc from flv header timestamp,
	* @return the calc correct pts.
	*/
	virtual int64_t on_buffer_start(int64_t flv_pts, int sample_rate);
	/**
	* when buffer continue, muxer donot write to file,
	* the audio buffer continue grow and donot need a pts,
	* for the ts audio PES packet only has one pts at the first time.
	*/
	virtual void on_buffer_continue();
};

//TODO: refine the ts muxer, do more jobs.
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
	virtual int write_audio(SrsMpegtsFrame* af, SrsCodecBuffer* ab);
	virtual int write_video(SrsMpegtsFrame* vf, SrsCodecBuffer* vb);
	virtual void close();
};

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
	
	/**
	* update the segment duration.
	*/
	virtual double update_duration(int64_t video_stream_dts);
};

/**
* muxer the m3u8 and ts files.
*/
class SrsM3u8Muxer
{
private:
	std::string app;
	std::string stream;
private:
	std::string hls_path;
	int hls_fragment;
	int hls_window;
private:
	bool _is_open;
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
	// last known dts
	int64_t video_stream_dts;
public:
	SrsM3u8Muxer();
	virtual ~SrsM3u8Muxer();
public:
	virtual bool is_open();
	virtual int update_config(std::string _app, std::string _stream, std::string path, int fragment, int window);
	virtual int segment_open();
	virtual int flush_audio(SrsMpegtsFrame* af, SrsCodecBuffer* ab);
	virtual int flush_video(SrsMpegtsFrame* af, SrsCodecBuffer* ab, SrsMpegtsFrame* vf, SrsCodecBuffer* vb);
	virtual int segment_close();
private:
	virtual int refresh_m3u8();
	virtual int _refresh_m3u8(int& fd, std::string m3u8_file);
	virtual int create_dir();
};

/**
* ts need to cache some audio then flush
*/
class SrsTSCache
{
private:
	// current frame and buffer
	SrsMpegtsFrame* af;
	SrsCodecBuffer* ab;
	SrsMpegtsFrame* vf;
	SrsCodecBuffer* vb;
private:
	// the audio cache buffer start pts, to flush audio if full.
	int64_t audio_buffer_start_pts;
	// time jitter for aac
	SrsHlsAacJitter* aac_jitter;
public:
	SrsTSCache();
	virtual ~SrsTSCache();
public:
	/**
	* write audio to cache, if need to flush, flush to muxer.
	*/
	virtual int write_audio(SrsCodec* codec, SrsM3u8Muxer* muxer, int64_t pts, SrsCodecSample* sample);
	/**
	* write video to muxer.
	*/
	virtual int write_video(SrsCodec* codec, SrsM3u8Muxer* muxer, int64_t dts, SrsCodecSample* sample);
	/**
	* flush audio in cache to muxer.
	*/
	virtual int flush_audio(SrsM3u8Muxer* muxer);
private:
	virtual int cache_audio(SrsCodec* codec, SrsCodecSample* sample);
	virtual int cache_video(SrsCodec* codec, SrsCodecSample* sample);
};

/**
* write m3u8 hls.
*/
class SrsHls
{
private:
	SrsM3u8Muxer* muxer;
	SrsTSCache* ts_cache;
private:
	bool hls_enabled;
	SrsCodec* codec;
	SrsCodecSample* sample;
	SrsRtmpJitter* jitter;
public:
	SrsHls();
	virtual ~SrsHls();
public:
	virtual int on_publish(SrsRequest* req);
	virtual void on_unpublish();
	virtual int on_meta_data(SrsOnMetaDataPacket* metadata);
	virtual int on_audio(SrsSharedPtrMessage* audio);
	virtual int on_video(SrsSharedPtrMessage* video);
};

#endif

#endif