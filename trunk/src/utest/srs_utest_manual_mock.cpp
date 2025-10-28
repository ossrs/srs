//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_manual_mock.hpp>

#include <srs_app_caster_flv.hpp>
#include <srs_app_config.hpp>
#include <srs_app_dash.hpp>
#include <srs_app_dvr.hpp>
#include <srs_app_ffmpeg.hpp>
#include <srs_app_fragment.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_kernel_error.hpp>
#ifdef SRS_GB28181
#include <srs_app_gb28181.hpp>
#endif
#include <srs_app_ingest.hpp>
#include <srs_app_listener.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_app_rtmp_source.hpp>
#ifdef SRS_RTSP
#include <srs_app_rtsp_source.hpp>
#endif
#include <srs_app_st.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_hourglass.hpp>
#include <srs_kernel_mp4.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_http_client.hpp>
#include <srs_protocol_st.hpp>
#include <srs_protocol_utility.hpp>

#include <sstream>

#include <srs_utest_ai22.hpp>

// MockSdpFactory implementation
MockSdpFactory::MockSdpFactory()
{
    // Initialize default SSRC and payload type values
    audio_ssrc_ = 1001;
    audio_pt_ = 111;
    video_ssrc_ = 2002;
    video_pt_ = 96;
}

MockSdpFactory::~MockSdpFactory()
{
}

std::string MockSdpFactory::create_chrome_player_offer()
{
    // Create a real Chrome-like WebRTC SDP offer for a player (subscriber) with H.264 video and Opus audio
    // Use member variables for SSRC and payload type values
    // Key difference from publisher: uses recvonly instead of sendonly
    std::stringstream ss;
    ss << "v=0\r\n"
       << "o=- 4611731400430051337 2 IN IP4 127.0.0.1\r\n"
       << "s=-\r\n"
       << "t=0 0\r\n"
       << "a=group:BUNDLE 0 1\r\n"
       << "a=msid-semantic: WMS\r\n"
       // Audio media description (Opus)
       << "m=audio 9 UDP/TLS/RTP/SAVPF " << (int)audio_pt_ << "\r\n"
       << "c=IN IP4 0.0.0.0\r\n"
       << "a=rtcp:9 IN IP4 0.0.0.0\r\n"
       << "a=ice-ufrag:test1234\r\n"
       << "a=ice-pwd:testpassword1234567890\r\n"
       << "a=ice-options:trickle\r\n"
       << "a=fingerprint:sha-256 AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99\r\n"
       << "a=setup:actpass\r\n"
       << "a=mid:0\r\n"
       << "a=recvonly\r\n"
       << "a=rtcp-mux\r\n"
       << "a=rtpmap:" << (int)audio_pt_ << " opus/48000/2\r\n"
       << "a=fmtp:" << (int)audio_pt_ << " minptime=10;useinbandfec=1\r\n"
       // Video media description (H.264)
       << "m=video 9 UDP/TLS/RTP/SAVPF " << (int)video_pt_ << "\r\n"
       << "c=IN IP4 0.0.0.0\r\n"
       << "a=rtcp:9 IN IP4 0.0.0.0\r\n"
       << "a=ice-ufrag:test1234\r\n"
       << "a=ice-pwd:testpassword1234567890\r\n"
       << "a=ice-options:trickle\r\n"
       << "a=fingerprint:sha-256 AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99\r\n"
       << "a=setup:actpass\r\n"
       << "a=mid:1\r\n"
       << "a=recvonly\r\n"
       << "a=rtcp-mux\r\n"
       << "a=rtcp-rsize\r\n"
       << "a=rtpmap:" << (int)video_pt_ << " H264/90000\r\n"
       << "a=rtcp-fb:" << (int)video_pt_ << " nack\r\n"
       << "a=rtcp-fb:" << (int)video_pt_ << " nack pli\r\n"
       << "a=rtcp-fb:" << (int)video_pt_ << " transport-cc\r\n"
       << "a=fmtp:" << (int)video_pt_ << " level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f\r\n";

    return ss.str();
}

std::string MockSdpFactory::create_chrome_publisher_offer()
{
    // Create a real Chrome-like WebRTC SDP offer with H.264 video and Opus audio
    // Use member variables for SSRC and payload type values
    std::stringstream ss;
    ss << "v=0\r\n"
       << "o=- 4611731400430051336 2 IN IP4 127.0.0.1\r\n"
       << "s=-\r\n"
       << "t=0 0\r\n"
       << "a=group:BUNDLE 0 1\r\n"
       << "a=msid-semantic: WMS stream\r\n"
       // Audio media description (Opus)
       << "m=audio 9 UDP/TLS/RTP/SAVPF " << (int)audio_pt_ << "\r\n"
       << "c=IN IP4 0.0.0.0\r\n"
       << "a=rtcp:9 IN IP4 0.0.0.0\r\n"
       << "a=ice-ufrag:test1234\r\n"
       << "a=ice-pwd:testpassword1234567890\r\n"
       << "a=ice-options:trickle\r\n"
       << "a=fingerprint:sha-256 AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99\r\n"
       << "a=setup:actpass\r\n"
       << "a=mid:0\r\n"
       << "a=sendonly\r\n"
       << "a=rtcp-mux\r\n"
       << "a=rtpmap:" << (int)audio_pt_ << " opus/48000/2\r\n"
       << "a=fmtp:" << (int)audio_pt_ << " minptime=10;useinbandfec=1\r\n"
       << "a=ssrc:" << audio_ssrc_ << " cname:test-audio-cname\r\n"
       << "a=ssrc:" << audio_ssrc_ << " msid:stream audio\r\n"
       // Video media description (H.264)
       << "m=video 9 UDP/TLS/RTP/SAVPF " << (int)video_pt_ << "\r\n"
       << "c=IN IP4 0.0.0.0\r\n"
       << "a=rtcp:9 IN IP4 0.0.0.0\r\n"
       << "a=ice-ufrag:test1234\r\n"
       << "a=ice-pwd:testpassword1234567890\r\n"
       << "a=ice-options:trickle\r\n"
       << "a=fingerprint:sha-256 AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99\r\n"
       << "a=setup:actpass\r\n"
       << "a=mid:1\r\n"
       << "a=sendonly\r\n"
       << "a=rtcp-mux\r\n"
       << "a=rtcp-rsize\r\n"
       << "a=rtpmap:" << (int)video_pt_ << " H264/90000\r\n"
       << "a=rtcp-fb:" << (int)video_pt_ << " nack\r\n"
       << "a=rtcp-fb:" << (int)video_pt_ << " nack pli\r\n"
       << "a=rtcp-fb:" << (int)video_pt_ << " transport-cc\r\n"
       << "a=fmtp:" << (int)video_pt_ << " level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f\r\n"
       << "a=ssrc:" << video_ssrc_ << " cname:test-video-cname\r\n"
       << "a=ssrc:" << video_ssrc_ << " msid:stream video\r\n";

    return ss.str();
}

MockDtlsCertificate::MockDtlsCertificate()
{
    fingerprint_ = "AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99";
}

MockDtlsCertificate::~MockDtlsCertificate()
{
}

srs_error_t MockDtlsCertificate::initialize()
{
    return srs_success;
}

std::string MockDtlsCertificate::get_fingerprint()
{
    return fingerprint_;
}

// MockRtcTrackDescriptionFactory implementation
MockRtcTrackDescriptionFactory::MockRtcTrackDescriptionFactory()
{
    audio_ssrc_ = 12345;
    video_ssrc_ = 67890;
    screen_ssrc_ = 98765;

    audio_pt_ = 111;
    video_pt_ = 96;
    screen_pt_ = 97;
}

MockRtcTrackDescriptionFactory::~MockRtcTrackDescriptionFactory()
{
}

