/*
The MIT License (MIT)

Copyright (c) 2013-2017 SRS(ossrs)

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

#ifndef SRS_APP_DVR_HPP
#define SRS_APP_DVR_HPP

/*
#include <srs_app_dvr.hpp>
*/
#include <srs_core.hpp>

#include <string>
#include <sstream>

class SrsSource;
class SrsOriginHub;
class SrsRequest;
class SrsBuffer;
class SrsRtmpJitter;
class SrsSharedPtrMessage;
class SrsFileWriter;
class SrsFlvEncoder;
class SrsDvrPlan;
class SrsJsonAny;
class SrsJsonObject;
class SrsThread;
class SrsMp4Encoder;

#include <srs_app_source.hpp>
#include <srs_app_reload.hpp>
#include <srs_app_async_call.hpp>

/**
 * The segmenter for DVR, to write a segment file in flv/mp4.
 */
class SrsDvrSegmenter : public ISrsReloadHandler
{
protected:
    // The underlayer file object.
    SrsFileWriter* fs;
    // Whether wait keyframe to reap segment.
    bool wait_keyframe;
    // The duration in ms of current segment.
    int64_t duration;
private:
    // The path of current segment flv file path.
    std::string path;
    std::string tmp_dvr_file;
private:
    SrsRequest* req;
    SrsDvrPlan* plan;
private:
    SrsRtmpJitter* jitter;
    SrsRtmpJitterAlgorithm jitter_algorithm;
private:
    // The previous stream RTMP pkt time in ms, used to calc the duration.
    // for the RTMP timestamp will overflow.
    // TODO: FIXME: Use utility object to calc it.
    int64_t stream_previous_pkt_time;
public:
    SrsDvrSegmenter();
    virtual ~SrsDvrSegmenter();
public:
    // Initialize the segment.
    virtual int initialize(SrsDvrPlan* p, SrsRequest* r);
    // Get the current dvr path.
    virtual std::string get_path();
    // Get the duration in ms of segment.
    virtual int64_t get_duration();
    // Open new segment file.
    // @param use_tmp_file Whether use tmp file for DVR, and rename when close.
    // @remark Ignore when file is already open.
    virtual int open();
    // Write the metadata.
    virtual int write_metadata(SrsSharedPtrMessage* metadata);
    // Write audio packet.
    // @param shared_audio, directly ptr, copy it if need to save it.
    virtual int write_audio(SrsSharedPtrMessage* shared_audio);
    // Write video packet.
    // @param shared_video, directly ptr, copy it if need to save it.
    virtual int write_video(SrsSharedPtrMessage* shared_video);
    // Refresh the metadata. For example, there is duration in flv metadata,
    // when DVR in append mode, the duration must be update every some seconds.
    // @remark Maybe ignored by concreate segmenter.
    virtual int refresh_metadata() = 0;
    // Close current segment.
    // @remark ignore when already closed.
    virtual int close();
protected:
    virtual int open_encoder() = 0;
    virtual int encode_metadata(SrsSharedPtrMessage* metadata) = 0;
    virtual int encode_audio(SrsSharedPtrMessage* audio) = 0;
    virtual int encode_video(SrsSharedPtrMessage* video) = 0;
    virtual int close_encoder() = 0;
private:
    // Generate the flv segment path.
    virtual std::string generate_path();
    // When update the duration of segment by rtmp msg.
    virtual int on_update_duration(SrsSharedPtrMessage* msg);
// interface ISrsReloadHandler
public:
    virtual int on_reload_vhost_dvr(std::string vhost);
};

/**
 * The FLV segmenter to use FLV encoder to write file.
 */
class SrsDvrFlvSegmenter : public SrsDvrSegmenter
{
private:
    // The FLV encoder, for FLV target.
    SrsFlvEncoder* enc;
private:
    // The offset of file for duration value.
    // The next 8 bytes is the double value.
    int64_t duration_offset;
    // The offset of file for filesize value.
    // The next 8 bytes is the double value.
    int64_t filesize_offset;
    // Whether current segment has keyframe.
    bool has_keyframe;
public:
    SrsDvrFlvSegmenter();
    virtual ~SrsDvrFlvSegmenter();
public:
    virtual int refresh_metadata();
protected:
    virtual int open_encoder();
    virtual int encode_metadata(SrsSharedPtrMessage* metadata);
    virtual int encode_audio(SrsSharedPtrMessage* audio);
    virtual int encode_video(SrsSharedPtrMessage* video);
    virtual int close_encoder();
};

/**
 * The MP4 segmenter to use MP4 encoder to write file.
 */
