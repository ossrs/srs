//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_HLS_HPP
#define SRS_APP_HLS_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>
#include <sstream>

#include <srs_app_async_call.hpp>
#include <srs_app_fragment.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_mp4.hpp>

class SrsFormat;
class SrsMediaPacket;
class SrsAmf0Object;
class SrsRtmpJitter;
class SrsTsContextWriter;
class ISrsRequest;
class SrsPithyPrint;
class SrsLiveSource;
class SrsOriginHub;
class ISrsOriginHub;
class ISrsFileWriter;
class ISrsAppConfig;
class ISrsHttpHooks;
class SrsSimpleStream;
class SrsTsAacJitter;
class SrsTsMessageCache;
class SrsHlsSegment;
class SrsTsContext;
class SrsFmp4SegmentEncoder;
class ISrsHttpHooks;
class ISrsAppConfig;
class ISrsAppFactory;

// The wrapper of m3u8 segment from specification:
//
// 3.3.2.  EXTINF
// The EXTINF tag specifies the duration of a media segment.
// TODO: refactor this to support fmp4 segment.
class SrsHlsSegment : public SrsFragment
{
public:
    // sequence number in m3u8.
    int sequence_no_;
    // ts uri in m3u8.
    std::string uri_;
    // The underlayer file writer.
    ISrsFileWriter *writer_;
    // The TS context writer to write TS to file.
    SrsTsContextWriter *tscw_;
    // Will be saved in m3u8 file.
    unsigned char iv_[16];
    // The full key path.
    std::string keypath_;

public:
    SrsHlsSegment(SrsTsContext *c, SrsAudioCodecId ac, SrsVideoCodecId vc, ISrsFileWriter *w);
    virtual ~SrsHlsSegment();

public:
    void config_cipher(unsigned char *key, unsigned char *iv);
    // replace the placeholder
    virtual srs_error_t rename();
};

class SrsInitMp4Segment : public SrsFragment
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsFileWriter *fw_;
    SrsMp4M2tsInitEncoder init_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Key ID for encryption
    unsigned char kid_[16];
    // Constant IV for encryption
    unsigned char const_iv_[16];
    // IV size (8 or 16 bytes)
    uint8_t const_iv_size_;

public:
    SrsInitMp4Segment(ISrsFileWriter *fw);
    virtual ~SrsInitMp4Segment();

public:
    virtual srs_error_t config_cipher(unsigned char *kid, unsigned char *const_iv, uint8_t const_iv_size);
    // Write the init mp4 file, with the v_tid(video track id) and a_tid (audio track id).
    virtual srs_error_t write(SrsFormat *format, int v_tid, int a_tid);
    virtual srs_error_t write_video_only(SrsFormat *format, int v_tid);
    virtual srs_error_t write_audio_only(SrsFormat *format, int a_tid);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t init_encoder();
};

// TODO: merge this code with SrsFragmentedMp4 in dash
class SrsHlsM4sSegment : public SrsFragment
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsFileWriter *fw_;
    SrsFmp4SegmentEncoder enc_;

public:
    // m4s uri in m3u8.
    std::string uri_;
    // sequence number in m3u8.
    int sequence_no_;
    // IV for encryption, saved in m3u8 file.
    unsigned char iv_[16];

public:
    SrsHlsM4sSegment(ISrsFileWriter *fw);
    virtual ~SrsHlsM4sSegment();

public:
    virtual srs_error_t initialize(int64_t time, uint32_t v_tid, uint32_t a_tid, int sequence_number, std::string m4s_path);
    virtual void config_cipher(unsigned char *key, unsigned char *iv);
    virtual srs_error_t write(SrsMediaPacket *shared_msg, SrsFormat *format);
    // Finalizes segment
    virtual srs_error_t reap(uint64_t dts);
};

// The hls async call: on_hls
class SrsDvrAsyncCallOnHls : public ISrsAsyncCallTask
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsHttpHooks *hooks_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsContextId cid_;
    std::string path_;
    std::string ts_url_;
    std::string m3u8_;
    std::string m3u8_url_;
    int seq_no_;
    ISrsRequest *req_;
    srs_utime_t duration_;

public:
    // TODO: FIXME: Use TBN 1000.
    SrsDvrAsyncCallOnHls(SrsContextId c, ISrsRequest *r, std::string p, std::string t, std::string m, std::string mu, int s, srs_utime_t d);
    virtual ~SrsDvrAsyncCallOnHls();

public:
    virtual srs_error_t call();
    virtual std::string to_string();
};