std::map<uint32_t, SrsRtcTrackDescription *> MockRtcTrackDescriptionFactory::create_audio_video_tracks()
{
    std::map<uint32_t, SrsRtcTrackDescription *> sub_relations;

    // Create audio track
    SrsRtcTrackDescription *audio_desc = create_audio_track(audio_ssrc_, "audio-track-1", "0");
    sub_relations[audio_desc->ssrc_] = audio_desc;

    // Create video track
    SrsRtcTrackDescription *video_desc = create_video_track(video_ssrc_, "video-track-1", "1");
    sub_relations[video_desc->ssrc_] = video_desc;

    // Create screen share track
    SrsRtcTrackDescription *screen_desc = create_video_track(screen_ssrc_, "screen-track-1", "2");
    sub_relations[screen_desc->ssrc_] = screen_desc;

    return sub_relations;
}

SrsRtcSourceDescription *MockRtcTrackDescriptionFactory::create_stream_description()
{
    SrsRtcSourceDescription *stream_desc = new SrsRtcSourceDescription();

    // Create audio track
    stream_desc->audio_track_desc_ = create_audio_track(audio_ssrc_, "audio-track-1", "0");

    // Create video track
    stream_desc->video_track_descs_.push_back(create_video_track(video_ssrc_, "video-track-1", "1"));

    return stream_desc;
}

SrsRtcTrackDescription *MockRtcTrackDescriptionFactory::create_audio_track(uint32_t ssrc, std::string id, std::string mid)
{
    SrsRtcTrackDescription *audio_desc = new SrsRtcTrackDescription();
    audio_desc->type_ = "audio";
    audio_desc->ssrc_ = ssrc;
    audio_desc->id_ = id;
    audio_desc->is_active_ = true;
    audio_desc->direction_ = "sendrecv";
    audio_desc->mid_ = mid;
    audio_desc->media_ = new SrsAudioPayload(audio_pt_, "opus", 48000, 2);
    return audio_desc;
}

SrsRtcTrackDescription *MockRtcTrackDescriptionFactory::create_video_track(uint32_t ssrc, std::string id, std::string mid)
{
    uint8_t pt = video_pt_;
    if (ssrc == screen_ssrc_) {
        pt = screen_pt_;
    }

    SrsRtcTrackDescription *video_desc = new SrsRtcTrackDescription();
    video_desc->type_ = "video";
    video_desc->ssrc_ = ssrc;
    video_desc->id_ = id;
    video_desc->is_active_ = true;
    video_desc->direction_ = "sendrecv";
    video_desc->mid_ = mid;
    video_desc->media_ = new SrsVideoPayload(pt, "H264", 90000);
    return video_desc;
}

// MockExpire implementation
MockExpire::MockExpire() : expired_(false)
{
}

MockExpire::~MockExpire()
{
}

void MockExpire::expire()
{
    expired_ = true;
}

// MockRtcAsyncCallRequest implementation
MockRtcAsyncCallRequest::MockRtcAsyncCallRequest(std::string vhost, std::string app, std::string stream)
{
    vhost_ = vhost;
    app_ = app;
    stream_ = stream;
}

MockRtcAsyncCallRequest::~MockRtcAsyncCallRequest()
{
}

ISrsRequest *MockRtcAsyncCallRequest::copy()
{
    MockRtcAsyncCallRequest *cp = new MockRtcAsyncCallRequest(vhost_, app_, stream_);
    return cp;
}

std::string MockRtcAsyncCallRequest::get_stream_url()
{
    return "rtmp://" + vhost_ + "/" + app_ + "/" + stream_;
}

void MockRtcAsyncCallRequest::update_auth(ISrsRequest *req)
{
}

void MockRtcAsyncCallRequest::strip()
{
}

ISrsRequest *MockRtcAsyncCallRequest::as_http()
{
    return this;
}

MockRtcSource::MockRtcSource()
{
    on_rtp_count_ = 0;
}

MockRtcSource::~MockRtcSource()
{
}

srs_error_t MockRtcSource::on_rtp(SrsRtpPacket *pkt)
{
    on_rtp_count_++;
    return SrsRtcSource::on_rtp(pkt);
}

// MockRtcSourceManager implementation
MockRtcSourceManager::MockRtcSourceManager()
{
    initialize_error_ = srs_success;
    fetch_or_create_error_ = srs_success;
    initialize_count_ = 0;
    fetch_or_create_count_ = 0;
    mock_source_ = SrsSharedPtr<SrsRtcSource>(new MockRtcSource());
}

MockRtcSourceManager::~MockRtcSourceManager()
{
    srs_freep(initialize_error_);
    srs_freep(fetch_or_create_error_);
}

srs_error_t MockRtcSourceManager::initialize()
{
    initialize_count_++;
    return srs_error_copy(initialize_error_);
}

srs_error_t MockRtcSourceManager::fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsRtcSource> &pps)
{
    srs_error_t err = srs_success;

    if (fetch_or_create_count_ == 0) {
        err = mock_source_->initialize(r);
    }

    fetch_or_create_count_++;

    if (fetch_or_create_error_ != srs_success) {
        return srs_error_copy(fetch_or_create_error_);
    }
    pps = mock_source_;

    return err;
}

SrsSharedPtr<SrsRtcSource> MockRtcSourceManager::fetch(ISrsRequest *r)
{
    return mock_source_;
}

void MockRtcSourceManager::set_initialize_error(srs_error_t err)
{
    srs_freep(initialize_error_);
    initialize_error_ = srs_error_copy(err);
}

void MockRtcSourceManager::set_fetch_or_create_error(srs_error_t err)
{
    srs_freep(fetch_or_create_error_);
    fetch_or_create_error_ = srs_error_copy(err);
}

// MockAppStatistic implementation
MockAppStatistic::MockAppStatistic()
{
    on_client_error_ = srs_success;
    on_client_count_ = 0;
    on_disconnect_count_ = 0;
    last_client_id_ = "";
    last_client_req_ = NULL;
    last_client_conn_ = NULL;
    last_client_type_ = SrsRtmpConnUnknown;
}

MockAppStatistic::~MockAppStatistic()
{
    srs_freep(on_client_error_);
}

void MockAppStatistic::on_disconnect(std::string id, srs_error_t err)
{
    on_disconnect_count_++;
}

srs_error_t MockAppStatistic::on_client(std::string id, ISrsRequest *req, ISrsExpire *conn, SrsRtmpConnType type)
{
    on_client_count_++;
    last_client_id_ = id;
    last_client_req_ = req;
    last_client_conn_ = conn;
    last_client_type_ = type;
    return srs_error_copy(on_client_error_);
}

srs_error_t MockAppStatistic::on_video_info(ISrsRequest *req, SrsVideoCodecId vcodec, int avc_profile, int avc_level, int width, int height)
{
    return srs_success;
}

srs_error_t MockAppStatistic::on_audio_info(ISrsRequest *req, SrsAudioCodecId acodec, SrsAudioSampleRate asample_rate, SrsAudioChannels asound_type, SrsAacObjectType aac_object)
{
    return srs_success;
}

void MockAppStatistic::on_stream_publish(ISrsRequest *req, std::string publisher_id)
{
}

void MockAppStatistic::on_stream_close(ISrsRequest *req)
{
}

void MockAppStatistic::kbps_add_delta(std::string id, ISrsKbpsDelta *delta)
{
}

void MockAppStatistic::kbps_sample()
{
}

srs_error_t MockAppStatistic::on_video_frames(ISrsRequest *req, int nb_frames)
{
    return srs_success;
}

std::string MockAppStatistic::server_id()
{
    return "";
}

std::string MockAppStatistic::service_id()
{
    return "";
}

std::string MockAppStatistic::service_pid()
{
    return "";
}

SrsStatisticVhost *MockAppStatistic::find_vhost_by_id(std::string vid)
{
    return NULL;
}

SrsStatisticStream *MockAppStatistic::find_stream(std::string sid)
{
    return NULL;
}

SrsStatisticStream *MockAppStatistic::find_stream_by_url(std::string url)
{
    return NULL;
}

SrsStatisticClient *MockAppStatistic::find_client(std::string client_id)
{
    return NULL;
}

