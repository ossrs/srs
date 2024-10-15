//
// Copyright (c) 2013-2024 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_HLS_HPP
#define SRS_APP_HLS_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

#include <srs_kernel_codec.hpp>
#include <srs_kernel_file.hpp>
#include <srs_app_async_call.hpp>
#include <srs_app_fragment.hpp>

class SrsFormat;
class SrsSharedPtrMessage;
class SrsAmf0Object;
class SrsRtmpJitter;
class SrsTsContextWriter;
class SrsRequest;
class SrsPithyPrint;
class SrsLiveSource;
class SrsOriginHub;
class SrsFileWriter;
class SrsSimpleStream;
class SrsTsAacJitter;
class SrsTsMessageCache;
class SrsHlsSegment;
class SrsTsContext;

// The wrapper of m3u8 segment from specification:
//
// 3.3.2.  EXTINF
// The EXTINF tag specifies the duration of a media segment.
class SrsHlsSegment : public SrsFragment
{
public:
    // sequence number in m3u8.
    int sequence_no;
    // ts uri in m3u8.
    std::string uri;
    // The underlayer file writer.
    SrsFileWriter* writer;
    // The TS context writer to write TS to file.
    SrsTsContextWriter* tscw;
    // Will be saved in m3u8 file.
    unsigned char iv[16];
    // The full key path.
    std::string keypath;
public:
    SrsHlsSegment(SrsTsContext* c, SrsAudioCodecId ac, SrsVideoCodecId vc, SrsFileWriter* w);
    virtual ~SrsHlsSegment();
public:
    void config_cipher(unsigned char* key,unsigned char* iv);
    // replace the placeholder
    virtual srs_error_t rename();
};

// The hls async call: on_hls
class SrsDvrAsyncCallOnHls : public ISrsAsyncCallTask
{
private:
    SrsContextId cid;
    std::string path;
    std::string ts_url;
    std::string m3u8;
    std::string m3u8_url;
    int seq_no;
    SrsRequest* req;
    srs_utime_t duration;
public:
    // TODO: FIXME: Use TBN 1000.
    SrsDvrAsyncCallOnHls(SrsContextId c, SrsRequest* r, std::string p, std::string t, std::string m, std::string mu, int s, srs_utime_t d);
    virtual ~SrsDvrAsyncCallOnHls();
public:
    virtual srs_error_t call();
    virtual std::string to_string();
};

// The hls async call: on_hls_notify
class SrsDvrAsyncCallOnHlsNotify : public ISrsAsyncCallTask
{
private:
    SrsContextId cid;
    std::string ts_url;
    SrsRequest* req;
public:
    SrsDvrAsyncCallOnHlsNotify(SrsContextId c, SrsRequest* r, std::string u);
    virtual ~SrsDvrAsyncCallOnHlsNotify();
public:
    virtual srs_error_t call();
    virtual std::string to_string();
};