// The hls async call: on_hls_notify
class SrsDvrAsyncCallOnHlsNotify : public ISrsAsyncCallTask
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsHttpHooks *hooks_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsContextId cid_;
    std::string ts_url_;
    ISrsRequest *req_;

public:
    SrsDvrAsyncCallOnHlsNotify(SrsContextId c, ISrsRequest *r, std::string u);
    virtual ~SrsDvrAsyncCallOnHlsNotify();

public:
    virtual srs_error_t call();
    virtual std::string to_string();
};

// The HLS muxer interface.
class ISrsHlsMuxer
{
public:
    ISrsHlsMuxer();
    virtual ~ISrsHlsMuxer();

public:
    virtual srs_error_t initialize() = 0;
    virtual void dispose() = 0;

public:
    virtual int sequence_no() = 0;
    virtual std::string ts_url() = 0;
    virtual srs_utime_t duration() = 0;
    virtual int deviation() = 0;

public:
    virtual SrsAudioCodecId latest_acodec() = 0;
    virtual void set_latest_acodec(SrsAudioCodecId v) = 0;
    virtual SrsVideoCodecId latest_vcodec() = 0;
    virtual void set_latest_vcodec(SrsVideoCodecId v) = 0;

public:
    virtual bool pure_audio() = 0;
    virtual bool is_segment_overflow() = 0;
    virtual bool is_segment_absolutely_overflow() = 0;
    virtual bool wait_keyframe() = 0;

public:
    virtual srs_error_t on_publish(ISrsRequest *req) = 0;
    virtual srs_error_t on_unpublish() = 0;
    virtual srs_error_t update_config(ISrsRequest *r, std::string entry_prefix,
                                      std::string path, std::string m3u8_file, std::string ts_file,
                                      srs_utime_t fragment, srs_utime_t window, bool ts_floor, double aof_ratio,
                                      bool cleanup, bool wait_keyframe, bool keys, int fragments_per_key,
                                      std::string key_file, std::string key_file_path, std::string key_url) = 0;
    virtual srs_error_t segment_open() = 0;
    virtual srs_error_t on_sequence_header() = 0;
    virtual srs_error_t flush_audio(SrsTsMessageCache *cache) = 0;
    virtual srs_error_t flush_video(SrsTsMessageCache *cache) = 0;
    virtual void update_duration(uint64_t dts) = 0;
    virtual srs_error_t segment_close() = 0;
    virtual srs_error_t recover_hls() = 0;
};

// Mux the HLS stream(m3u8 and ts files).
// Generally, the m3u8 muxer only provides methods to open/close segments,
// to flush video/audio, without any mechenisms.
//
// That is, user must use HlsCache, which will control the methods of muxer,
// and provides HLS mechenisms.
// TODO: Rename to SrsHlsTsMuxer, for TS file only.
class SrsHlsMuxer : public ISrsHlsMuxer
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsAppFactory *app_factory_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRequest *req_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::string hls_entry_prefix_;
    std::string hls_path_;
    std::string hls_ts_file_;
    bool hls_cleanup_;
    bool hls_wait_keyframe_;
    std::string m3u8_dir_;
    double hls_aof_ratio_;
    // TODO: FIXME: Use TBN 1000.
    srs_utime_t hls_fragment_;
    srs_utime_t hls_window_;
    SrsAsyncCallWorker *async_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Whether use floor algorithm for timestamp.
    bool hls_ts_floor_;
    // The deviation in piece to adjust the fragment to be more
    // bigger or smaller.
    int deviation_ts_;
    // The previous reap floor timestamp,
    // used to detect the dup or jmp or ts.
    int64_t accept_floor_ts_;
    int64_t previous_floor_ts_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Whether encrypted or not
    bool hls_keys_;
    int hls_fragments_per_key_;
    // The key file name
    std::string hls_key_file_;
    // The key file path
    std::string hls_key_file_path_;
    // The key file url
    std::string hls_key_url_;
    // The key and iv.
    unsigned char key_[16];
    unsigned char iv_[16];
    // The underlayer file writer.
    ISrsFileWriter *writer_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    int sequence_no_;
    srs_utime_t max_td_;
    std::string m3u8_;
    std::string m3u8_url_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The available cached segments in m3u8.
    SrsFragmentWindow *segments_;
    // The current writing segment.
    SrsHlsSegment *current_;
    // The ts context, to keep cc continous between ts.
    SrsTsContext *context_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
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
    virtual srs_error_t on_publish(ISrsRequest *req);
    virtual srs_error_t on_unpublish();
    // When publish, update the config for muxer.
    virtual srs_error_t update_config(ISrsRequest *r, std::string entry_prefix,
                                      std::string path, std::string m3u8_file, std::string ts_file,
                                      srs_utime_t fragment, srs_utime_t window, bool ts_floor, double aof_ratio,
                                      bool cleanup, bool wait_keyframe, bool keys, int fragments_per_key,
                                      std::string key_file, std::string key_file_path, std::string key_url);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t create_directories();