srs_error_t MockAppStatistic::dumps_vhosts(SrsJsonArray *arr)
{
    return srs_success;
}

srs_error_t MockAppStatistic::dumps_streams(SrsJsonArray *arr, int start, int count)
{
    return srs_success;
}

srs_error_t MockAppStatistic::dumps_clients(SrsJsonArray *arr, int start, int count)
{
    return srs_success;
}

srs_error_t MockAppStatistic::dumps_metrics(int64_t &send_bytes, int64_t &recv_bytes, int64_t &nstreams, int64_t &nclients, int64_t &total_nclients, int64_t &nerrs)
{
    return srs_success;
}

void MockAppStatistic::set_on_client_error(srs_error_t err)
{
    srs_freep(on_client_error_);
    on_client_error_ = srs_error_copy(err);
}

// MockRtcAsyncTaskExecutor implementation
MockRtcAsyncTaskExecutor::MockRtcAsyncTaskExecutor()
{
    exec_error_ = srs_success;
    exec_count_ = 0;
    last_task_ = NULL;
}

MockRtcAsyncTaskExecutor::~MockRtcAsyncTaskExecutor()
{
}

srs_error_t MockRtcAsyncTaskExecutor::exec_rtc_async_work(ISrsAsyncCallTask *t)
{
    exec_count_++;
    last_task_ = t;
    return exec_error_;
}

void MockRtcAsyncTaskExecutor::set_exec_error(srs_error_t err)
{
    exec_error_ = err;
}

// MockRtcPacketSender implementation
MockRtcPacketSender::MockRtcPacketSender()
{
    send_packet_error_ = srs_success;
    send_packet_count_ = 0;
    last_sent_packet_ = NULL;
}

MockRtcPacketSender::~MockRtcPacketSender()
{
}

srs_error_t MockRtcPacketSender::do_send_packet(SrsRtpPacket *pkt)
{
    send_packet_count_++;
    last_sent_packet_ = pkt;
    return send_packet_error_;
}

void MockRtcPacketSender::set_send_packet_error(srs_error_t err)
{
    send_packet_error_ = err;
}

// Mock RTC packet receiver implementation
MockRtcPacketReceiver::MockRtcPacketReceiver()
{
    send_rtcp_rr_error_ = srs_success;
    send_rtcp_xr_rrtr_error_ = srs_success;
    send_rtcp_error_ = srs_success;
    send_rtcp_fb_pli_error_ = srs_success;
    send_rtcp_rr_count_ = 0;
    send_rtcp_xr_rrtr_count_ = 0;
    send_rtcp_count_ = 0;
    send_rtcp_fb_pli_count_ = 0;
    check_send_nacks_count_ = 0;
}

MockRtcPacketReceiver::~MockRtcPacketReceiver()
{
}

srs_error_t MockRtcPacketReceiver::send_rtcp_rr(uint32_t ssrc, SrsRtpRingBuffer *rtp_queue, const uint64_t &last_send_systime, const SrsNtp &last_send_ntp)
{
    send_rtcp_rr_count_++;
    return send_rtcp_rr_error_;
}

srs_error_t MockRtcPacketReceiver::send_rtcp_xr_rrtr(uint32_t ssrc)
{
    send_rtcp_xr_rrtr_count_++;
    return send_rtcp_xr_rrtr_error_;
}

void MockRtcPacketReceiver::check_send_nacks(SrsRtpNackForReceiver *nack, uint32_t ssrc, uint32_t &sent_nacks, uint32_t &timeout_nacks)
{
    check_send_nacks_count_++;
    sent_nacks = 0;
    timeout_nacks = 0;
}

srs_error_t MockRtcPacketReceiver::send_rtcp(char *data, int nb_data)
{
    send_rtcp_count_++;
    return send_rtcp_error_;
}

srs_error_t MockRtcPacketReceiver::send_rtcp_fb_pli(uint32_t ssrc, const SrsContextId &cid_of_subscriber)
{
    send_rtcp_fb_pli_count_++;
    return send_rtcp_fb_pli_error_;
}

void MockRtcPacketReceiver::set_send_rtcp_rr_error(srs_error_t err)
{
    send_rtcp_rr_error_ = err;
}

void MockRtcPacketReceiver::set_send_rtcp_xr_rrtr_error(srs_error_t err)
{
    send_rtcp_xr_rrtr_error_ = err;
}

void MockRtcPacketReceiver::set_send_rtcp_error(srs_error_t err)
{
    send_rtcp_error_ = err;
}

void MockRtcPacketReceiver::set_send_rtcp_fb_pli_error(srs_error_t err)
{
    send_rtcp_fb_pli_error_ = err;
}

MockSecurity::MockSecurity()
{
    check_error_ = srs_success;
    check_count_ = 0;
}

MockSecurity::~MockSecurity()
{
    srs_freep(check_error_);
}

srs_error_t MockSecurity::check(SrsRtmpConnType type, std::string ip, ISrsRequest *req)
{
    check_count_++;
    return srs_error_copy(check_error_);
}

// Mock live source manager implementation
MockLiveSourceManager::MockLiveSourceManager()
{
    fetch_or_create_error_ = srs_success;
    fetch_or_create_count_ = 0;
    can_publish_ = true;

    // Create a mock live source
    mock_source_ = SrsSharedPtr<SrsLiveSource>(new MockLiveSource());
}

MockLiveSourceManager::~MockLiveSourceManager()
{
    srs_freep(fetch_or_create_error_);
}

srs_error_t MockLiveSourceManager::fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsLiveSource> &pps)
{
    srs_error_t err = srs_success;

    if (fetch_or_create_count_ == 0) {
        err = mock_source_->initialize(mock_source_, r);
    }

    fetch_or_create_count_++;
    if (fetch_or_create_error_ != srs_success) {
        return srs_error_copy(fetch_or_create_error_);
    }
    pps = mock_source_;
    return err;
}

SrsSharedPtr<SrsLiveSource> MockLiveSourceManager::fetch(ISrsRequest *r)
{
    return mock_source_;
}

void MockLiveSourceManager::dispose()
{
    // Mock implementation - no-op for testing
}

srs_error_t MockLiveSourceManager::initialize()
{
    // Mock implementation - always succeeds
    return srs_success;
}

void MockLiveSourceManager::set_fetch_or_create_error(srs_error_t err)
{
    srs_freep(fetch_or_create_error_);
    fetch_or_create_error_ = srs_error_copy(err);
}

void MockLiveSourceManager::set_can_publish(bool can_publish)
{
    can_publish_ = can_publish;
    if (mock_source_.get()) {
        MockLiveSource *mock_live_source = dynamic_cast<MockLiveSource *>(mock_source_.get());
        if (mock_live_source) {
            mock_live_source->set_can_publish(can_publish);
        }
    }
}

// Mock live source implementation
MockLiveSource::MockLiveSource()
{
    can_publish_result_ = true;
    on_audio_count_ = 0;
    on_video_count_ = 0;
    on_dump_packets_count_ = 0;
    on_frame_count_ = 0;
}

MockLiveSource::~MockLiveSource()
{
}

bool MockLiveSource::can_publish(bool is_edge)
{
    return can_publish_result_;
}

void MockLiveSource::set_can_publish(bool can_publish)
{
    can_publish_result_ = can_publish;
}

srs_error_t MockLiveSource::on_publish()
{
    // Mock implementation - just return success
    return srs_success;
}

srs_error_t MockLiveSource::on_edge_start_publish()
{
    // Mock implementation - just return success
    return srs_success;
}

srs_error_t MockLiveSource::consumer_dumps(ISrsLiveConsumer *consumer, bool ds, bool dm, bool dg)
{
    on_dump_packets_count_++;
    return SrsLiveSource::consumer_dumps(consumer, ds, dm, dg);
}

srs_error_t MockLiveSource::on_audio(SrsRtmpCommonMessage *audio)
{
    return SrsLiveSource::on_audio(audio);
}

