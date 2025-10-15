//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_APP9_HPP
#define SRS_UTEST_APP9_HPP

/*
#include <srs_utest_app9.hpp>
*/
#include <srs_utest.hpp>

#include <srs_app_config.hpp>
#include <srs_app_dash.hpp>
#include <srs_app_dvr.hpp>
#include <srs_app_factory.hpp>
#include <srs_app_forward.hpp>
#include <srs_app_hds.hpp>
#include <srs_app_hls.hpp>
#include <srs_app_ng_exec.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_statistic.hpp>
#include <srs_kernel_packet.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_utest_app6.hpp>
#ifdef SRS_HDS
#include <srs_app_hds.hpp>
#endif
#include <srs_app_factory.hpp>

// Mock media packet for testing jitter correction
class MockMediaPacketForJitter : public SrsMediaPacket
{
public:
    MockMediaPacketForJitter(int64_t timestamp, bool is_av);
    virtual ~MockMediaPacketForJitter();
};

// Mock live source for testing message queue dump_packets
class MockLiveSourceForQueue : public SrsLiveSource
{
public:
    MockLiveSourceForQueue();
    virtual ~MockLiveSourceForQueue();
    virtual void on_consumer_destroy(SrsLiveConsumer *consumer);
    virtual srs_error_t initialize(SrsSharedPtr<SrsLiveSource> wrapper, ISrsRequest *r);
    virtual void update_auth(ISrsRequest *r);
    virtual srs_error_t consumer_dumps(ISrsLiveConsumer *consumer, bool ds, bool dm, bool dg);
};

// Mock live consumer for testing message queue dump_packets
class MockLiveConsumerForQueue : public SrsLiveConsumer
{
public:
    int enqueue_count_;
    std::vector<int64_t> enqueued_timestamps_;

public:
    MockLiveConsumerForQueue(MockLiveSourceForQueue *source);
    virtual ~MockLiveConsumerForQueue();
    virtual srs_error_t enqueue(SrsMediaPacket *shared_msg, bool atc, SrsRtmpJitterAlgorithm ag);
};

// Mock media packet for testing gop cache with H.264 video
class MockH264VideoPacket : public SrsMediaPacket
{
public:
    MockH264VideoPacket(bool is_keyframe);
    virtual ~MockH264VideoPacket();
};

// Mock media packet for testing gop cache with audio
class MockAudioPacket : public SrsMediaPacket
{
public:
    MockAudioPacket();
    virtual ~MockAudioPacket();
};

// Mock ISrsHls for testing SrsOriginHub::initialize
class MockHlsForOriginHub : public ISrsHls
{
public:
    int initialize_count_;
    srs_error_t initialize_error_;
    srs_utime_t cleanup_delay_;
    int on_audio_count_;
    int on_video_count_;

public:
    MockHlsForOriginHub();
    virtual ~MockHlsForOriginHub();
    virtual srs_error_t initialize(SrsOriginHub *h, ISrsRequest *r);
    virtual srs_error_t on_audio(SrsMediaPacket *shared_audio, SrsFormat *format);
    virtual srs_error_t on_video(SrsMediaPacket *shared_video, SrsFormat *format);
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
    virtual void dispose();
    virtual srs_error_t cycle();
    virtual srs_utime_t cleanup_delay();
};

// Mock ISrsDash for testing SrsOriginHub::initialize
class MockDashForOriginHub : public ISrsDash
{
public:
    int initialize_count_;
    srs_error_t initialize_error_;
    srs_utime_t cleanup_delay_;
    int on_audio_count_;
    int on_video_count_;

public:
    MockDashForOriginHub();
    virtual ~MockDashForOriginHub();
    virtual srs_error_t initialize(ISrsOriginHub *h, ISrsRequest *r);
    virtual srs_error_t on_publish();
    virtual srs_error_t on_audio(SrsMediaPacket *shared_audio, SrsFormat *format);
    virtual srs_error_t on_video(SrsMediaPacket *shared_video, SrsFormat *format);
    virtual void on_unpublish();
    virtual void dispose();
    virtual srs_error_t cycle();
    virtual srs_utime_t cleanup_delay();
};