public:
    // Open a new segment(a new ts file)
    virtual srs_error_t segment_open();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual std::string generate_ts_filename();
    virtual bool is_segment_open();

public:
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
    virtual srs_error_t flush_audio(SrsTsMessageCache *cache);
    virtual srs_error_t flush_video(SrsTsMessageCache *cache);
    // When flushing video or audio, we update the duration. But, we should also update the
    // duration before closing the segment. Keep in mind that it's fine to update the duration
    // several times using the same dts timestamp.
    void update_duration(uint64_t dts);
    // Close segment(ts).
    virtual srs_error_t segment_close();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t do_segment_close();
    virtual srs_error_t do_segment_close2();
    virtual srs_error_t write_hls_key();
    virtual srs_error_t refresh_m3u8();
    virtual srs_error_t do_refresh_m3u8(std::string m3u8_file);
    virtual srs_error_t do_refresh_m3u8_segment(SrsHlsSegment *segment, std::stringstream &ss);
    // Check if a segment with the given URI already exists in the segments list.
    virtual bool segment_exists(const std::string &ts_url);

public:
    // HLS recover mode.
    srs_error_t recover_hls();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t do_recover_hls();
};

// Mux the HLS stream(m3u8 and m4s files).
// Generally, the m3u8 muxer only provides methods to open/close segments,
// to flush video/audio, without any mechenisms.
class SrsHlsFmp4Muxer
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsAppFactory *app_factory_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRequest *req_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::string hls_entry_prefix_;
    std::string hls_path_;
    std::string hls_m4s_file_;
    bool hls_cleanup_;
    bool hls_wait_keyframe_;
    std::string m3u8_dir_;
    double hls_aof_ratio_;
    // TODO: FIXME: Use TBN 1000.
    srs_utime_t hls_fragment_;
    srs_utime_t hls_window_;
    SrsAsyncCallWorker *async_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Whether use floor algorithm for timestamp.
    bool hls_ts_floor_;
    // The deviation in piece to adjust the fragment to be more
    // bigger or smaller.
    int deviation_ts_;
    // The previous reap floor timestamp,
    // used to detect the dup or jmp or ts.
    int64_t accept_floor_ts_;
    int64_t previous_floor_ts_;
    bool init_mp4_ready_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Whether encrypted or not
    // TODO: fmp4 encryption is not yet implemented.
    // fmp4 support four kinds of protection scheme: 'cenc', 'cbc1', 'cens', 'cbcs'.
    // @see: https://cdn.standards.iteh.ai/samples/84637/04ebded1a92a4c8ab9be6f419a3252ed/ISO-IEC-23001-7-2023.pdf
    // But unfortunately the above link is just part of the spec, the full doc is not free.
    // And Apple's doc said HLS support unencrypted and encrypted with 'cbcs'.
    // @see: https://developer.apple.com/documentation/http-live-streaming/about-the-common-media-application-format-with-http-live-streaming-hls
    // Another Apple doc said Encrypted fmp4 content MUST contain either a Sample Encryption Box('senc'), or both a Sample Auxiliary Information
    // Sizes Box('saiz') and a Sample Auxiliary Information Offsets Box('saio').
    // @see: https://developer.apple.com/documentation/http-live-streaming/hls-authoring-specification-for-apple-devices
    bool hls_keys_;
    int hls_fragments_per_key_;
    // The key file name
    std::string hls_key_file_;
    // The key file path
    std::string hls_key_file_path_;
    // The key file url
    std::string hls_key_url_;
    // The key and iv.
    unsigned char key_[16];
    unsigned char kid_[16];
    unsigned char iv_[16];
    // The underlayer file writer.
    ISrsFileWriter *writer_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    int sequence_no_;
    srs_utime_t max_td_;
    std::string m3u8_;
    std::string m3u8_url_;
    std::string init_mp4_uri_; // URI for init.mp4 in m3u8 playlist
    int video_track_id_;
    int audio_track_id_;
    uint64_t video_dts_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The available cached segments in m3u8.
    SrsFragmentWindow *segments_;
    // The current writing segment.
    SrsHlsM4sSegment *current_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Latest audio codec, parsed from stream.
    SrsAudioCodecId latest_acodec_;
    // Latest audio codec, parsed from stream.
    SrsVideoCodecId latest_vcodec_;