srs_error_t MockLiveSource::on_video(SrsRtmpCommonMessage *video)
{
    return SrsLiveSource::on_video(video);
}

srs_error_t MockLiveSource::on_frame(SrsMediaPacket *msg)
{
    on_frame_count_++;
    if (msg->is_audio()) {
        on_audio_count_++;
    } else if (msg->is_video()) {
        on_video_count_++;
    }
    return SrsLiveSource::on_frame(msg);
}

// Mock SRT source implementation
MockSrtSource::MockSrtSource()
{
    can_publish_result_ = true;
    on_publish_count_ = 0;
    on_packet_count_ = 0;
}

MockSrtSource::~MockSrtSource()
{
}

bool MockSrtSource::can_publish()
{
    return can_publish_result_;
}

srs_error_t MockSrtSource::on_publish()
{
    on_publish_count_++;
    return SrsSrtSource::on_publish();
}

srs_error_t MockSrtSource::on_packet(SrsSrtPacket *packet)
{
    on_packet_count_++;
    return SrsSrtSource::on_packet(packet);
}

void MockSrtSource::set_can_publish(bool can_publish)
{
    can_publish_result_ = can_publish;
}

// Mock SRT source manager implementation
MockSrtSourceManager::MockSrtSourceManager()
{
    initialize_error_ = srs_success;
    fetch_or_create_error_ = srs_success;
    initialize_count_ = 0;
    fetch_or_create_count_ = 0;
    can_publish_ = true;

    // Create a mock SRT source
    mock_source_ = SrsSharedPtr<SrsSrtSource>(new MockSrtSource());
}

MockSrtSourceManager::~MockSrtSourceManager()
{
    srs_freep(initialize_error_);
    srs_freep(fetch_or_create_error_);
}

srs_error_t MockSrtSourceManager::initialize()
{
    initialize_count_++;
    return srs_error_copy(initialize_error_);
}

srs_error_t MockSrtSourceManager::fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsSrtSource> &pps)
{
    srs_error_t err = srs_success;
    if (fetch_or_create_count_ == 0) {
        err = mock_source_->initialize(r);
    }

    fetch_or_create_count_++;
    if (fetch_or_create_error_ != srs_success) {
        return srs_error_copy(fetch_or_create_error_);
    }
    pps = mock_source_;

    return err;
}

SrsSharedPtr<SrsSrtSource> MockSrtSourceManager::fetch(ISrsRequest *r)
{
    return mock_source_;
}

void MockSrtSourceManager::set_initialize_error(srs_error_t err)
{
    srs_freep(initialize_error_);
    initialize_error_ = srs_error_copy(err);
}

void MockSrtSourceManager::set_fetch_or_create_error(srs_error_t err)
{
    srs_freep(fetch_or_create_error_);
    fetch_or_create_error_ = srs_error_copy(err);
}

void MockSrtSourceManager::set_can_publish(bool can_publish)
{
    can_publish_ = can_publish;
    if (mock_source_.get()) {
        MockSrtSource *mock_srt_source = dynamic_cast<MockSrtSource *>(mock_source_.get());
        if (mock_srt_source) {
            mock_srt_source->set_can_publish(can_publish);
        }
    }
}

MockRtmpServer::MockRtmpServer()
{
    type_ = SrsRtmpConnFMLEPublish;
    stream_ = "livestream";
    duration_ = 0;

    recv_err_ = srs_success;
    cond_ = new SrsCond();

    nb_sent_messages_ = 0;
    start_play_error_ = srs_success;
    start_publish_error_ = srs_success;
    start_play_count_ = 0;
    start_fmle_publish_count_ = 0;
    start_flash_publish_count_ = 0;
    start_haivision_publish_count_ = 0;

    // Initialize fields for handle_publish_message testing
    decode_message_error_ = srs_success;
    decode_message_packet_ = NULL;
    decode_message_count_ = 0;
    fmle_unpublish_error_ = srs_success;
    fmle_unpublish_count_ = 0;

    // Initialize fields for process_play_control_msg testing
    send_and_free_packet_count_ = 0;
    on_play_client_pause_count_ = 0;
    last_pause_state_ = false;

    // Initialize fields for set_auto_response testing
    set_auto_response_called_ = false;
    auto_response_value_ = true;
}

MockRtmpServer::~MockRtmpServer()
{
    srs_freep(start_play_error_);
    srs_freep(start_publish_error_);
    srs_freep(recv_err_);
    srs_freep(cond_);
    srs_freep(decode_message_error_);
    srs_freep(decode_message_packet_);
    srs_freep(fmle_unpublish_error_);

    for (vector<SrsRtmpCommonMessage *>::iterator it = recv_msgs_.begin(); it != recv_msgs_.end(); ++it) {
        SrsRtmpCommonMessage *msg = *it;
        srs_freep(msg);
    }
    recv_msgs_.clear();
}

void MockRtmpServer::set_request(SrsRtmpConnType type, std::string ip, std::string vhost, std::string app, std::string stream, std::string tcUrl, std::string schema, int port, std::string host)
{
    type_ = type;
    ip_ = ip;
    vhost_ = vhost;
    app_ = app;
    stream_ = stream;
    tcUrl_ = tcUrl;
    schema_ = schema;
    port_ = port;
    host_ = host;
}

void MockRtmpServer::set_recv_timeout(srs_utime_t tm)
{
}

void MockRtmpServer::set_send_timeout(srs_utime_t tm)
{
}

srs_error_t MockRtmpServer::handshake()
{
    return srs_success;
}

srs_error_t MockRtmpServer::connect_app(ISrsRequest *req)
{
    req->ip_ = ip_;
    req->vhost_ = vhost_;
    req->app_ = app_;
    req->tcUrl_ = tcUrl_;
    req->schema_ = schema_;
    req->host_ = host_;
    req->port_ = port_;

    return srs_success;
}

uint32_t MockRtmpServer::proxy_real_ip()
{
    return 0;
}

srs_error_t MockRtmpServer::set_window_ack_size(int ack_size)
{
    return srs_success;
}

srs_error_t MockRtmpServer::set_in_window_ack_size(int ack_size)
{
    return srs_success;
}

srs_error_t MockRtmpServer::set_peer_bandwidth(int bandwidth, int type)
{
    return srs_success;
}

srs_error_t MockRtmpServer::set_chunk_size(int chunk_size)
{
    return srs_success;
}

srs_error_t MockRtmpServer::response_connect_app(ISrsRequest *req, const char *server_ip)
{
    return srs_success;
}

srs_error_t MockRtmpServer::on_bw_done()
{
    return srs_success;
}

srs_error_t MockRtmpServer::identify_client(int stream_id, SrsRtmpConnType &type, std::string &stream_name, srs_utime_t &duration)
{
    type = type_;
    stream_name = stream_;
    duration = duration_;
    return srs_success;
}

srs_error_t MockRtmpServer::start_play(int stream_id)
{
    start_play_count_++;
    return srs_error_copy(start_play_error_);
}

srs_error_t MockRtmpServer::start_fmle_publish(int stream_id)
{
    start_fmle_publish_count_++;
    return srs_error_copy(start_publish_error_);
}

srs_error_t MockRtmpServer::start_haivision_publish(int stream_id)
{
    start_haivision_publish_count_++;
    return srs_success;
}

srs_error_t MockRtmpServer::fmle_unpublish(int stream_id, double unpublish_tid)
{
    fmle_unpublish_count_++;
    return srs_error_copy(fmle_unpublish_error_);
}

srs_error_t MockRtmpServer::start_flash_publish(int stream_id)
{
    start_flash_publish_count_++;
    return srs_success;
}

srs_error_t MockRtmpServer::start_publishing(int stream_id)
{
    return srs_success;
}

srs_error_t MockRtmpServer::redirect(ISrsRequest *r, std::string url, bool &accepted)
{
    return srs_success;
}

srs_error_t MockRtmpServer::send_and_free_messages(SrsMediaPacket **msgs, int nb_msgs, int stream_id)
{
    nb_sent_messages_ += nb_msgs;
    return srs_success;
}