// Mux the HLS stream(m3u8 and ts files).
// Generally, the m3u8 muxer only provides methods to open/close segments,
// to flush video/audio, without any mechenisms.
//
// That is, user must use HlsCache, which will control the methods of muxer,
// and provides HLS mechenisms.
class SrsHlsMuxer
{
private:
    SrsRequest* req;
private:
    std::string hls_entry_prefix;
    std::string hls_path;
    std::string hls_ts_file;
    bool hls_cleanup;
    bool hls_wait_keyframe;
    std::string m3u8_dir;
    double hls_aof_ratio;
    // TODO: FIXME: Use TBN 1000.
    srs_utime_t hls_fragment;
    srs_utime_t hls_window;
    SrsAsyncCallWorker* async;
private:
    // Whether use floor algorithm for timestamp.
    bool hls_ts_floor;
    // The deviation in piece to adjust the fragment to be more
    // bigger or smaller.
    int deviation_ts;
    // The previous reap floor timestamp,
    // used to detect the dup or jmp or ts.
    int64_t accept_floor_ts;
    int64_t previous_floor_ts;
private:
    // Whether encrypted or not
    bool hls_keys;
    int  hls_fragments_per_key;
    // The key file name
    std::string hls_key_file;
    // The key file path
    std::string hls_key_file_path;
    // The key file url
    std::string hls_key_url;
    // The key and iv.
    unsigned char key[16];
    unsigned char iv[16];
    // The underlayer file writer.
    SrsFileWriter* writer;
private:
    int _sequence_no;
    srs_utime_t max_td;
    std::string m3u8;
    std::string m3u8_url;
private:
    // The available cached segments in m3u8.
    SrsFragmentWindow* segments;
    // The current writing segment.
    SrsHlsSegment* current;
    // The ts context, to keep cc continous between ts.
    SrsTsContext* context;
private:
    // Latest audio codec, parsed from stream.
    SrsAudioCodecId latest_acodec_;
    // Latest audio codec, parsed from stream.
    SrsVideoCodecId latest_vcodec_;
public:
    SrsHlsMuxer();
    virtual ~SrsHlsMuxer();
public:
    virtual void dispose();
public:
    virtual int sequence_no();
    virtual std::string ts_url();
    virtual srs_utime_t duration();
    virtual int deviation();
public:
    SrsAudioCodecId latest_acodec();
    void set_latest_acodec(SrsAudioCodecId v);
    SrsVideoCodecId latest_vcodec();
    void set_latest_vcodec(SrsVideoCodecId v);
public:
    // Initialize the hls muxer.
    virtual srs_error_t initialize();
    // When publish or unpublish stream.
    virtual srs_error_t on_publish(SrsRequest* req);
    virtual srs_error_t on_unpublish();
    // When publish, update the config for muxer.
    virtual srs_error_t update_config(SrsRequest* r, std::string entry_prefix,
        std::string path, std::string m3u8_file, std::string ts_file,
        srs_utime_t fragment, srs_utime_t window, bool ts_floor, double aof_ratio,
        bool cleanup, bool wait_keyframe, bool keys, int fragments_per_key,
        std::string key_file, std::string key_file_path, std::string key_url);
    // Open a new segment(a new ts file)
    virtual srs_error_t segment_open();
    virtual srs_error_t on_sequence_header();
    // Whether segment overflow,
    // that is whether the current segment duration>=(the segment in config)
    virtual bool is_segment_overflow();
    // Whether wait keyframe to reap the ts.
    virtual bool wait_keyframe();
    // Whether segment absolutely overflow, for pure audio to reap segment,
    // that is whether the current segment duration>=2*(the segment in config)
    virtual bool is_segment_absolutely_overflow();
public:
    // Whether current hls muxer is pure audio mode.
    virtual bool pure_audio();
    virtual srs_error_t flush_audio(SrsTsMessageCache* cache);
    virtual srs_error_t flush_video(SrsTsMessageCache* cache);
    // When flushing video or audio, we update the duration. But, we should also update the
    // duration before closing the segment. Keep in mind that it's fine to update the duration
    // several times using the same dts timestamp.
    void update_duration(uint64_t dts);
    // Close segment(ts).
    virtual srs_error_t segment_close();
private:
    virtual srs_error_t do_segment_close();
    virtual srs_error_t write_hls_key();
    virtual srs_error_t refresh_m3u8();
    virtual srs_error_t _refresh_m3u8(std::string m3u8_file);
};