public:
    SrsHlsFmp4Muxer();
    virtual ~SrsHlsFmp4Muxer();

public:
    virtual void dispose();

public:
    virtual int sequence_no();
    virtual std::string m4s_url();
    virtual srs_utime_t duration();
    virtual int deviation();

public:
    SrsAudioCodecId latest_acodec();
    void set_latest_acodec(SrsAudioCodecId v);
    SrsVideoCodecId latest_vcodec();
    void set_latest_vcodec(SrsVideoCodecId v);

public:
    // Initialize the hls muxer.
    virtual srs_error_t initialize(int v_tid, int a_tid);
    // When publish or unpublish stream.
    virtual srs_error_t on_publish(ISrsRequest *req);

public:
    virtual srs_error_t write_init_mp4(SrsFormat *format, bool has_video, bool has_audio);
    virtual srs_error_t write_audio(SrsMediaPacket *shared_audio, SrsFormat *format);
    virtual srs_error_t write_video(SrsMediaPacket *shared_video, SrsFormat *format);

public:
    virtual srs_error_t on_unpublish();
    // When publish, update the config for muxer.
    virtual srs_error_t update_config(ISrsRequest *r);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t create_directories();

public:
    // Open a new segment(a new ts file)
    virtual srs_error_t segment_open(srs_utime_t basetime);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual std::string generate_m4s_filename();
    virtual bool is_segment_open();

public:
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
    // When flushing video or audio, we update the duration. But, we should also update the
    // duration before closing the segment. Keep in mind that it's fine to update the duration
    // several times using the same dts timestamp.
    void update_duration(uint64_t dts);
    // Close segment(ts).
    virtual srs_error_t segment_close();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t do_segment_close();
    virtual srs_error_t write_hls_key();
    virtual srs_error_t refresh_m3u8();
    virtual srs_error_t do_refresh_m3u8(std::string m3u8_file);
    virtual srs_error_t do_refresh_m3u8_segment(SrsHlsM4sSegment *segment, std::stringstream &ss);
};

// The base class for HLS controller
class ISrsHlsController
{
public:
    ISrsHlsController();
    virtual ~ISrsHlsController();

public:
    virtual srs_error_t initialize() = 0;
    virtual void dispose() = 0;
    // When publish or unpublish stream.
    virtual srs_error_t on_publish(ISrsRequest *req) = 0;
    virtual srs_error_t on_unpublish() = 0;

public:
    virtual srs_error_t write_audio(SrsMediaPacket *shared_audio, SrsFormat *format) = 0;
    virtual srs_error_t write_video(SrsMediaPacket *shared_video, SrsFormat *format) = 0;

public:
    virtual srs_error_t on_sequence_header(SrsMediaPacket *msg, SrsFormat *format) = 0;
    virtual int sequence_no() = 0;
    // TODO: maybe rename to segment_url?
    virtual std::string ts_url() = 0;
    virtual srs_utime_t duration() = 0;
    virtual int deviation() = 0;
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
// TODO: Rename to SrsHlsTsController, for TS file only.
class SrsHlsController : public ISrsHlsController
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The HLS muxer to reap ts and m3u8.
    // The TS is cached to SrsTsMessageCache then flush to ts segment.
    ISrsHlsMuxer *muxer_;
    // The TS cache
    SrsTsMessageCache *tsmc_;

    // If the diff=dts-previous_audio_dts is about 23,
    // that's the AAC samples is 1024, and we use the samples to calc the dts.
    int64_t previous_audio_dts_;
    // The total aac samples.
    uint64_t aac_samples_;
    // Whether directly turn FLV timestamp to TS DTS.
    bool hls_dts_directly_;

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
    virtual srs_error_t on_publish(ISrsRequest *req);
    virtual srs_error_t on_unpublish();
    // When get sequence header,
    // must write a #EXT-X-DISCONTINUITY to m3u8.
    // @see: hls-m3u8-draft-pantos-http-live-streaming-12.txt
    // @see: 3.4.11.  EXT-X-DISCONTINUITY
    virtual srs_error_t on_sequence_header(SrsMediaPacket *shared_audio, SrsFormat *format);
    // write audio to cache, if need to flush, flush to muxer.
    virtual srs_error_t write_audio(SrsMediaPacket *shared_audio, SrsFormat *format);
    // write video to muxer.
    virtual srs_error_t write_video(SrsMediaPacket *shared_video, SrsFormat *format);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Reopen the muxer for a new hls segment,
    // close current segment, open a new segment,
    // then write the key frame to the new segment.
    // so, user must reap_segment then flush_video to hls muxer.
    virtual srs_error_t reap_segment();
};