srs_error_t MockRtmpServer::decode_message(SrsRtmpCommonMessage *msg, SrsRtmpCommand **ppacket)
{
    decode_message_count_++;
    if (decode_message_error_ != srs_success) {
        return srs_error_copy(decode_message_error_);
    }
    *ppacket = decode_message_packet_;
    decode_message_packet_ = NULL; // Transfer ownership
    return srs_success;
}

srs_error_t MockRtmpServer::send_and_free_packet(SrsRtmpCommand *packet, int stream_id)
{
    send_and_free_packet_count_++;
    srs_freep(packet);
    return srs_success;
}

srs_error_t MockRtmpServer::on_play_client_pause(int stream_id, bool is_pause)
{
    on_play_client_pause_count_++;
    last_pause_state_ = is_pause;
    return srs_success;
}

srs_error_t MockRtmpServer::recv_message(SrsRtmpCommonMessage **pmsg)
{
    // No message received during playing util get control event.
    if (recv_msgs_.empty()) {
        cond_->wait();
    }

    if (recv_err_ != srs_success) {
        return srs_error_copy(recv_err_);
    }

    if (recv_msgs_.empty()) {
        return srs_error_new(ERROR_SOCKET_READ, "mock rtmp server no message");
    }

    SrsRtmpCommonMessage *msg = recv_msgs_.front();
    recv_msgs_.erase(recv_msgs_.begin());

    if (pmsg) {
        *pmsg = msg;
    } else {
        srs_freep(msg);
    }

    return srs_success;
}

void MockRtmpServer::set_merge_read(bool v, IMergeReadHandler *handler)
{
}

void MockRtmpServer::set_recv_buffer(int buffer_size)
{
}

void MockRtmpServer::set_auto_response(bool v)
{
    set_auto_response_called_ = true;
    auto_response_value_ = v;
}

// Mock ISrsProtocolReadWriter implementation for testing SrsHttpConn::cycle()
MockProtocolReadWriter::MockProtocolReadWriter()
{
    recv_timeout_ = SRS_UTIME_NO_TIMEOUT;
    send_timeout_ = SRS_UTIME_NO_TIMEOUT;
    read_count_ = 0;
    recv_bytes_ = 0;

    read_error_ = srs_success;
    cond_ = new SrsCond();
}

MockProtocolReadWriter::~MockProtocolReadWriter()
{
    srs_freep(read_error_);
    srs_freep(cond_);
    recv_msgs_.clear();
}

srs_error_t MockProtocolReadWriter::read_fully(void *buf, size_t size, ssize_t *nread)
{
    return srs_success;
}

srs_error_t MockProtocolReadWriter::read(void *buf, size_t size, ssize_t *nread)
{
    // No message received during playing util get control event.
    if (recv_msgs_.empty()) {
        cond_->wait();
    }

    read_count_++;

    if (read_error_ != srs_success) {
        return srs_error_copy(read_error_);
    }

    if (recv_msgs_.empty()) {
        return srs_error_new(ERROR_SOCKET_READ, "mock rtmp server no message");
    }

    string test_data_ = recv_msgs_.front();
    recv_msgs_.erase(recv_msgs_.begin());

    // Simulate reading data
    size_t copy_size = srs_min(size, test_data_.size());
    memcpy(buf, test_data_.c_str(), copy_size);
    if (nread) {
        *nread = copy_size;
    }
    recv_bytes_ += copy_size;

    return srs_success;
}

void MockProtocolReadWriter::set_recv_timeout(srs_utime_t tm)
{
    recv_timeout_ = tm;
}

srs_utime_t MockProtocolReadWriter::get_recv_timeout()
{
    return recv_timeout_;
}

int64_t MockProtocolReadWriter::get_recv_bytes()
{
    return 0;
}

srs_error_t MockProtocolReadWriter::write(void *buf, size_t size, ssize_t *nwrite)
{
    return srs_success;
}

void MockProtocolReadWriter::set_send_timeout(srs_utime_t tm)
{
    send_timeout_ = tm;
}

srs_utime_t MockProtocolReadWriter::get_send_timeout()
{
    return send_timeout_;
}

int64_t MockProtocolReadWriter::get_send_bytes()
{
    return 0;
}

srs_error_t MockProtocolReadWriter::writev(const iovec *iov, int iov_size, ssize_t *nwrite)
{
    return srs_success;
}

MockRtmpTransport::MockRtmpTransport()
{
}

MockRtmpTransport::~MockRtmpTransport()
{
}

srs_netfd_t MockRtmpTransport::fd()
{
    return NULL;
}

int MockRtmpTransport::osfd()
{
    return -1;
}

ISrsProtocolReadWriter *MockRtmpTransport::io()
{
    return &mock_io_;
}

srs_error_t MockRtmpTransport::handshake()
{
    return srs_success;
}

const char *MockRtmpTransport::transport_type()
{
    return "mock";
}

srs_error_t MockRtmpTransport::set_socket_buffer(srs_utime_t buffer_v)
{
    return srs_success;
}

srs_error_t MockRtmpTransport::set_tcp_nodelay(bool v)
{
    return srs_success;
}

int64_t MockRtmpTransport::get_recv_bytes()
{
    return 0;
}

int64_t MockRtmpTransport::get_send_bytes()
{
    return 0;
}

srs_error_t MockRtmpTransport::read(void *buf, size_t size, ssize_t *nread)
{
    return srs_success;
}

srs_error_t MockRtmpTransport::read_fully(void *buf, size_t size, ssize_t *nread)
{
    return srs_success;
}

void MockRtmpTransport::set_recv_timeout(srs_utime_t tm)
{
}

srs_utime_t MockRtmpTransport::get_recv_timeout()
{
    return 0;
}

srs_error_t MockRtmpTransport::write(void *buf, size_t size, ssize_t *nwrite)
{
    return srs_success;
}

void MockRtmpTransport::set_send_timeout(srs_utime_t tm)
{
}

srs_utime_t MockRtmpTransport::get_send_timeout()
{
    return 0;
}

srs_error_t MockRtmpTransport::writev(const iovec *iov, int iov_size, ssize_t *nwrite)
{
    return srs_success;
}

// Mock ISrsSslConnection implementation for testing HTTPS connections
MockSslConnection::MockSslConnection()
{
    handshake_called_ = false;
    handshake_error_ = srs_success;
    recv_timeout_ = SRS_UTIME_NO_TIMEOUT;
    send_timeout_ = SRS_UTIME_NO_TIMEOUT;
    recv_bytes_ = 0;
    send_bytes_ = 0;
    read_count_ = 0;

    read_error_ = srs_success;
    cond_ = new SrsCond();
}

MockSslConnection::~MockSslConnection()
{
    srs_freep(handshake_error_);

    srs_freep(read_error_);
    srs_freep(cond_);
    recv_msgs_.clear();
}

srs_error_t MockSslConnection::handshake(std::string key_file, std::string crt_file)
{
    handshake_called_ = true;
    return srs_error_copy(handshake_error_);
}

void MockSslConnection::set_recv_timeout(srs_utime_t tm)
{
    recv_timeout_ = tm;
}

srs_utime_t MockSslConnection::get_recv_timeout()
{
    return recv_timeout_;
}

srs_error_t MockSslConnection::read_fully(void *buf, size_t size, ssize_t *nread)
{
    return srs_error_new(ERROR_NOT_SUPPORTED, "mock ssl read_fully");
}

int64_t MockSslConnection::get_recv_bytes()
{
    return recv_bytes_;
}

int64_t MockSslConnection::get_send_bytes()
{
    return send_bytes_;
}