// The hls stream cache,
// use to cache hls stream and flush to hls muxer.
//
// When write stream to ts file:
// video frame will directly flush to M3u8Muxer,
// audio frame need to cache, because it's small and flv tbn problem.
//
// Whatever, the Hls cache used to cache video/audio,
// and flush video/audio to m3u8 muxer if needed.
//
// About the flv tbn problem:
//   flv tbn is 1/1000, ts tbn is 1/90000,
//   when timestamp convert to flv tbn, it will loose precise,
//   so we must gather audio frame together, and recalc the timestamp @see SrsTsAacJitter,
//   we use a aac jitter to correct the audio pts.
class SrsHlsController
{
private:
    // The HLS muxer to reap ts and m3u8.
    // The TS is cached to SrsTsMessageCache then flush to ts segment.
    SrsHlsMuxer* muxer;
    // The TS cache
    SrsTsMessageCache* tsmc;
public:
    SrsHlsController();
    virtual ~SrsHlsController();
public:
    virtual srs_error_t initialize();
    virtual void dispose();
    virtual int sequence_no();
    virtual std::string ts_url();
    virtual srs_utime_t duration();
    virtual int deviation();
public:
    // When publish or unpublish stream.
    virtual srs_error_t on_publish(SrsRequest* req);
    virtual srs_error_t on_unpublish();
    // When get sequence header,
    // must write a #EXT-X-DISCONTINUITY to m3u8.
    // @see: hls-m3u8-draft-pantos-http-live-streaming-12.txt
    // @see: 3.4.11.  EXT-X-DISCONTINUITY
    virtual srs_error_t on_sequence_header();
    // write audio to cache, if need to flush, flush to muxer.
    virtual srs_error_t write_audio(SrsAudioFrame* frame, int64_t pts);
    // write video to muxer.
    virtual srs_error_t write_video(SrsVideoFrame* frame, int64_t dts);
private:
    // Reopen the muxer for a new hls segment,
    // close current segment, open a new segment,
    // then write the key frame to the new segment.
    // so, user must reap_segment then flush_video to hls muxer.
    virtual srs_error_t reap_segment();
};

// Transmux RTMP stream to HLS(m3u8 and ts).
// TODO: FIXME: add utest for hls.
class SrsHls
{
private:
    SrsHlsController* controller;
private:
    SrsRequest* req;
    // Whether the HLS is enabled.
    bool enabled;
    // Whether the HLS stream is able to be disposed.
    bool disposable;
    // Whether the HLS stream is unpublishing.
    bool unpublishing_;
    // Whether requires HLS to do reload asynchronously.
    bool async_reload_;
    bool reloading_;
    // To detect heartbeat and dispose it if configured.
    srs_utime_t last_update_time;
private:
    // If the diff=dts-previous_audio_dts is about 23,
    // that's the AAC samples is 1024, and we use the samples to calc the dts.
    int64_t previous_audio_dts;
    // The total aac samples.
    uint64_t aac_samples;
    // Whether directly turn FLV timestamp to TS DTS.
    bool hls_dts_directly;
private:
    SrsOriginHub* hub;
    SrsRtmpJitter* jitter;
    SrsPithyPrint* pprint;
public:
    SrsHls();
    virtual ~SrsHls();
public:
    virtual void async_reload();
private:
    srs_error_t reload();
    srs_error_t do_reload(int *reloading, int *reloaded, int *refreshed);
public:
    virtual void dispose();
    virtual srs_error_t cycle();
    srs_utime_t cleanup_delay();
public:
    // Initialize the hls by handler and source.
    virtual srs_error_t initialize(SrsOriginHub* h, SrsRequest* r);
    // Publish stream event, continue to write the m3u8,
    // for the muxer object not destroyed.
    // @param fetch_sequence_header whether fetch sequence from source.
    virtual srs_error_t on_publish();
    // The unpublish event, only close the muxer, donot destroy the
    // muxer, for when we continue to publish, the m3u8 will continue.
    virtual void on_unpublish();
    // Mux the audio packets to ts.
    // @param shared_audio, directly ptr, copy it if need to save it.
    virtual srs_error_t on_audio(SrsSharedPtrMessage* shared_audio, SrsFormat* format);
    // Mux the video packets to ts.
    // @param shared_video, directly ptr, copy it if need to save it.
    // @param is_sps_pps whether the video is h.264 sps/pps.
    // TODO: FIXME: Remove param is_sps_pps.
    virtual srs_error_t on_video(SrsSharedPtrMessage* shared_video, SrsFormat* format);
private:
    virtual void hls_show_mux_log();
};

#endif