class SrsDvrMp4Segmenter : public SrsDvrSegmenter
{
private:
    // The MP4 encoder, for MP4 target.
    SrsMp4Encoder* enc;
    // The buffer to demux the packet to mp4 sample.
    SrsBuffer* buffer;
public:
    SrsDvrMp4Segmenter();
    virtual ~SrsDvrMp4Segmenter();
public:
    virtual int refresh_metadata();
protected:
    virtual int open_encoder();
    virtual int encode_metadata(SrsSharedPtrMessage* metadata);
    virtual int encode_audio(SrsSharedPtrMessage* audio);
    virtual int encode_video(SrsSharedPtrMessage* video);
    virtual int close_encoder();
};

/**
* the dvr async call.
*/
class SrsDvrAsyncCallOnDvr : public ISrsAsyncCallTask
{
private:
    int cid;
    std::string path;
    SrsRequest* req;
public:
    SrsDvrAsyncCallOnDvr(int c, SrsRequest* r, std::string p);
    virtual ~SrsDvrAsyncCallOnDvr();
public:
    virtual int call();
    virtual std::string to_string();
};

/**
 * The DVR plan, when and how to reap segment.
*/
class SrsDvrPlan : public ISrsReloadHandler
{
public:
    SrsRequest* req;
protected:
    SrsOriginHub* hub;
    SrsDvrSegmenter* segment;
    SrsAsyncCallWorker* async;
    bool dvr_enabled;
public:
    SrsDvrPlan();
    virtual ~SrsDvrPlan();
public:
    virtual int initialize(SrsOriginHub* h, SrsDvrSegmenter* s, SrsRequest* r);
    virtual int on_publish() = 0;
    virtual void on_unpublish() = 0;
    virtual int on_meta_data(SrsSharedPtrMessage* shared_metadata);
    virtual int on_audio(SrsSharedPtrMessage* shared_audio);
    virtual int on_video(SrsSharedPtrMessage* shared_video);
// Internal interface for segmenter.
public:
    // When segmenter close a segment.
    virtual int on_reap_segment();
public:
    static int create_plan(std::string vhost, SrsDvrPlan** pplan);
};

/**
 * The DVR session plan: reap flv when session complete(unpublish)
 */
class SrsDvrSessionPlan : public SrsDvrPlan
{
public:
    SrsDvrSessionPlan();
    virtual ~SrsDvrSessionPlan();
public:
    virtual int on_publish();
    virtual void on_unpublish();
};

/**
 * The DVR segment plan: reap flv when duration exceed.
 */
class SrsDvrSegmentPlan : public SrsDvrPlan
{
private:
    // in config, in ms
    int cduration;
    bool wait_keyframe;
public:
    SrsDvrSegmentPlan();
    virtual ~SrsDvrSegmentPlan();
public:
    virtual int initialize(SrsOriginHub* h, SrsDvrSegmenter* s, SrsRequest* r);
    virtual int on_publish();
    virtual void on_unpublish();
    virtual int on_audio(SrsSharedPtrMessage* shared_audio);
    virtual int on_video(SrsSharedPtrMessage* shared_video);
private:
    virtual int update_duration(SrsSharedPtrMessage* msg);
// interface ISrsReloadHandler
public:
    virtual int on_reload_vhost_dvr(std::string vhost);
};

/**
 * DVR(Digital Video Recorder) to record RTMP stream to flv/mp4 file.
 */
class SrsDvr : public ISrsReloadHandler
{
private:
    SrsOriginHub* hub;
    SrsDvrPlan* plan;
    SrsRequest* req;
private:
    // whether the dvr is actived by filter, which is specified by dvr_apply.
    // we always initialize the dvr, which crote plan and segment object,
    // but they never create actual piece of file util the apply active it.
    bool actived;
public:
    SrsDvr();
    virtual ~SrsDvr();
public:
    /**
     * initialize dvr, create dvr plan.
     * when system initialize(encoder publish at first time, or reload),
     * initialize the dvr will reinitialize the plan, the whole dvr framework.
     */
    virtual int initialize(SrsOriginHub* h, SrsRequest* r);
    /**
     * publish stream event,
     * when encoder start to publish RTMP stream.
     * @param fetch_sequence_header whether fetch sequence from source.
     */
    virtual int on_publish(bool fetch_sequence_header);
    /**
    * the unpublish event.,
    * when encoder stop(unpublish) to publish RTMP stream.
    */
    virtual void on_unpublish();
    /**
    * get some information from metadata, it's optinal.
    */
    virtual int on_meta_data(SrsSharedPtrMessage* metadata);
    /**
    * mux the audio packets to dvr.
    * @param shared_audio, directly ptr, copy it if need to save it.
    */
    virtual int on_audio(SrsSharedPtrMessage* shared_audio);
    /**
    * mux the video packets to dvr.
    * @param shared_video, directly ptr, copy it if need to save it.
    */
    virtual int on_video(SrsSharedPtrMessage* shared_video);
// interface ISrsReloadHandler
public:
    virtual int on_reload_vhost_dvr_apply(std::string vhost);
};

#endif