srs_error_t MockSslConnection::read(void *buf, size_t size, ssize_t *nread)
{
    // No message received during playing util get control event.
    if (recv_msgs_.empty()) {
        cond_->wait();
    }

    read_count_++;

    if (read_error_ != srs_success) {
        return srs_error_copy(read_error_);
    }

    if (recv_msgs_.empty()) {
        return srs_error_new(ERROR_SOCKET_READ, "mock rtmp server no message");
    }

    string test_data_ = recv_msgs_.front();
    recv_msgs_.erase(recv_msgs_.begin());

    // Simulate reading data
    size_t copy_size = srs_min(size, test_data_.size());
    memcpy(buf, test_data_.c_str(), copy_size);
    if (nread) {
        *nread = copy_size;
    }
    recv_bytes_ += copy_size;

    return srs_success;
}

void MockSslConnection::set_send_timeout(srs_utime_t tm)
{
    send_timeout_ = tm;
}

srs_utime_t MockSslConnection::get_send_timeout()
{
    return send_timeout_;
}

srs_error_t MockSslConnection::write(void *buf, size_t size, ssize_t *nwrite)
{
    *nwrite = size;
    send_bytes_ += size;
    return srs_success;
}

srs_error_t MockSslConnection::writev(const iovec *iov, int iov_size, ssize_t *nwrite)
{
    return srs_error_new(ERROR_NOT_SUPPORTED, "mock ssl writev");
}

// Mock ISrsProtocolReadWriter implementation for SrsSrtRecvThread
MockSrtConnection::MockSrtConnection()
{
    read_count_ = 0;
    simulate_timeout_ = false;
    recv_timeout_ = 1 * SRS_UTIME_SECONDS;
    send_timeout_ = 1 * SRS_UTIME_SECONDS;
    recv_bytes_ = 0;
    send_bytes_ = 0;
    streamid_ = "test_streamid";
    srt_fd_ = 1;

    read_error_ = srs_success;
    cond_ = new SrsCond();
}

MockSrtConnection::~MockSrtConnection()
{
    srs_freep(read_error_);
    srs_freep(cond_);
    recv_msgs_.clear();
}

srs_error_t MockSrtConnection::read(void *buf, size_t size, ssize_t *nread)
{
    // Simulate timeout error
    if (simulate_timeout_) {
        return srs_error_new(ERROR_SRT_TIMEOUT, "srt timeout");
    }

    // No message received during playing util get control event.
    if (recv_msgs_.empty()) {
        cond_->wait();
    }

    read_count_++;

    if (read_error_ != srs_success) {
        return srs_error_copy(read_error_);
    }

    if (recv_msgs_.empty()) {
        return srs_error_new(ERROR_SOCKET_READ, "mock rtmp server no message");
    }

    string test_data_ = recv_msgs_.front();
    recv_msgs_.erase(recv_msgs_.begin());

    // Simulate reading data
    size_t copy_size = srs_min(size, test_data_.size());
    memcpy(buf, test_data_.c_str(), copy_size);
    if (nread) {
        *nread = copy_size;
    }
    recv_bytes_ += copy_size;

    return srs_success;
}

srs_error_t MockSrtConnection::read_fully(void *buf, size_t size, ssize_t *nread)
{
    return srs_error_new(ERROR_NOT_SUPPORTED, "not supported");
}

srs_error_t MockSrtConnection::write(void *buf, size_t size, ssize_t *nwrite)
{
    *nwrite = size;
    send_bytes_ += size;
    return srs_success;
}

srs_error_t MockSrtConnection::writev(const iovec *iov, int iov_size, ssize_t *nwrite)
{
    return srs_error_new(ERROR_NOT_SUPPORTED, "not supported");
}

void MockSrtConnection::set_recv_timeout(srs_utime_t tm)
{
    recv_timeout_ = tm;
}

srs_utime_t MockSrtConnection::get_recv_timeout()
{
    return recv_timeout_;
}

int64_t MockSrtConnection::get_recv_bytes()
{
    return recv_bytes_;
}

void MockSrtConnection::set_send_timeout(srs_utime_t tm)
{
    send_timeout_ = tm;
}

srs_utime_t MockSrtConnection::get_send_timeout()
{
    return send_timeout_;
}

int64_t MockSrtConnection::get_send_bytes()
{
    return send_bytes_;
}

srs_srt_t MockSrtConnection::srtfd()
{
    return srt_fd_;
}

srs_error_t MockSrtConnection::get_streamid(std::string &streamid)
{
    streamid = streamid_;
    return srs_success;
}

srs_error_t MockSrtConnection::get_stats(SrsSrtStat &stat)
{
    return srs_success;
}

// Mock ISrsHttpParser implementation for testing SrsHttpConn::cycle()
MockHttpParser::MockHttpParser()
{
    initialize_called_ = false;
    parse_message_called_ = false;
    initialize_error_ = srs_success;
    parse_message_error_ = srs_success;
    cond_ = new SrsCond();
}

MockHttpParser::~MockHttpParser()
{
    initialize_called_ = false;
    parse_message_called_ = false;
    srs_freep(initialize_error_);
    srs_freep(parse_message_error_);

    for (vector<ISrsHttpMessage *>::iterator it = messages_.begin(); it != messages_.end(); ++it) {
        ISrsHttpMessage *msg = *it;
        srs_freep(msg);
    }
    messages_.clear();

    srs_freep(cond_);
}

srs_error_t MockHttpParser::initialize(enum llhttp_type type)
{
    initialize_called_ = true;
    if (initialize_error_ != srs_success) {
        return srs_error_copy(initialize_error_);
    }
    return srs_success;
}

void MockHttpParser::set_jsonp(bool allow_jsonp)
{
}

srs_error_t MockHttpParser::parse_message(ISrsReader *reader, ISrsHttpMessage **ppmsg)
{
    parse_message_called_ = true;

    if (messages_.empty()) {
        cond_->wait();
    }

    if (parse_message_error_ != srs_success) {
        return srs_error_copy(parse_message_error_);
    }

    if (messages_.empty()) {
        return srs_error_new(ERROR_SOCKET_READ, "mock http parser no message");
    }

    ISrsHttpMessage *msg = messages_.front();
    messages_.erase(messages_.begin());
    if (ppmsg) {
        *ppmsg = msg;
    } else {
        srs_freep(msg);
    }

    return srs_success;
}

// Old mock implementations for backward compatibility
MockHttpxConn::MockHttpxConn()
{
    enable_stat_ = false;
    on_start_called_ = false;
    on_http_message_called_ = false;
    on_message_done_called_ = false;
    on_conn_done_called_ = false;
    on_start_error_ = srs_success;
    on_http_message_error_ = srs_success;
    on_message_done_error_ = srs_success;
    on_conn_done_error_ = srs_success;
}

MockHttpxConn::~MockHttpxConn()
{
    srs_freep(on_start_error_);
    srs_freep(on_http_message_error_);
    srs_freep(on_message_done_error_);
    srs_freep(on_conn_done_error_);
}

void MockHttpxConn::set_enable_stat(bool v)
{
    enable_stat_ = v;
}

srs_error_t MockHttpxConn::on_start()
{
    on_start_called_ = true;
    if (on_start_error_ != srs_success) {
        return srs_error_copy(on_start_error_);
    }
    return srs_success;
}

srs_error_t MockHttpxConn::on_http_message(ISrsHttpMessage *r, ISrsHttpResponseWriter *w)
{
    on_http_message_called_ = true;
    if (on_http_message_error_ != srs_success) {
        return srs_error_copy(on_http_message_error_);
    }
    return srs_success;
}

srs_error_t MockHttpxConn::on_message_done(ISrsHttpMessage *r, ISrsHttpResponseWriter *w)
{
    on_message_done_called_ = true;
    if (on_message_done_error_ != srs_success) {
        return srs_error_copy(on_message_done_error_);
    }
    return srs_success;
}

srs_error_t MockHttpxConn::on_conn_done(srs_error_t r0)
{
    on_conn_done_called_ = true;
    if (on_conn_done_error_ != srs_success) {
        srs_freep(r0);
        return srs_error_copy(on_conn_done_error_);
    }
    return r0;
}

MockHttpConn::MockHttpConn()
{
    handler_ = new MockHttpxConn();
    remote_ip_ = "127.0.0.1";
}