// HLS controller for fMP4 (.m4s) segments with init.mp4.
// Direct sample processing without caching, simpler than TS controller.
class SrsHlsMp4Controller : public ISrsHlsController
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    bool has_video_sh_;
    bool has_audio_sh_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    int video_track_id_;
    int audio_track_id_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Current audio dts.
    uint64_t audio_dts_;
    // Current video dts.
    uint64_t video_dts_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRequest *req_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsHlsFmp4Muxer *muxer_;

public:
    SrsHlsMp4Controller();
    virtual ~SrsHlsMp4Controller();

public:
    virtual srs_error_t initialize();
    virtual void dispose();
    // When publish or unpublish stream.
    virtual srs_error_t on_publish(ISrsRequest *req);
    virtual srs_error_t on_unpublish();
    virtual srs_error_t write_audio(SrsMediaPacket *shared_audio, SrsFormat *format);
    virtual srs_error_t write_video(SrsMediaPacket *shared_video, SrsFormat *format);

public:
    virtual srs_error_t on_sequence_header(SrsMediaPacket *shared_audio, SrsFormat *format);
    virtual int sequence_no();
    virtual std::string ts_url();
    virtual srs_utime_t duration();
    virtual int deviation();
};

// The HLS interface.
class ISrsHls
{
public:
    ISrsHls();
    virtual ~ISrsHls();

public:
    virtual srs_error_t initialize(ISrsOriginHub *h, ISrsRequest *r) = 0;
    virtual srs_error_t on_audio(SrsMediaPacket *shared_audio, SrsFormat *format) = 0;
    virtual srs_error_t on_video(SrsMediaPacket *shared_video, SrsFormat *format) = 0;
    virtual srs_error_t on_publish() = 0;
    virtual void on_unpublish() = 0;

public:
    virtual void dispose() = 0;
    virtual srs_error_t cycle() = 0;
    virtual srs_utime_t cleanup_delay() = 0;
};

// Transmux RTMP stream to HLS(m3u8 and ts,fmp4).
// TODO: FIXME: add utest for hls.
class SrsHls : public ISrsHls
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsHlsController *controller_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRequest *req_;
    // Whether the HLS is enabled.
    bool enabled_;
    // Whether the HLS stream is able to be disposed.
    bool disposable_;
    // Whether the HLS stream is unpublishing.
    bool unpublishing_;
    // Whether requires HLS to do reload asynchronously.
    bool async_reload_;
    bool reloading_;
    // To detect heartbeat and dispose it if configured.
    srs_utime_t last_update_time_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsOriginHub *hub_;
    SrsRtmpJitter *jitter_;
    SrsPithyPrint *pprint_;

public:
    SrsHls();
    virtual ~SrsHls();

public:
    virtual void async_reload();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t reload();
    srs_error_t do_reload(int *reloading, int *reloaded, int *refreshed);

public:
    virtual void dispose();
    virtual srs_error_t cycle();
    srs_utime_t cleanup_delay();

public:
    // Initialize the hls by handler and source.
    virtual srs_error_t initialize(ISrsOriginHub *h, ISrsRequest *r);
    // Publish stream event, continue to write the m3u8,
    // for the muxer object not destroyed.
    // @param fetch_sequence_header whether fetch sequence from source.
    virtual srs_error_t on_publish();
    // The unpublish event, only close the muxer, donot destroy the
    // muxer, for when we continue to publish, the m3u8 will continue.
    virtual void on_unpublish();
    // Mux the audio packets to ts.
    // @param shared_audio, directly ptr, copy it if need to save it.
    virtual srs_error_t on_audio(SrsMediaPacket *shared_audio, SrsFormat *format);
    // Mux the video packets to ts.
    // @param shared_video, directly ptr, copy it if need to save it.
    // @param is_sps_pps whether the video is h.264 sps/pps.
    // TODO: FIXME: Remove param is_sps_pps.
    virtual srs_error_t on_video(SrsMediaPacket *shared_video, SrsFormat *format);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual void hls_show_mux_log();
};

#endif