// Mock ISrsDvr for testing SrsOriginHub::initialize
class MockDvrForOriginHub : public ISrsDvr
{
public:
    int initialize_count_;
    srs_error_t initialize_error_;
    int on_meta_data_count_;
    int on_audio_count_;
    int on_video_count_;

public:
    MockDvrForOriginHub();
    virtual void assemble();
    virtual ~MockDvrForOriginHub();
    virtual srs_error_t initialize(ISrsOriginHub *h, ISrsRequest *r);
    virtual srs_error_t on_publish(ISrsRequest *r);
    virtual void on_unpublish();
    virtual srs_error_t on_meta_data(SrsMediaPacket *metadata);
    virtual srs_error_t on_audio(SrsMediaPacket *shared_audio, SrsFormat *format);
    virtual srs_error_t on_video(SrsMediaPacket *shared_video, SrsFormat *format);
};

// Mock ISrsForwarder for testing SrsOriginHub::on_meta_data
class MockForwarderForOriginHub : public ISrsForwarder
{
public:
    int on_meta_data_count_;
    int on_audio_count_;
    int on_video_count_;

public:
    MockForwarderForOriginHub();
    virtual ~MockForwarderForOriginHub();
    virtual srs_error_t initialize(ISrsRequest *r, std::string ep);
    virtual void set_queue_size(srs_utime_t queue_size);
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
    virtual srs_error_t on_meta_data(SrsMediaPacket *shared_metadata);
    virtual srs_error_t on_audio(SrsMediaPacket *shared_audio);
    virtual srs_error_t on_video(SrsMediaPacket *shared_video);
};

// Mock ISrsLiveSource for testing SrsOriginHub::on_audio
class MockLiveSourceForOriginHub : public ISrsLiveSource
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsRtmpFormat *format_;
    SrsMetaCache *meta_;

public:
    MockLiveSourceForOriginHub();
    virtual ~MockLiveSourceForOriginHub();
    virtual void on_consumer_destroy(SrsLiveConsumer *consumer);
    virtual SrsContextId source_id();
    virtual SrsContextId pre_source_id();
    virtual SrsMetaCache *meta();
    virtual SrsRtmpFormat *format();
    virtual srs_error_t on_source_id_changed(SrsContextId id);
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
    virtual srs_error_t on_audio(SrsRtmpCommonMessage *audio);
    virtual srs_error_t on_video(SrsRtmpCommonMessage *video);
    virtual srs_error_t on_aggregate(SrsRtmpCommonMessage *msg);
    virtual srs_error_t on_meta_data(SrsRtmpCommonMessage *msg, SrsOnMetaDataPacket *metadata);
};

// Mock ISrsStatistic for testing SrsOriginHub::on_video
class MockStatisticForOriginHub : public ISrsStatistic
{
public:
    int on_video_info_count_;
    int on_audio_info_count_;

public:
    MockStatisticForOriginHub();
    virtual ~MockStatisticForOriginHub();
    virtual void on_disconnect(std::string id, srs_error_t err);
    virtual srs_error_t on_client(std::string id, ISrsRequest *req, ISrsExpire *conn, SrsRtmpConnType type);
    virtual srs_error_t on_video_info(ISrsRequest *req, SrsVideoCodecId vcodec, int avc_profile, int avc_level, int width, int height);
    virtual srs_error_t on_audio_info(ISrsRequest *req, SrsAudioCodecId acodec, SrsAudioSampleRate asample_rate, SrsAudioChannels asound_type, SrsAacObjectType aac_object);
    virtual void on_stream_publish(ISrsRequest *req, std::string publisher_id);
    virtual void on_stream_close(ISrsRequest *req);
    virtual void kbps_add_delta(std::string id, ISrsKbpsDelta *delta);
    virtual void kbps_sample();
    virtual srs_error_t on_video_frames(ISrsRequest *req, int nb_frames);
    virtual std::string server_id();
    virtual std::string service_id();
    virtual std::string service_pid();
    virtual SrsStatisticVhost *find_vhost_by_id(std::string vid);
    virtual SrsStatisticStream *find_stream(std::string sid);
    virtual SrsStatisticStream *find_stream_by_url(std::string url);
    virtual SrsStatisticClient *find_client(std::string client_id);
    virtual srs_error_t dumps_vhosts(SrsJsonArray *arr);
    virtual srs_error_t dumps_streams(SrsJsonArray *arr, int start, int count);
    virtual srs_error_t dumps_clients(SrsJsonArray *arr, int start, int count);
    virtual srs_error_t dumps_metrics(int64_t &send_bytes, int64_t &recv_bytes, int64_t &nstreams, int64_t &nclients, int64_t &total_nclients, int64_t &nerrs);
};