MockHttpConn::~MockHttpConn()
{
    srs_freep(handler_);
}

std::string MockHttpConn::remote_ip()
{
    return remote_ip_;
}

const SrsContextId &MockHttpConn::get_id()
{
    static SrsContextId id;
    return id;
}

std::string MockHttpConn::desc()
{
    return "MockHttpConn";
}

void MockHttpConn::expire()
{
}

ISrsHttpConnOwner *MockHttpConn::handler()
{
    return handler_;
}

MockHttpMessage::MockHttpMessage() : SrsHttpMessage()
{
    mock_conn_ = new MockHttpConn();
    set_connection(mock_conn_);
}

MockHttpMessage::~MockHttpMessage()
{
    srs_freep(mock_conn_);
}

std::string MockHttpMessage::path()
{
    return "/live/stream.flv";
}

MockHttpServeMux::MockHttpServeMux()
{
    handle_count_ = 0;
    serve_http_count_ = 0;
}

MockHttpServeMux::~MockHttpServeMux()
{
}

srs_error_t MockHttpServeMux::handle(std::string pattern, ISrsHttpHandler *handler)
{
    handle_count_++;
    patterns_.push_back(pattern);
    // Free the handler since we're not actually using it
    srs_freep(handler);
    return srs_success;
}

srs_error_t MockHttpServeMux::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    serve_http_count_++;
    return srs_success;
}

// Mock request implementation for SrsBufferCache testing
MockRequest::MockRequest(std::string vhost, std::string app, std::string stream)
{
    vhost_ = vhost;
    app_ = app;
    stream_ = stream;
    host_ = "127.0.0.1";
    port_ = 1935;
    tcUrl_ = "rtmp://127.0.0.1/" + app;
    schema_ = "rtmp";
    param_ = "";
    duration_ = 0;
    args_ = NULL;
    protocol_ = "rtmp";
    objectEncoding_ = 0;
}

MockRequest::~MockRequest()
{
}

ISrsRequest *MockRequest::copy()
{
    MockRequest *req = new MockRequest(vhost_, app_, stream_);
    req->host_ = host_;
    req->port_ = port_;
    req->tcUrl_ = tcUrl_;
    req->pageUrl_ = pageUrl_;
    req->swfUrl_ = swfUrl_;
    req->schema_ = schema_;
    req->param_ = param_;
    req->duration_ = duration_;
    req->protocol_ = protocol_;
    req->objectEncoding_ = objectEncoding_;
    req->ip_ = ip_;
    return req;
}

std::string MockRequest::get_stream_url()
{
    if (vhost_ == "__defaultVhost__" || vhost_.empty()) {
        return "/" + app_ + "/" + stream_;
    } else {
        return vhost_ + "/" + app_ + "/" + stream_;
    }
}

void MockRequest::update_auth(ISrsRequest *req)
{
    if (req) {
        pageUrl_ = req->pageUrl_;
        swfUrl_ = req->swfUrl_;
        tcUrl_ = req->tcUrl_;
    }
}

void MockRequest::strip()
{
    // Mock implementation - basic string cleanup
    host_ = srs_strings_remove(host_, "/ \n\r\t");
    vhost_ = srs_strings_remove(vhost_, "/ \n\r\t");
    app_ = srs_strings_remove(app_, " \n\r\t");
    stream_ = srs_strings_remove(stream_, " \n\r\t");

    app_ = srs_strings_trim_end(app_, "/");
    stream_ = srs_strings_trim_end(stream_, "/");
}

ISrsRequest *MockRequest::as_http()
{
    return copy();
}

MockBufferCache::MockBufferCache()
{
    dump_cache_count_ = 0;
    last_consumer_ = NULL;
    last_jitter_ = SrsRtmpJitterAlgorithmOFF;
}

MockBufferCache::~MockBufferCache()
{
}

srs_error_t MockBufferCache::start()
{
    return srs_success;
}

void MockBufferCache::stop()
{
}

bool MockBufferCache::alive()
{
    return true;
}

srs_error_t MockBufferCache::dump_cache(ISrsLiveConsumer *consumer, SrsRtmpJitterAlgorithm jitter)
{
    dump_cache_count_++;
    last_consumer_ = consumer;
    last_jitter_ = jitter;
    return srs_success;
}

srs_error_t MockBufferCache::update_auth(ISrsRequest *r)
{
    return srs_success;
}

// Mock HTTP hooks implementation
MockHttpHooks::MockHttpHooks()
{
    on_stop_count_ = 0;
    on_unpublish_count_ = 0;
}

MockHttpHooks::~MockHttpHooks()
{
    clear_calls();
}

srs_error_t MockHttpHooks::on_connect(std::string url, ISrsRequest *req)
{
    return srs_success;
}

void MockHttpHooks::on_close(std::string url, ISrsRequest *req, int64_t send_bytes, int64_t recv_bytes)
{
}

srs_error_t MockHttpHooks::on_publish(std::string url, ISrsRequest *req)
{
    return srs_success;
}

void MockHttpHooks::on_unpublish(std::string url, ISrsRequest *req)
{
    on_unpublish_count_++;
    on_unpublish_calls_.push_back(std::make_pair(url, req));
}

srs_error_t MockHttpHooks::on_play(std::string url, ISrsRequest *req)
{
    return srs_success;
}

void MockHttpHooks::on_stop(std::string url, ISrsRequest *req)
{
    on_stop_count_++;
    on_stop_calls_.push_back(std::make_pair(url, req));
}

srs_error_t MockHttpHooks::on_dvr(SrsContextId cid, std::string url, ISrsRequest *req, std::string file)
{
    return srs_success;
}

srs_error_t MockHttpHooks::on_hls(SrsContextId cid, std::string url, ISrsRequest *req, std::string file, std::string ts_url,
                                  std::string m3u8, std::string m3u8_url, int sn, srs_utime_t duration)
{
    return srs_success;
}

srs_error_t MockHttpHooks::on_hls_notify(SrsContextId cid, std::string url, ISrsRequest *req, std::string ts_url, int nb_notify)
{
    return srs_success;
}

srs_error_t MockHttpHooks::discover_co_workers(std::string url, std::string &host, int &port)
{
    return srs_success;
}

srs_error_t MockHttpHooks::on_forward_backend(std::string url, ISrsRequest *req, std::vector<std::string> &rtmp_urls)
{
    return srs_success;
}

void MockHttpHooks::clear_calls()
{
    on_stop_calls_.clear();
    on_stop_count_ = 0;
    on_unpublish_calls_.clear();
    on_unpublish_count_ = 0;
}

// Mock origin hub implementation
MockOriginHub::MockOriginHub()
{
    initialize_count_ = 0;
    initialize_error_ = srs_success;
    on_hls_request_sh_count_ = 0;
    on_hls_request_sh_error_ = srs_success;
    on_forwarder_start_count_ = 0;
    on_forwarder_start_error_ = srs_success;
    on_dvr_request_sh_count_ = 0;
    on_dvr_request_sh_error_ = srs_success;
}

MockOriginHub::~MockOriginHub()
{
    srs_freep(initialize_error_);
    srs_freep(on_hls_request_sh_error_);
    srs_freep(on_forwarder_start_error_);
    srs_freep(on_dvr_request_sh_error_);
}

srs_error_t MockOriginHub::initialize(SrsSharedPtr<SrsLiveSource> s, ISrsRequest *r)
{
    initialize_count_++;
    return srs_error_copy(initialize_error_);
}

void MockOriginHub::dispose()
{
}

srs_error_t MockOriginHub::cycle()
{
    return srs_success;
}

bool MockOriginHub::active()
{
    return true;
}

srs_utime_t MockOriginHub::cleanup_delay()
{
    return 0;
}

srs_error_t MockOriginHub::on_meta_data(SrsMediaPacket *shared_metadata, SrsOnMetaDataPacket *packet)
{
    return srs_success;
}

srs_error_t MockOriginHub::on_audio(SrsMediaPacket *shared_audio)
{
    return srs_success;
}