// Mock ISrsNgExec for testing SrsOriginHub::on_publish
class MockNgExecForOriginHub : public ISrsNgExec
{
public:
    int on_publish_count_;

public:
    MockNgExecForOriginHub();
    virtual ~MockNgExecForOriginHub();
    virtual srs_error_t on_publish(ISrsRequest *req);
    virtual void on_unpublish();
    virtual srs_error_t cycle();
};

#ifdef SRS_HDS
// Mock ISrsHds for testing SrsOriginHub::on_publish
class MockHdsForOriginHub : public ISrsHds
{
public:
    int on_publish_count_;

public:
    MockHdsForOriginHub();
    virtual ~MockHdsForOriginHub();
    virtual srs_error_t on_publish(ISrsRequest *req);
    virtual srs_error_t on_unpublish();
    virtual srs_error_t on_video(SrsMediaPacket *msg);
    virtual srs_error_t on_audio(SrsMediaPacket *msg);
};
#endif

// Mock config for testing SrsOriginHub::create_forwarders
class MockAppConfigForForwarder : public MockAppConfig
{
public:
    SrsConfDirective *forwards_directive_;
    SrsConfDirective *backend_directive_;

public:
    MockAppConfigForForwarder();
    virtual ~MockAppConfigForForwarder();
    virtual bool get_forward_enabled(std::string vhost);
    virtual SrsConfDirective *get_forwards(std::string vhost);
    virtual SrsConfDirective *get_forward_backend(std::string vhost);
    void set_forward_destinations(const std::vector<std::string> &destinations);
    void set_forward_backend(const std::string &backend_url);
};

// Mock HTTP hooks for testing SrsOriginHub::create_backend_forwarders
class MockHttpHooksForBackend : public MockHttpHooks
{
public:
    std::vector<std::string> backend_urls_;
    int on_forward_backend_count_;

public:
    MockHttpHooksForBackend();
    virtual ~MockHttpHooksForBackend();
    virtual srs_error_t on_forward_backend(std::string url, ISrsRequest *req, std::vector<std::string> &rtmp_urls);
    void set_backend_urls(const std::vector<std::string> &urls);
};

// Mock ISrsHourGlass for testing SrsLiveSourceManager::setup_ticks
class MockHourGlassForSourceManager : public ISrsHourGlass
{
public:
    int tick_event_;
    srs_utime_t tick_interval_;
    int tick_count_;
    int start_count_;
    srs_error_t tick_error_;
    srs_error_t start_error_;

public:
    MockHourGlassForSourceManager();
    virtual ~MockHourGlassForSourceManager();
    virtual srs_error_t start();
    virtual void stop();
    virtual srs_error_t tick(srs_utime_t interval);
    virtual srs_error_t tick(int event, srs_utime_t interval);
    virtual void untick(int event);
};

// Mock ISrsAppFactory for testing SrsLiveSourceManager::fetch_or_create
class MockAppFactoryForSourceManager : public SrsAppFactory
{
public:
    int create_live_source_count_;

public:
    MockAppFactoryForSourceManager();
    virtual ~MockAppFactoryForSourceManager();
    virtual SrsLiveSource *create_live_source();
};

// Mock ISrsOriginHub for testing SrsLiveSource::initialize
class MockOriginHubForLiveSource : public ISrsOriginHub
{
public:
    int initialize_count_;
    srs_error_t initialize_error_;

public:
    MockOriginHubForLiveSource();
    virtual ~MockOriginHubForLiveSource();
    virtual srs_error_t initialize(SrsSharedPtr<SrsLiveSource> s, ISrsRequest *r);
    virtual void dispose();
    virtual srs_error_t cycle();
    virtual bool active();
    virtual srs_utime_t cleanup_delay();
    virtual srs_error_t on_meta_data(SrsMediaPacket *shared_metadata, SrsOnMetaDataPacket *packet);
    virtual srs_error_t on_audio(SrsMediaPacket *shared_audio);
    virtual srs_error_t on_video(SrsMediaPacket *shared_video, bool is_sequence_header);
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
    virtual srs_error_t on_dvr_request_sh();
};

// Mock ISrsAppFactory for testing SrsLiveSource::initialize
class MockAppFactoryForLiveSource : public SrsAppFactory
{
public:
    MockOriginHubForLiveSource *mock_hub_;
    int create_origin_hub_count_;

public:
    MockAppFactoryForLiveSource();
    virtual ~MockAppFactoryForLiveSource();
    virtual ISrsOriginHub *create_origin_hub();
};

#endif