srs_error_t MockOriginHub::on_video(SrsMediaPacket *shared_video, bool is_sequence_header)
{
    return srs_success;
}

srs_error_t MockOriginHub::on_publish()
{
    return srs_success;
}

void MockOriginHub::on_unpublish()
{
}

srs_error_t MockOriginHub::on_dvr_request_sh()
{
    on_dvr_request_sh_count_++;
    return srs_error_copy(on_dvr_request_sh_error_);
}

srs_error_t MockOriginHub::on_forwarder_start(SrsForwarder *forwarder)
{
    on_forwarder_start_count_++;
    return srs_error_copy(on_forwarder_start_error_);
}

srs_error_t MockOriginHub::on_hls_request_sh()
{
    on_hls_request_sh_count_++;
    return srs_error_copy(on_hls_request_sh_error_);
}

void MockOriginHub::set_initialize_error(srs_error_t err)
{
    srs_freep(initialize_error_);
    initialize_error_ = srs_error_copy(err);
}

void MockOriginHub::set_on_hls_request_sh_error(srs_error_t err)
{
    srs_freep(on_hls_request_sh_error_);
    on_hls_request_sh_error_ = srs_error_copy(err);
}

void MockOriginHub::set_on_dvr_request_sh_error(srs_error_t err)
{
    srs_freep(on_dvr_request_sh_error_);
    on_dvr_request_sh_error_ = srs_error_copy(err);
}

void MockOriginHub::set_on_forwarder_start_error(srs_error_t err)
{
    srs_freep(on_forwarder_start_error_);
    on_forwarder_start_error_ = srs_error_copy(err);
}

// MockAudioCache implementation
MockAudioCache::MockAudioCache()
{
    process_packet_count_ = 0;
}

MockAudioCache::~MockAudioCache()
{
}

srs_error_t MockAudioCache::process_packet(SrsRtpPacket *src, std::vector<SrsRtpPacket *> &ready_packets)
{
    process_packet_count_++;

    // Copy the packet.
    SrsRtpPacket *copy = src->copy();
    ready_packets.push_back(copy);

    return srs_success;
}

void MockAudioCache::clear_all()
{
}

// Mock ISrsBasicRtmpClient implementation
MockRtmpClient::MockRtmpClient()
{
    connect_called_ = false;
    publish_called_ = false;
    play_called_ = false;
    close_called_ = false;
    recv_message_called_ = false;
    decode_message_called_ = false;
    set_recv_timeout_called_ = false;
    kbps_sample_called_ = false;
    send_and_free_message_called_ = false;
    connect_error_ = srs_success;
    publish_error_ = srs_success;
    play_error_ = srs_success;
    send_and_free_message_error_ = srs_success;
    publish_chunk_size_ = 0;
    stream_id_ = 1;
    recv_timeout_ = 0;
    kbps_age_ = 0;
    send_message_count_ = 0;
    send_and_free_messages_count_ = 0;

    recv_err_ = srs_success;
    cond_ = new SrsCond();
}

MockRtmpClient::~MockRtmpClient()
{
    srs_freep(connect_error_);
    srs_freep(publish_error_);
    srs_freep(play_error_);
    srs_freep(send_and_free_message_error_);

    srs_freep(recv_err_);
    srs_freep(cond_);

    for (vector<SrsRtmpCommonMessage *>::iterator it = recv_msgs_.begin(); it != recv_msgs_.end(); ++it) {
        SrsRtmpCommonMessage *msg = *it;
        srs_freep(msg);
    }
    recv_msgs_.clear();
}

srs_error_t MockRtmpClient::connect()
{
    connect_called_ = true;
    return srs_error_copy(connect_error_);
}

void MockRtmpClient::close()
{
    close_called_ = true;
}

srs_error_t MockRtmpClient::publish(int chunk_size, bool with_vhost, std::string *pstream)
{
    publish_called_ = true;
    publish_chunk_size_ = chunk_size;
    if (pstream) {
        publish_stream_ = *pstream;
    }
    return srs_error_copy(publish_error_);
}

srs_error_t MockRtmpClient::play(int chunk_size, bool with_vhost, std::string *pstream)
{
    play_called_ = true;
    if (pstream) {
        // Return the stream name if play_stream_ is set, otherwise use the input
        if (!play_stream_.empty()) {
            *pstream = play_stream_;
        } else {
            *pstream = "livestream"; // Default stream name for compatibility
            play_stream_ = *pstream;
        }
    }
    return srs_error_copy(play_error_);
}

void MockRtmpClient::kbps_sample(const char *label, srs_utime_t age)
{
    kbps_sample_called_ = true;
    if (label) {
        kbps_label_ = label;
    }
    kbps_age_ = age;
}

void MockRtmpClient::kbps_sample(const char *label, srs_utime_t age, int msgs)
{
    kbps_sample_called_ = true;
    if (label) {
        kbps_label_ = label;
    }
    kbps_age_ = age;
}

int MockRtmpClient::sid()
{
    return stream_id_;
}

srs_error_t MockRtmpClient::recv_message(SrsRtmpCommonMessage **pmsg)
{
    recv_message_called_ = true;

    // No message received during playing util get control event.
    if (recv_msgs_.empty()) {
        cond_->wait();
    }

    if (recv_err_ != srs_success) {
        return srs_error_copy(recv_err_);
    }

    if (recv_msgs_.empty()) {
        return srs_error_new(ERROR_SOCKET_READ, "mock rtmp server no message");
    }

    SrsRtmpCommonMessage *msg = recv_msgs_.front();
    recv_msgs_.erase(recv_msgs_.begin());

    if (pmsg) {
        *pmsg = msg;
    } else {
        srs_freep(msg);
    }

    return srs_success;
}

srs_error_t MockRtmpClient::decode_message(SrsRtmpCommonMessage *msg, SrsRtmpCommand **ppacket)
{
    decode_message_called_ = true;
    return srs_success;
}

srs_error_t MockRtmpClient::send_and_free_messages(SrsMediaPacket **msgs, int nb_msgs)
{
    send_and_free_messages_count_ += nb_msgs;
    return srs_success;
}

srs_error_t MockRtmpClient::send_and_free_message(SrsMediaPacket *msg)
{
    send_and_free_message_called_ = true;
    send_message_count_++;
    return srs_error_copy(send_and_free_message_error_);
}

void MockRtmpClient::set_recv_timeout(srs_utime_t timeout)
{
    set_recv_timeout_called_ = true;
    recv_timeout_ = timeout;
}

void MockRtmpClient::set_url(std::string url)
{
    url_ = url;
}

MockAudioTranscoder::MockAudioTranscoder()
{
    transcode_count_ = 0;
    // Set default AAC header for mock transcoder
    aac_header_ = std::string("\xAF\x00\x12\x10", 4);
}

MockAudioTranscoder::~MockAudioTranscoder()
{
}

srs_error_t MockAudioTranscoder::initialize(SrsAudioCodecId from, SrsAudioCodecId to, int channels, int sample_rate, int bit_rate)
{
    return srs_success;
}

srs_error_t MockAudioTranscoder::transcode(SrsParsedAudioPacket *in, std::vector<SrsParsedAudioPacket *> &outs)
{
    transcode_count_++;

    SrsParsedAudioPacket *out = in->copy();
    output_packets_.push_back(out);
    outs.push_back(out);

    return srs_success;
}

void MockAudioTranscoder::free_frames(std::vector<SrsParsedAudioPacket *> &frames)
{
    for (std::vector<SrsParsedAudioPacket *>::iterator it = frames.begin(); it != frames.end(); ++it) {
        SrsParsedAudioPacket *p = *it;
        srs_freep(p);
    }
}

void MockAudioTranscoder::aac_codec_header(uint8_t **data, int *len)
{
    int size = aac_header_.size();
    if (size <= 0) {
        return;
    }

    uint8_t *copy = new uint8_t[size];
    memcpy(copy, aac_header_.data(), size);
    *data = copy;
    *len = size;
}
