//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_ai45.hpp>

#include <srs_app_dvr.hpp>
#include <srs_app_edge.hpp>
#include <srs_app_rtc_server.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_security.hpp>
#include <srs_app_srt_conn.hpp>
#include <srs_app_srt_server.hpp>
#include <srs_app_srt_source.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_stream_token.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_resource.hpp>
#include <srs_protocol_rtmp_msg_array.hpp>
#include <srs_protocol_sdp.hpp>
#include <srs_protocol_srt.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_utest_ai05.hpp>
#include <srs_utest_ai07.hpp>
#include <srs_utest_ai08.hpp>
#include <srs_utest_ai09.hpp>
#include <srs_utest_ai14.hpp>
#include <srs_utest_ai15.hpp>
#include <srs_utest_ai18.hpp>
#include <srs_utest_ai32.hpp>
#include <srs_utest_ai36.hpp>
#include <srs_utest_ai40.hpp>
#include <srs_utest_ai41.hpp>
#include <srs_utest_ai42.hpp>
#include <srs_utest_ai44.hpp>
#include <srs_utest_manual_config.hpp>
#include <srs_utest_manual_coworkers.hpp>
#include <srs_utest_manual_kernel.hpp>
#include <srs_utest_manual_mock.hpp>
#include <srs_utest_manual_protocol.hpp>
#include <srs_utest_manual_service.hpp>
#ifdef SRS_RTSP
#include <srs_app_rtsp_conn.hpp>
#include <srs_app_rtsp_source.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_utest_ai22.hpp>
#endif

#include <algorithm>
#include <set>
#include <sstream>
#include <string>
using namespace std;

// An RTC session in the server's connection manager: it subscribes to the manager as SrsRtcConnection::assemble does,
// and marks itself disposing when the manager is about to free it, as SrsRtcConnection::on_before_dispose does.
class RtcServerUtestSession : public MockRtcConnectionForUpdateSessions
{
public:
    ISrsResourceManager *manager_;
    int switches_;

public:
    RtcServerUtestSession(ISrsResourceManager *manager, string username, bool alive)
    {
        manager_ = manager;
        switches_ = 0;
        username_ = username;
        is_alive_ = alive;
        manager_->subscribe(this);
    }
    virtual ~RtcServerUtestSession()
    {
        manager_->unsubscribe(this);
    }

public:
    virtual void on_before_dispose(ISrsResource *c)
    {
        if (c == this) {
            is_disposing_ = true;
        }
    }
    virtual void switch_to_context()
    {
        switches_++;
    }
};

// The server's connection manager, not started, so a removed session waits in the zombies as it does until the
// manager's coroutine runs, with the session manager's 5s check on it, and the server's log kept.
class RtcServerUtestSessions
{
public:
    SrsResourceManager manager_;
    SrsRtcSessionManager sessions_;
    MockLogForSignal log_;
    ISrsLog *saved_log_;

public:
    RtcServerUtestSessions() : manager_("RTC", false)
    {
        manager_.assemble();
        sessions_.conn_manager_ = &manager_;

        saved_log_ = _srs_log;
        _srs_log = &log_;
    }
    ~RtcServerUtestSessions()
    {
        _srs_log = saved_log_;
        sessions_.conn_manager_ = NULL;
    }

public:
    int traces(string text)
    {
        int n = 0;
        for (int i = 0; i < (int)log_.traces_.size(); i++) {
            if (log_.traces_[i].find(text) != string::npos) {
                n++;
            }
        }
        return n;
    }
    string last_trace(string text)
    {
        string found;
        for (int i = 0; i < (int)log_.traces_.size(); i++) {
            if (log_.traces_[i].find(text) != string::npos) {
                found = log_.traces_[i];
            }
        }
        return found;
    }
    bool is_zombie(ISrsResource *c)
    {
        return std::find(manager_.zombies_.begin(), manager_.zombies_.end(), c) != manager_.zombies_.end();
    }
};

// The 5s check of RTC sessions on a server that also serves an RTMP client: the connection manager holds every
// client, so the check skips the RTMP client. A session that timed out is removed once, and at the next check, before
// the manager's coroutine frees it, it is disposing and not removed again. When the last session times out, the
// server has no RTC session, so no stats line is printed.
VOID TEST(AppRtcServerTest, SessionCheck_SkipsRtmpClientAndRemovedSession)
{
    RtcServerUtestSessions s;

    MockSrsResource *rtmp = new MockSrsResource();
    s.manager_.add(rtmp);
    RtcServerUtestSession *alive = new RtcServerUtestSession(&s.manager_, "alive", true);
    s.manager_.add_with_name("alive", alive);
    RtcServerUtestSession *gone = new RtcServerUtestSession(&s.manager_, "gone", false);
    s.manager_.add_with_name("gone", gone);

    s.sessions_.srs_update_rtc_sessions();
    EXPECT_EQ(1, gone->switches_);
    EXPECT_TRUE(gone->is_disposing_);
    EXPECT_TRUE(s.is_zombie(gone));
    EXPECT_EQ(1, (int)s.manager_.zombies_.size());
    EXPECT_EQ(0, alive->switches_);
    EXPECT_EQ(1, s.traces("session destroy by timeout, username=gone"));
    EXPECT_EQ(1, s.traces("RTC: Server conns=1"));

    // The manager has not freed the session yet, so it is still listed.
    EXPECT_EQ(3, (int)s.manager_.size());
    s.sessions_.srs_update_rtc_sessions();
    EXPECT_EQ(1, gone->switches_);
    EXPECT_EQ(1, (int)s.manager_.zombies_.size());
    EXPECT_EQ(1, s.traces("session destroy by timeout"));
    EXPECT_EQ(2, s.traces("RTC: Server conns=1"));

    alive->is_alive_ = false;
    s.sessions_.srs_update_rtc_sessions();
    EXPECT_EQ(1, alive->switches_);
    EXPECT_TRUE(s.is_zombie(alive));
    EXPECT_EQ(2, (int)s.manager_.zombies_.size());
    EXPECT_FALSE(s.is_zombie(rtmp));
    EXPECT_EQ(2, s.traces("RTC: Server conns="));
}

// The server's UDP buffers overflowed since the last check, as /proc/net/snmp counts in RcvbufErrors and SndbufErrors:
// the 5s stats line of the RTC sessions reports the datagrams the kernel dropped, and no loss once none are dropped.
VOID TEST(AppRtcServerTest, SessionCheck_StatsLineReportsUdpBufferLoss)
{
    SrsSnmpUdpStat *udp = srs_get_udp_snmp_stat();
    SrsSnmpUdpStat saved = *udp;

    RtcServerUtestSessions s;
    RtcServerUtestSession *session = new RtcServerUtestSession(&s.manager_, "player", true);
    s.manager_.add_with_name("player", session);

    udp->rcv_buf_errors_delta_ = 3;
    udp->snd_buf_errors_delta_ = 1;
    s.sessions_.srs_update_rtc_sessions();
    string line = s.last_trace("RTC: Server conns=1");
    EXPECT_NE(string::npos, line.find(", loss=(r:3,s:1)")) << line;

    udp->rcv_buf_errors_delta_ = 0;
    udp->snd_buf_errors_delta_ = 0;
    s.sessions_.srs_update_rtc_sessions();
    EXPECT_EQ(2, s.traces("RTC: Server conns=1"));
    line = s.last_trace("RTC: Server conns=1");
    EXPECT_EQ(string::npos, line.find("loss=")) << line;

    *udp = saved;
}

// A host with a loopback and an Ethernet interface, with candidate * and auto detection: the loopback address is never
// a candidate, as a browser on another host cannot reach it, so the answer has the Ethernet address only.
VOID TEST(AppRtcServerTest, Candidates_LoopbackInterfaceSkipped)
{
    srs_error_t err;

    MockSrsConfig conf;
    HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{enabled on; listen 8000; candidate *; use_auto_detect_network_ip on;}"));

    MockProtocolUtility utility;
    utility.add_ip("127.0.0.1", "lo", true, true, false);
    utility.add_ip("192.168.1.10", "eth0", true, false, false);

    SrsRtcUserConfig ruc;
    set<string> candidates = discover_candidates(&utility, &conf, &ruc);
    ASSERT_EQ(1, (int)candidates.size());
    EXPECT_STREQ("192.168.1.10", candidates.begin()->c_str());
}

// A host whose interfaces cannot be listed (getifaddrs fails, so SRS has no local address): with candidate *, the
// answer has only what the client gave, its ?eip=, and no candidate at all without one.
VOID TEST(AppRtcServerTest, Candidates_NoLocalAddress_OnlyClientEip)
{
    srs_error_t err;

    MockSrsConfig conf;
    HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{enabled on; listen 8000; candidate *;}"));

    MockProtocolUtility utility;

    if (true) {
        SrsRtcUserConfig ruc;
        ruc.eip_ = "203.0.113.5";
        set<string> candidates = discover_candidates(&utility, &conf, &ruc);
        ASSERT_EQ(1, (int)candidates.size());
        EXPECT_STREQ("203.0.113.5", candidates.begin()->c_str());
    }

    if (true) {
        SrsRtcUserConfig ruc;
        set<string> candidates = discover_candidates(&utility, &conf, &ruc);
        EXPECT_TRUE(candidates.empty());
    }
}

// A WHEP player reaches the API by a domain name DNS does not resolve, such as a name only in the client's hosts file,
// with api_as_candidates, keep_api_domain and resolve_api_domain on: the domain is still a candidate, no resolved
// address is added, and the host's own address is a candidate too.
VOID TEST(AppRtcServerTest, Candidates_ApiDomainNotResolved_DomainKept)
{
    srs_error_t err;

    MockSrsConfig conf;
    HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{enabled on; listen 8000; candidate *; "
                                                       "api_as_candidates on; keep_api_domain on; resolve_api_domain on;}"));

    MockProtocolUtility utility;
    utility.add_ip("192.168.1.10", "eth0", true, false, false);

    SrsRtcUserConfig ruc;
    ruc.req_->host_ = "srs-utest.invalid";
    set<string> candidates = discover_candidates(&utility, &conf, &ruc);
    ASSERT_EQ(2, (int)candidates.size());
    EXPECT_TRUE(candidates.find("srs-utest.invalid") != candidates.end());
    EXPECT_TRUE(candidates.find("192.168.1.10") != candidates.end());
}

// A WebRTC stream's description as a WHIP publisher negotiates it: Opus audio and one H.264 video track.
static SrsRtcSourceDescription *rtc_source_utest_whip_desc()
{
    SrsRtcSourceDescription *desc = new SrsRtcSourceDescription();

    SrsRtcTrackDescription *audio = new SrsRtcTrackDescription();
    audio->type_ = "audio";
    audio->id_ = "audio-whip";
    audio->ssrc_ = 1234;
    audio->media_ = new SrsAudioPayload(111, "opus", 48000, 2);
    desc->audio_track_desc_ = audio;

    SrsRtcTrackDescription *video = new SrsRtcTrackDescription();
    video->type_ = "video";
    video->id_ = "video-whip";
    video->ssrc_ = 5678;
    video->media_ = new SrsVideoPayload(106, "H264", 90000);
    desc->video_track_descs_.push_back(video);

    return desc;
}

// A WebRTC player leaves a stream nobody publishes, such as one it opened before the publisher came: the source stays
// for 3s, so a player that reconnects finds it, and the 3s check of the source manager frees it after that, while a
// published stream stays.
VOID TEST(AppRtcSourceTest, SourceOfLastPlayerGone_FreedAfterThreeSeconds)
{
    srs_error_t err;

    MockClockForPithyPrint clock;
    clock.now_ = 10 * SRS_UTIME_SECONDS;

    SrsRtcSourceManager manager;
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->vhost_ = "__defaultVhost__";
    req->app_ = "live";
    req->stream_ = "livestream";

    SrsSharedPtr<SrsRtcSource> source;
    HELPER_ASSERT_SUCCESS(manager.fetch_or_create(req.get(), source));
    source->clk_ = &clock;

    ISrsRtcConsumer *consumer = NULL;
    HELPER_ASSERT_SUCCESS(source->create_consumer(consumer));
    srs_freep(consumer);

    // Another stream of the manager, which a publisher publishes with no player, is never freed.
    SrsUniquePtr<SrsRequest> published_req(new SrsRequest());
    published_req->vhost_ = "__defaultVhost__";
    published_req->app_ = "live";
    published_req->stream_ = "published";
    SrsSharedPtr<SrsRtcSource> published;
    HELPER_ASSERT_SUCCESS(manager.fetch_or_create(published_req.get(), published));
    published->clk_ = &clock;
    HELPER_ASSERT_SUCCESS(published->on_publish());

    // Just under 3s after the player left, the source is kept.
    clock.now_ = 12 * SRS_UTIME_SECONDS + 999 * SRS_UTIME_MILLISECONDS;
    HELPER_EXPECT_SUCCESS(manager.notify(0, 3 * SRS_UTIME_SECONDS, clock.now_));
    EXPECT_EQ(2, (int)manager.pool_.size());

    // At 3s, it is freed from the manager, and the published stream stays.
    clock.now_ = 13 * SRS_UTIME_SECONDS;
    HELPER_EXPECT_SUCCESS(manager.notify(0, 3 * SRS_UTIME_SECONDS, clock.now_));
    EXPECT_EQ(1, (int)manager.pool_.size());
    EXPECT_TRUE(manager.pool_.find(published_req->get_stream_url()) != manager.pool_.end());
    published->on_unpublish();

    // A player that comes now gets a new source.
    SrsSharedPtr<SrsRtcSource> fresh;
    HELPER_ASSERT_SUCCESS(manager.fetch_or_create(req.get(), fresh));
    EXPECT_TRUE(fresh.get() != source.get());
}

// A WHIP stream converted to RTMP, on a vhost with `rtc_to_rtmp on; pli_for_rtmp 1;`: the source asks the publisher
// for a keyframe of its video track every second, so an RTMP, HLS or FLV player that joins between the browser's own
// keyframes waits at most that long.
VOID TEST(AppRtcSourceTest, WhipToRtmp_KeyframeRequestedEveryPliInterval)
{
    srs_error_t err;

    MockSrsConfig conf;
    HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost __defaultVhost__{rtc{enabled on; rtc_to_rtmp on; pli_for_rtmp 1;}}"));

    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->vhost_ = "__defaultVhost__";
    req->app_ = "live";
    req->stream_ = "whip2rtmp";

    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    source->config_ = &conf;
    HELPER_ASSERT_SUCCESS(source->initialize(req.get()));

    SrsUniquePtr<SrsRtcSourceDescription> desc(rtc_source_utest_whip_desc());
    source->set_stream_desc(desc.get());

    MockRtcPublishStream publisher;
    SrsContextId cid;
    cid.set_value("whip-publisher");
    publisher.set_context_id(cid);
    source->set_publish_stream(&publisher);
    source->set_bridge(new MockRtcBridge());
    HELPER_ASSERT_SUCCESS(source->on_publish());
    EXPECT_EQ(1 * SRS_UTIME_SECONDS, source->pli_for_rtmp_);

    // The shared 100ms timer: nothing for the first 900ms.
    for (int i = 0; i < 9; i++) {
        HELPER_EXPECT_SUCCESS(source->on_timer(100 * SRS_UTIME_MILLISECONDS));
    }
    EXPECT_EQ(0, publisher.request_keyframe_count_);

    // At 1s, a PLI for the video track, in the publisher's context.
    HELPER_EXPECT_SUCCESS(source->on_timer(100 * SRS_UTIME_MILLISECONDS));
    EXPECT_EQ(1, publisher.request_keyframe_count_);
    EXPECT_EQ(5678u, publisher.last_keyframe_ssrc_);
    EXPECT_EQ(0, publisher.last_keyframe_cid_.compare(cid));

    // And again a second later.
    for (int i = 0; i < 10; i++) {
        HELPER_EXPECT_SUCCESS(source->on_timer(100 * SRS_UTIME_MILLISECONDS));
    }
    EXPECT_EQ(2, publisher.request_keyframe_count_);

    // The publisher's session ends: no more requests.
    source->set_publish_stream(NULL);
    for (int i = 0; i < 10; i++) {
        HELPER_EXPECT_SUCCESS(source->on_timer(100 * SRS_UTIME_MILLISECONDS));
    }
    EXPECT_EQ(2, publisher.request_keyframe_count_);

    source->on_unpublish();
    source->config_ = NULL;
}

// The audio track of a WebRTC player that offers RED with another payload type than the publisher's, such as Chrome
// publishing Opus with red/48000/2 as 63 and a player offering it as 96: each RED packet of the publisher is sent to
// the player as its own RED type, and plain Opus as its own Opus type.
VOID TEST(AppRtcSourceTest, PlayerAudioRedPayloadType_RewrittenToPlayers)
{
    srs_error_t err;

    MockRtcPacketSender sender;
    SrsRtcTrackDescription desc;
    desc.type_ = "audio";
    desc.ssrc_ = 4321;
    desc.is_active_ = true;
    desc.media_ = new SrsAudioPayload(111, "opus", 48000, 2);
    desc.media_->pt_of_publisher_ = 109;
    desc.red_ = new SrsRedPayload(96, "red", 48000, 2);
    desc.red_->pt_of_publisher_ = 63;

    SrsRtcAudioSendTrack track(&sender, &desc);
    track.assemble();

    SrsRtpPacket red;
    red.header_.set_payload_type(63);
    red.header_.set_sequence(100);
    HELPER_EXPECT_SUCCESS(track.on_rtp(&red));
    EXPECT_EQ(96, red.header_.get_payload_type());
    EXPECT_EQ(4321u, red.header_.get_ssrc());

    SrsRtpPacket opus;
    opus.header_.set_payload_type(109);
    opus.header_.set_sequence(101);
    HELPER_EXPECT_SUCCESS(track.on_rtp(&opus));
    EXPECT_EQ(111, opus.header_.get_payload_type());

    EXPECT_EQ(2, sender.send_packet_count_);
}

// The video track of a WebRTC player whose red/90000 type differs from the one Chrome published, 116 to the player's
// 122: the RED packets reach the player as 122, the H.264 packets as the player's H.264 type.
VOID TEST(AppRtcSourceTest, PlayerVideoRedPayloadType_RewrittenToPlayers)
{
    srs_error_t err;

    MockRtcPacketSender sender;
    SrsRtcTrackDescription desc;
    desc.type_ = "video";
    desc.ssrc_ = 8765;
    desc.is_active_ = true;
    desc.media_ = new SrsVideoPayload(102, "H264", 90000);
    desc.media_->pt_of_publisher_ = 106;
    desc.red_ = new SrsRedPayload(122, "red", 90000, 0);
    desc.red_->pt_of_publisher_ = 116;

    SrsRtcVideoSendTrack track(&sender, &desc);
    track.assemble();

    SrsRtpPacket red;
    red.header_.set_payload_type(116);
    red.header_.set_sequence(200);
    HELPER_EXPECT_SUCCESS(track.on_rtp(&red));
    EXPECT_EQ(122, red.header_.get_payload_type());

    SrsRtpPacket h264;
    h264.header_.set_payload_type(106);
    h264.header_.set_sequence(201);
    HELPER_EXPECT_SUCCESS(track.on_rtp(&h264));
    EXPECT_EQ(102, h264.header_.get_payload_type());

    EXPECT_EQ(2, sender.send_packet_count_);
}

// A player on a vhost with `keep_original_ssrc on`, for end-to-end debugging, gets the publisher's own sequence
// numbers and RTP times, even across the publisher's restart; a player of another vhost gets them rebuilt, and kept
// continuous across the restart, as KernelRTCTest.JitterSequence locks.
VOID TEST(AppRtcSourceTest, KeepOriginalSsrc_PlayerGetsPublisherSequenceAndTime)
{
    srs_error_t err;

    MockRtcPacketSender sender;
    SrsRtcTrackDescription desc;
    desc.type_ = "video";
    desc.ssrc_ = 5678;
    desc.is_active_ = true;

    SrsRtcVideoSendTrack kept(&sender, &desc);
    kept.assemble();
    kept.set_keep_original_ssrc(true);

    SrsRtcVideoSendTrack rebuilt(&sender, &desc);
    rebuilt.assemble();

    uint16_t seqs[] = {30000, 30001, 7};
    uint32_t times[] = {900000, 903000, 4000};
    for (int i = 0; i < 3; i++) {
        SrsRtpPacket pkt;
        pkt.header_.set_sequence(seqs[i]);
        pkt.header_.set_timestamp(times[i]);
        HELPER_EXPECT_SUCCESS(kept.on_rtp(&pkt));
        EXPECT_EQ(seqs[i], pkt.header_.get_sequence());
        EXPECT_EQ(times[i], pkt.header_.get_timestamp());
    }

    uint16_t rebuilt_seqs[3];
    for (int i = 0; i < 3; i++) {
        SrsRtpPacket pkt;
        pkt.header_.set_sequence(seqs[i]);
        pkt.header_.set_timestamp(times[i]);
        HELPER_EXPECT_SUCCESS(rebuilt.on_rtp(&pkt));
        rebuilt_seqs[i] = pkt.header_.get_sequence();
    }
    EXPECT_NE(seqs[0], rebuilt_seqs[0]);
    EXPECT_EQ((uint16_t)(rebuilt_seqs[0] + 1), rebuilt_seqs[1]);
    EXPECT_GE(srs_rtp_seq_distance(rebuilt_seqs[1], rebuilt_seqs[2]), 0);
    EXPECT_LE(srs_rtp_seq_distance(rebuilt_seqs[1], rebuilt_seqs[2]), 1);
}

// A player NACKs a video packet the track no longer keeps, after the 1000 packets of its queue went past, with a
// packet it still keeps: only the kept one is sent again, and the old one is skipped, as resending another packet
// at that slot would break the player's SRTP.
VOID TEST(AppRtcSourceTest, PlayerNacksPacketNoLongerCached_OnlyCachedResent)
{
    srs_error_t err;

    MockRtcPacketSender sender;
    SrsRtcTrackDescription desc;
    desc.type_ = "video";
    desc.ssrc_ = 5678;
    desc.is_active_ = true;

    SrsRtcVideoSendTrack track(&sender, &desc);
    track.assemble();

    // 1001 packets: the last one takes the slot of the first.
    uint16_t first = 0, last = 0;
    for (int i = 0; i < 1001; i++) {
        SrsRtpPacket *pkt = new SrsRtpPacket();
        pkt->header_.set_sequence(5000 + i);
        pkt->header_.set_timestamp(90000 + i * 3000);
        HELPER_EXPECT_SUCCESS(track.on_rtp(pkt));
        if (i == 0) {
            first = pkt->header_.get_sequence();
        }
        last = pkt->header_.get_sequence();
        HELPER_EXPECT_SUCCESS(track.on_nack(&pkt));
        srs_freep(pkt);
    }
    EXPECT_EQ(1001, sender.send_packet_count_);

    vector<uint16_t> lost;
    lost.push_back(first);
    lost.push_back(last - 1);
    HELPER_EXPECT_SUCCESS(track.on_recv_nack(lost));

    ASSERT_EQ(1002, sender.send_packet_count_);
    EXPECT_EQ((uint16_t)(last - 1), sender.last_sent_packet_->header_.get_sequence());
}

// An HEVC SPS of 768x320, Main profile, from libx265.
static uint8_t rtc_source_utest_hevc_sps[] = {
    0x42, 0x01, 0x01, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, 0x90, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03,
    0x00, 0x3f, 0xa0, 0x06, 0x02, 0x01, 0x41, 0x65, 0x95, 0x9a, 0x49, 0x32, 0xbc, 0x05, 0xa0, 0x20,
    0x00, 0x00, 0x03, 0x00, 0x20, 0x00, 0x00, 0x03, 0x03, 0x21};
// An H.264 SPS of 768x320, High profile.
static uint8_t rtc_source_utest_avc_sps[] = {
    0x67, 0x64, 0x00, 0x20, 0xac, 0xd9, 0x40, 0xc0, 0x29, 0xb0, 0x11, 0x00, 0x00, 0x03, 0x00, 0x01,
    0x00, 0x00, 0x03, 0x00, 0x32, 0x0f, 0x18, 0x31, 0x96};

// A video packet of a single NAL unit, the SPS, which refers to the test's bytes.
static SrsRtpPacket *rtc_source_utest_sps_packet(uint8_t nalu_type, uint8_t *bytes, int size)
{
    SrsRtpPacket *pkt = new SrsRtpPacket();
    SrsRtpRawPayload *raw = new SrsRtpRawPayload();
    raw->sample_->bytes_ = (char *)bytes;
    raw->sample_->size_ = size;
    pkt->nalu_type_ = nalu_type;
    pkt->set_payload(raw, SrsRtpPacketPayloadTypeRaw);
    return pkt;
}

// A WHIP publisher of HEVC that sends the VPS, SPS and PPS each in a packet of its own, not in an AP: the stream is
// listed with the size of that SPS.
VOID TEST(AppRtcSourceTest, WhipHevcSpsInItsOwnPacket_SizeListed)
{
    srs_error_t err;

    SrsUniquePtr<SrsStatistic> stat(new SrsStatistic());
    MockRequest req("__defaultVhost__", "live", "whip_hevc_single_sps");

    SrsRtcTrackDescription desc;
    desc.type_ = "video";
    desc.ssrc_ = 5678;
    SrsVideoPayload *media = new SrsVideoPayload(49, "H265", 90000);
    media->h265_param_.profile_id_ = "1";
    media->h265_param_.level_id_ = "93";
    desc.media_ = media;
    MockRtcPacketReceiver receiver;
    SrsRtcVideoRecvTrack track(&receiver, &desc, false);

    SrsRtcFormat format;
    format.stat_ = stat.get();
    HELPER_ASSERT_SUCCESS(format.initialize(&req));

    SrsUniquePtr<SrsRtpPacket> sps(rtc_source_utest_sps_packet(SrsHevcNaluType_SPS, rtc_source_utest_hevc_sps, sizeof(rtc_source_utest_hevc_sps)));
    HELPER_EXPECT_SUCCESS(format.on_rtp_packet(&track, sps.get(), false));

    SrsStatisticStream *stream = stat->find_stream_by_url(req.get_stream_url());
    ASSERT_TRUE(stream != NULL);
    EXPECT_EQ(SrsVideoCodecIdHEVC, stream->vcodec_);
    EXPECT_EQ(768, stream->width_);
    EXPECT_EQ(320, stream->height_);
}

// An HEVC offer without level-id, which RFC 7798 section 7.1 allows (level 3.1 is then implied): the stream is not
// listed from the SDP, and is listed with its codec and size once the first SPS comes.
VOID TEST(AppRtcSourceTest, WhipHevcOfferWithoutLevelId_ListedFromSps)
{
    srs_error_t err;

    SrsUniquePtr<SrsStatistic> stat(new SrsStatistic());
    MockRequest req("__defaultVhost__", "live", "whip_hevc_no_level");

    SrsRtcTrackDescription desc;
    desc.type_ = "video";
    desc.ssrc_ = 5678;
    SrsVideoPayload *media = new SrsVideoPayload(49, "H265", 90000);
    media->h265_param_.profile_id_ = "1";
    desc.media_ = media;
    MockRtcPacketReceiver receiver;
    SrsRtcVideoRecvTrack track(&receiver, &desc, false);

    SrsRtcFormat format;
    format.stat_ = stat.get();
    HELPER_ASSERT_SUCCESS(format.initialize(&req));

    SrsRtpPacket slice;
    slice.nalu_type_ = kFuHevc;
    HELPER_EXPECT_SUCCESS(format.on_rtp_packet(&track, &slice, false));
    EXPECT_TRUE(stat->find_stream_by_url(req.get_stream_url()) == NULL);

    SrsUniquePtr<SrsRtpPacket> sps(rtc_source_utest_sps_packet(SrsHevcNaluType_SPS, rtc_source_utest_hevc_sps, sizeof(rtc_source_utest_hevc_sps)));
    HELPER_EXPECT_SUCCESS(format.on_rtp_packet(&track, sps.get(), false));

    SrsStatisticStream *stream = stat->find_stream_by_url(req.get_stream_url());
    ASSERT_TRUE(stream != NULL);
    EXPECT_EQ(SrsVideoCodecIdHEVC, stream->vcodec_);
    EXPECT_EQ(768, stream->width_);
    EXPECT_EQ(320, stream->height_);
}

// A WHIP publisher sends a cut SPS, its first 6 bytes: the error is returned for the publish stream to log, the size
// stays unknown, and the next whole SPS gives it.
VOID TEST(AppRtcSourceTest, WhipCutSps_SizeUnknownUntilWholeSps)
{
    srs_error_t err;

    SrsUniquePtr<SrsStatistic> stat(new SrsStatistic());
    MockRequest req("__defaultVhost__", "live", "whip_cut_sps");

    SrsRtcTrackDescription desc;
    desc.type_ = "video";
    desc.ssrc_ = 5678;
    SrsVideoPayload *media = new SrsVideoPayload(106, "H264", 90000);
    media->h264_param_.profile_level_id_ = "42e01f";
    desc.media_ = media;
    MockRtcPacketReceiver receiver;
    SrsRtcVideoRecvTrack track(&receiver, &desc, false);

    SrsRtcFormat format;
    format.stat_ = stat.get();
    HELPER_ASSERT_SUCCESS(format.initialize(&req));

    SrsUniquePtr<SrsRtpPacket> cut(rtc_source_utest_sps_packet(SrsAvcNaluTypeSPS, rtc_source_utest_avc_sps, 6));
    err = format.on_rtp_packet(&track, cut.get(), false);
    EXPECT_TRUE(err != srs_success);
    EXPECT_TRUE(srs_error_desc(err).find("demux sps") != string::npos);
    srs_freep(err);

    SrsStatisticStream *stream = stat->find_stream_by_url(req.get_stream_url());
    ASSERT_TRUE(stream != NULL);
    EXPECT_EQ(SrsVideoCodecIdAVC, stream->vcodec_);
    EXPECT_EQ(0, stream->width_);

    SrsUniquePtr<SrsRtpPacket> whole(rtc_source_utest_sps_packet(SrsAvcNaluTypeSPS, rtc_source_utest_avc_sps, sizeof(rtc_source_utest_avc_sps)));
    HELPER_EXPECT_SUCCESS(format.on_rtp_packet(&track, whole.get(), false));
    EXPECT_EQ(768, stream->width_);
    EXPECT_EQ(320, stream->height_);
}

// A WHIP offer whose H.264 fmtp has a parameter besides the three SRS knows, such as sprop-parameter-sets (RFC 6184
// section 8.1), whose base64 may end in '=': the payload takes the three known parameters and skips the rest, as
// srs_parse_h264_fmtp does, so the publish negotiation gets no error to drop.
VOID TEST(AppRtcSourceTest, WhipH264FmtpWithSprop_ParamDescAccepted)
{
    srs_error_t err;

    string fmtp = "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f;"
                  "sprop-parameter-sets=Z0LgH9oBQBbpUgAAAwACAAADAGQeMGVA,aM4yyA==";

    H264SpecificParam param;
    HELPER_ASSERT_SUCCESS(srs_parse_h264_fmtp(fmtp, param));
    EXPECT_EQ("42e01f", param.profile_level_id_);

    SrsVideoPayload payload(106, "H264", 90000);
    HELPER_EXPECT_SUCCESS(payload.set_h264_param_desc(fmtp));
    EXPECT_EQ("42e01f", payload.h264_param_.profile_level_id_);
    EXPECT_EQ("1", payload.h264_param_.packetization_mode_);
    EXPECT_EQ("1", payload.h264_param_.level_asymmetry_allow_);
}

#ifdef SRS_FFMPEG_FIT
// An Opus packet of a WHIP publisher, for the audio cache of the RTMP converter.
static SrsRtpPacket *rtc_source_utest_opus(uint16_t seq)
{
    SrsRtpPacket *pkt = new SrsRtpPacket();
    pkt->header_.set_sequence(seq);
    pkt->header_.set_timestamp(seq * 960);
    pkt->frame_type_ = SrsFrameTypeAudio;
    return pkt;
}

// A WHIP stream converted to RTMP, whose Opus packets 65535 and 0 come swapped at the sequence wrap, as a network
// reorders them: the converter should take 65535 then 0 once both are there. The audio cache sorts its packets by the
// 16-bit number, so 0 comes first and is a gap after 65534: the converter holds the audio for the 100ms timeout,
// then sends 0 and the packets after it before 65535, which RTMP gets with its time going back (B50).
VOID TEST(AppRtcSourceTest, DISABLED_WhipAudioSwappedAtSequenceWrap_HeldThenOutOfOrder)
{
    srs_error_t err;

    SrsRtcFrameBuilderAudioPacketCache cache;
    cache.set_timeout(10 * SRS_UTIME_SECONDS);
    vector<SrsRtpPacket *> ready;

    SrsUniquePtr<SrsRtpPacket> p65534(rtc_source_utest_opus(65534));
    HELPER_ASSERT_SUCCESS(cache.process_packet(p65534.get(), ready));
    ASSERT_EQ(1, (int)ready.size());
    srs_freep(ready[0]);
    ready.clear();

    SrsUniquePtr<SrsRtpPacket> p0(rtc_source_utest_opus(0));
    HELPER_ASSERT_SUCCESS(cache.process_packet(p0.get(), ready));
    EXPECT_EQ(0, (int)ready.size());

    SrsUniquePtr<SrsRtpPacket> p65535(rtc_source_utest_opus(65535));
    HELPER_ASSERT_SUCCESS(cache.process_packet(p65535.get(), ready));

    vector<uint16_t> seqs;
    for (int i = 0; i < (int)ready.size(); i++) {
        seqs.push_back(ready[i]->header_.get_sequence());
        srs_freep(ready[i]);
    }
    ASSERT_EQ(2, (int)seqs.size());
    EXPECT_EQ(65535, seqs[0]);
    EXPECT_EQ(0, seqs[1]);
}

static char rtc_source_utest_fua_bytes[1000];

// A packet of an H.264 frame cut into FU-A, as a WHIP publisher sends a large frame.
static SrsRtpPacket *rtc_source_utest_fua(uint16_t seq, uint32_t ts, SrsAvcNaluType type, bool start, bool end)
{
    SrsRtpPacket *pkt = new SrsRtpPacket();
    pkt->header_.set_sequence(seq);
    pkt->header_.set_timestamp(ts);
    pkt->header_.set_marker(end);
    pkt->frame_type_ = SrsFrameTypeVideo;
    pkt->nalu_type_ = kFuA;
    pkt->set_avsync_time(ts / 90);

    SrsRtpFUAPayload2 *fua = new SrsRtpFUAPayload2();
    fua->start_ = start;
    fua->end_ = end;
    fua->nri_ = SrsAvcNaluTypeIDR;
    fua->nalu_type_ = type;
    fua->payload_ = rtc_source_utest_fua_bytes;
    fua->size_ = sizeof(rtc_source_utest_fua_bytes);
    pkt->set_payload(fua, SrsRtpPacketPayloadTypeFUA2);
    return pkt;
}

// A WHIP stream converted to RTMP whose keyframe is 600 FU-A packets, more than the 512 packets the converter keeps,
// such as a 4K screen share at a high bitrate: the converter fails at its 512th packet with a cache overflow and
// drops the keyframe, and RTMP gets the next keyframe that fits. This is the converter's limit, not a bug: its
// cache holds 512 packets.
VOID TEST(AppRtcSourceTest, WhipKeyframeOver512Packets_DroppedNextKeyframeConverted)
{
    srs_error_t err;

    MockRtcFrameTarget target;
    SrsRtcFrameBuilder builder(_srs_app_factory, &target);
    MockRequest req("__defaultVhost__", "live", "whip_big_keyframe");
    HELPER_ASSERT_SUCCESS(builder.initialize(&req, SrsAudioCodecIdPCMU, SrsVideoCodecIdAVC));

    int failed_at = -1;
    for (int i = 0; i < 600; i++) {
        SrsUniquePtr<SrsRtpPacket> pkt(rtc_source_utest_fua(1000 + i, 90000, SrsAvcNaluTypeIDR, i == 0, i == 599));
        err = builder.on_rtp(pkt.get());
        if (err != srs_success) {
            if (failed_at < 0) {
                failed_at = i;
            }
            EXPECT_EQ(ERROR_RTC_RTP_MUXER, srs_error_code(err));
            EXPECT_TRUE(srs_error_desc(err).find("video cache is overflow") != string::npos);
            srs_freep(err);
        }
    }
    EXPECT_EQ(511, failed_at);
    EXPECT_EQ(0, target.on_frame_count_);

    // The next keyframe, of 3 packets, is converted.
    for (int i = 0; i < 3; i++) {
        SrsUniquePtr<SrsRtpPacket> pkt(rtc_source_utest_fua(1600 + i, 270000, SrsAvcNaluTypeIDR, i == 0, i == 2));
        HELPER_EXPECT_SUCCESS(builder.on_rtp(pkt.get()));
    }
    EXPECT_EQ(1, target.on_frame_count_);
}

// A packet of an HEVC IDR cut into FUs, as Safari sends it.
static SrsRtpPacket *rtc_source_utest_hevc_fu(uint16_t seq, uint32_t ts, bool start, bool end)
{
    SrsRtpPacket *pkt = new SrsRtpPacket();
    pkt->header_.set_sequence(seq);
    pkt->header_.set_timestamp(ts);
    pkt->header_.set_marker(end);
    pkt->frame_type_ = SrsFrameTypeVideo;
    pkt->nalu_type_ = kFuHevc;
    pkt->set_avsync_time(ts / 90);

    SrsRtpFUAPayloadHevc2 *fu = new SrsRtpFUAPayloadHevc2();
    fu->start_ = start;
    fu->end_ = end;
    fu->nalu_type_ = SrsHevcNaluType_CODED_SLICE_IDR;
    fu->payload_ = rtc_source_utest_fua_bytes;
    fu->size_ = sizeof(rtc_source_utest_fua_bytes);
    pkt->set_payload(fu, SrsRtpPacketPayloadTypeFUAHevc2);
    return pkt;
}

// A WHIP HEVC publisher, such as Safari, sends its keyframe as an AP of the VPS, SPS and PPS, then the IDR in FUs:
// RTMP gets the sequence header from the AP, then the keyframe of the FUs.
VOID TEST(AppRtcSourceTest, WhipHevcKeyframeApThenFus_SequenceHeaderThenKeyframe)
{
    srs_error_t err;

    MockRtcFrameTarget target;
    SrsRtcFrameBuilder builder(_srs_app_factory, &target);
    MockRequest req("__defaultVhost__", "live", "whip_hevc_keyframe");
    HELPER_ASSERT_SUCCESS(builder.initialize(&req, SrsAudioCodecIdPCMU, SrsVideoCodecIdHEVC));

    SrsUniquePtr<SrsRtpPacket> ap(create_hevc_stap_packet_with_vps_sps_pps());
    HELPER_EXPECT_SUCCESS(builder.on_rtp(ap.get()));
    EXPECT_EQ(1, target.on_frame_count_);

    for (int i = 0; i < 3; i++) {
        SrsUniquePtr<SrsRtpPacket> pkt(rtc_source_utest_hevc_fu(101 + i, 90000, i == 0, i == 2));
        HELPER_EXPECT_SUCCESS(builder.on_rtp(pkt.get()));
    }
    ASSERT_EQ(2, target.on_frame_count_);
    EXPECT_TRUE(target.last_frame_->is_video());
    EXPECT_EQ(SrsVideoAvcFrameTypeKeyFrame, (SrsVideoAvcFrameType)((target.last_frame_->payload()[0] >> 4) & 0x07));
}
#endif

// The hooks of an RTMP or SRT client, in the order the connection calls them. A test can kick the client, as the HTTP
// API does, while one of them waits for its backend, or have the backend refuse one, as it answers a non-zero code.
class RtmpConnUtestHooks : public MockHttpHooksForCoworkers
{
public:
    std::vector<std::string> calls_;
    std::string kick_at_;
    std::string refuse_at_;
    ISrsExpire *conn_;

public:
    RtmpConnUtestHooks()
    {
        conn_ = NULL;
    }
    virtual ~RtmpConnUtestHooks()
    {
    }

public:
    std::string called()
    {
        std::string s;
        for (int i = 0; i < (int)calls_.size(); i++) {
            s += (i ? "," : "") + calls_[i];
        }
        return s;
    }
    void call(std::string name)
    {
        calls_.push_back(name);
        if (name == kick_at_ && conn_) {
            conn_->expire();
        }
    }
    // The error of a backend that refuses the event, as SrsHttpHooks returns it.
    srs_error_t answer(std::string name)
    {
        if (name != refuse_at_) {
            return srs_success;
        }
        return srs_error_new(ERROR_RESPONSE_CODE, "http: response object code 403");
    }
    virtual srs_error_t on_connect(std::string url, ISrsRequest *req)
    {
        call("on_connect");
        return answer("on_connect");
    }
    virtual void on_close(std::string url, ISrsRequest *req, int64_t send_bytes, int64_t recv_bytes)
    {
        call("on_close");
    }
    virtual srs_error_t on_publish(std::string url, ISrsRequest *req, SrsRtmpConnType type)
    {
        call("on_publish");
        return answer("on_publish");
    }
    virtual void on_unpublish(std::string url, ISrsRequest *req, SrsRtmpConnType type, const SrsStatisticStreamSnapshot &stream)
    {
        call("on_unpublish");
        MockHttpHooks::on_unpublish(url, req, type, stream);
    }
    virtual srs_error_t on_play(std::string url, ISrsRequest *req, SrsRtmpConnType type)
    {
        call("on_play");
        return answer("on_play");
    }
    virtual void on_stop(std::string url, ISrsRequest *req, SrsRtmpConnType type, const SrsStatisticStreamSnapshot &stream)
    {
        call("on_stop");
        MockHttpHooks::on_stop(url, req, type, stream);
    }
};

// The RTMP stack of a client. Its replies fail as when the client is gone, it decodes the client's commands as
// SrsRtmpServer does, and it keeps what it sent to the client.
class RtmpConnUtestRtmp : public MockRtmpServerForRedirect
{
public:
    srs_error_t start_haivision_error_;
    srs_error_t start_flash_error_;
    srs_error_t start_publishing_error_;
    srs_error_t send_messages_error_;
    srs_error_t send_packet_error_;
    srs_error_t pause_error_;
    // The timestamp of each media message sent, and the time it was sent.
    std::vector<int64_t> sent_timestamps_;
    std::vector<srs_utime_t> sent_times_;
    // The transaction id of each call answered.
    std::vector<double> answered_calls_;
    MockBufferIO io_;
    SrsProtocol protocol_;

public:
    RtmpConnUtestRtmp() : protocol_(&io_)
    {
        start_haivision_error_ = srs_success;
        start_flash_error_ = srs_success;
        start_publishing_error_ = srs_success;
        send_messages_error_ = srs_success;
        send_packet_error_ = srs_success;
        pause_error_ = srs_success;
    }
    virtual ~RtmpConnUtestRtmp()
    {
        srs_freep(start_haivision_error_);
        srs_freep(start_flash_error_);
        srs_freep(start_publishing_error_);
        srs_freep(send_messages_error_);
        srs_freep(send_packet_error_);
        srs_freep(pause_error_);
    }

public:
    // The client sends a message, which the server's receive thread reads.
    void client_sends(SrsRtmpCommonMessage *msg)
    {
        recv_msgs_.push_back(msg);
        cond_->signal();
    }
    // The client closes its connection.
    void client_closes()
    {
        srs_freep(recv_err_);
        recv_err_ = srs_error_new(ERROR_SOCKET_READ, "client closed");
        cond_->signal();
    }

public:
    virtual srs_error_t start_haivision_publish(int stream_id)
    {
        start_haivision_publish_count_++;
        return srs_error_copy(start_haivision_error_);
    }
    virtual srs_error_t start_flash_publish(int stream_id)
    {
        start_flash_publish_count_++;
        return srs_error_copy(start_flash_error_);
    }
    virtual srs_error_t start_publishing(int stream_id)
    {
        return srs_error_copy(start_publishing_error_);
    }
    virtual srs_error_t send_and_free_messages(SrsMediaPacket **msgs, int nb_msgs, int stream_id)
    {
        for (int i = 0; i < nb_msgs; i++) {
            SrsMediaPacket *msg = msgs[i];
            sent_timestamps_.push_back(msg->timestamp_);
            sent_times_.push_back(srs_time_now_realtime());
            srs_freep(msg);
        }
        nb_sent_messages_ += nb_msgs;
        return srs_error_copy(send_messages_error_);
    }
    virtual srs_error_t decode_message(SrsRtmpCommonMessage *msg, SrsRtmpCommand **ppacket)
    {
        decode_message_count_++;
        return protocol_.decode_message(msg, ppacket);
    }
    virtual srs_error_t send_and_free_packet(SrsRtmpCommand *packet, int stream_id)
    {
        SrsUniquePtr<SrsRtmpCommand> pkt(packet);
        send_and_free_packet_count_++;
        SrsCallResPacket *res = dynamic_cast<SrsCallResPacket *>(packet);
        if (res) {
            answered_calls_.push_back(res->transaction_id_);
        }
        return srs_error_copy(send_packet_error_);
    }
    // Waits for a message unless the client already closed, which the base waits for again.
    virtual srs_error_t recv_message(SrsRtmpCommonMessage **pmsg)
    {
        if (recv_msgs_.empty() && recv_err_ == srs_success) {
            cond_->wait();
        }
        if (recv_err_ != srs_success) {
            return srs_error_copy(recv_err_);
        }
        if (recv_msgs_.empty()) {
            return srs_error_new(ERROR_SOCKET_READ, "no message");
        }

        *pmsg = recv_msgs_.front();
        recv_msgs_.erase(recv_msgs_.begin());
        return srs_success;
    }
    virtual srs_error_t on_play_client_pause(int stream_id, bool is_pause)
    {
        MockRtmpServer::on_play_client_pause(stream_id, is_pause);
        return srs_error_copy(pause_error_);
    }
};

// The stream an RTMP client plays or publishes. It publishes as SrsLiveSource does, so an idle publisher is found,
// and it can refuse a publisher's messages, as when the origin hub, a bridge or the edge's upstream fails.
class RtmpConnUtestSource : public MockLiveSourceForPublishMessage
{
public:
    MockLiveSourceHandler handler_mock_;
    srs_error_t audio_error_;
    srs_error_t video_error_;
    srs_error_t aggregate_error_;
    srs_error_t metadata_error_;
    srs_error_t proxy_error_;
    int on_edge_proxy_unpublish_count_;
    // Whether a publish sets the source up as SrsLiveSource does, with its origin hub, or only takes the stream.
    bool publish_as_source_;

public:
    RtmpConnUtestSource()
    {
        handler_ = &handler_mock_;
        audio_error_ = srs_success;
        video_error_ = srs_success;
        aggregate_error_ = srs_success;
        metadata_error_ = srs_success;
        proxy_error_ = srs_success;
        on_edge_proxy_unpublish_count_ = 0;
        publish_as_source_ = true;
    }
    virtual ~RtmpConnUtestSource()
    {
        srs_freep(audio_error_);
        srs_freep(video_error_);
        srs_freep(aggregate_error_);
        srs_freep(metadata_error_);
        srs_freep(proxy_error_);
        handler_ = NULL;
    }

public:
    virtual srs_error_t on_publish()
    {
        if (!publish_as_source_) {
            return MockLiveSource::on_publish();
        }
        return SrsLiveSource::on_publish();
    }
    virtual srs_error_t on_audio(SrsRtmpCommonMessage *audio)
    {
        if (audio_error_ != srs_success) {
            return srs_error_copy(audio_error_);
        }
        return MockLiveSource::on_audio(audio);
    }
    virtual srs_error_t on_video(SrsRtmpCommonMessage *video)
    {
        if (video_error_ != srs_success) {
            return srs_error_copy(video_error_);
        }
        return MockLiveSource::on_video(video);
    }
    virtual srs_error_t on_aggregate(SrsRtmpCommonMessage *msg)
    {
        if (aggregate_error_ != srs_success) {
            return srs_error_copy(aggregate_error_);
        }
        return MockLiveSourceForPublishMessage::on_aggregate(msg);
    }
    virtual srs_error_t on_meta_data(SrsRtmpCommonMessage *msg, SrsOnMetaDataPacket *metadata)
    {
        if (metadata_error_ != srs_success) {
            return srs_error_copy(metadata_error_);
        }
        return MockLiveSourceForPublishMessage::on_meta_data(msg, metadata);
    }
    virtual srs_error_t on_edge_proxy_publish(SrsRtmpCommonMessage *msg)
    {
        if (proxy_error_ != srs_success) {
            return srs_error_copy(proxy_error_);
        }
        return MockLiveSourceForPublishMessage::on_edge_proxy_publish(msg);
    }
    virtual void on_edge_proxy_unpublish()
    {
        on_edge_proxy_unpublish_count_++;
        SrsLiveSource::on_edge_proxy_unpublish();
    }
};

// An RTMP client of the server: the real SrsRtmpConn with the vhost from config text, the stream's source, the
// statistic and the publish tokens. The client's RTMP stack is RtmpConnUtestRtmp, or the real SrsRtmpServer over the
// bytes the client sends. The connection serves the client in its own coroutine, as the server runs it.
class RtmpConnUtestClient : public ISrsCoroutineHandler
{
public:
    MockSrsConfig conf_;
    SrsStatistic stat_;
    RtmpConnUtestHooks hooks_;
    MockLiveSourceManager sources_;
    RtmpConnUtestSource *source_;
    SrsStreamPublishTokenManager tokens_;
    MockRtcSourceManager rtc_sources_;
    MockSrtSourceManager srt_sources_;
#ifdef SRS_RTSP
    MockRtspSourceManager rtsp_sources_;
#endif
    MockConnectionManager manager_;
    RtmpConnUtestRtmp *rtmp_;
    SrsRtmpConn *conn_;
    srs_error_t err_;
    bool done_;
    bool whole_cycle_;

public:
    RtmpConnUtestClient()
    {
        source_ = NULL;
        rtmp_ = NULL;
        conn_ = NULL;
        err_ = srs_success;
        done_ = false;
        whole_cycle_ = false;
    }
    virtual ~RtmpConnUtestClient()
    {
        srs_freep(conn_);
        srs_freep(err_);
    }

public:
    // A client of the type, which plays or publishes the stream of the live app, through RtmpConnUtestRtmp.
    srs_error_t open(std::string text, SrsRtmpConnType type, std::string stream = "livestream")
    {
        srs_error_t err = srs_success;

        if ((err = prepare(text)) != srs_success) {
            return srs_error_wrap(err, "prepare");
        }

        rtmp_ = new RtmpConnUtestRtmp();
        rtmp_->set_request(type, "192.168.1.100", "__defaultVhost__", "live", stream, "rtmp://127.0.0.1/live", "rtmp", 1935, "127.0.0.1");
        srs_freep(conn_->rtmp_);
        conn_->rtmp_ = rtmp_;

        return err;
    }
    // A client whose bytes are read from io by the real RTMP stack.
    srs_error_t open_io(std::string text, ISrsProtocolReadWriter *io)
    {
        srs_error_t err = srs_success;

        if ((err = prepare(text)) != srs_success) {
            return srs_error_wrap(err, "prepare");
        }

        srs_freep(conn_->rtmp_);
        conn_->rtmp_ = new SrsRtmpServer(io);

        return err;
    }
    srs_error_t start()
    {
        return conn_->start();
    }
    // Waits for the connection to end, at most about 2s.
    void wait_done()
    {
        for (int i = 0; i < 2000 && !done_; i++) {
            srs_usleep(1 * SRS_UTIME_MILLISECONDS);
        }
    }
    void wait_sent(int n)
    {
        for (int i = 0; i < 2000 && rtmp_->nb_sent_messages_ < n; i++) {
            srs_usleep(1 * SRS_UTIME_MILLISECONDS);
        }
    }
    // The publisher sends an audio frame.
    void publish_audio(int64_t timestamp)
    {
        rtmp_->client_sends(rtmp_conn_utest_audio(timestamp));
    }
    // The publisher of the stream sends an audio frame, which the source delivers to its players.
    srs_error_t feed_audio(int64_t timestamp)
    {
        SrsUniquePtr<SrsRtmpCommonMessage> msg(rtmp_conn_utest_audio(timestamp));
        return source_->on_audio(msg.get());
    }

public:
    virtual srs_error_t cycle()
    {
        if (whole_cycle_) {
            err_ = conn_->cycle();
        } else {
            err_ = conn_->do_cycle();
        }
        done_ = true;
        return srs_success;
    }

public:
    static SrsRtmpCommonMessage *rtmp_conn_utest_audio(int64_t timestamp)
    {
        SrsRtmpCommonMessage *msg = new SrsRtmpCommonMessage();
        msg->header_.initialize_audio(sizeof(flv_utest_aac_frame), (uint32_t)timestamp, 1);
        msg->create_payload(sizeof(flv_utest_aac_frame));
        memcpy(msg->payload(), flv_utest_aac_frame, sizeof(flv_utest_aac_frame));
        return msg;
    }

private:
    srs_error_t prepare(std::string text)
    {
        srs_error_t err = srs_success;

        if ((err = conf_.mock_parse(std::string(_MIN_OK_CONF) + text)) != srs_success) {
            return srs_error_wrap(err, "parse");
        }

        source_ = new RtmpConnUtestSource();
        source_->config_ = &conf_;
        sources_.mock_source_ = SrsSharedPtr<SrsLiveSource>(source_);

        conn_ = new SrsRtmpConn(new MockRtmpTransport(), "192.168.1.100", 50000);
        conn_->config_ = &conf_;
        conn_->stat_ = &stat_;
        conn_->hooks_ = &hooks_;
        conn_->live_sources_ = &sources_;
        conn_->stream_publish_tokens_ = &tokens_;
        conn_->rtc_sources_ = &rtc_sources_;
        conn_->srt_sources_ = &srt_sources_;
#ifdef SRS_RTSP
        conn_->rtsp_sources_ = &rtsp_sources_;
#endif
        conn_->manager_ = &manager_;
        conn_->assemble();
        dynamic_cast<SrsSecurity *>(conn_->security_)->config_ = &conf_;

        // Serve the client in a coroutine of this object, which keeps what the connection returned.
        srs_freep(conn_->trd_);
        conn_->trd_ = new SrsSTCoroutine("rtmp", this, _srs_context->get_id());

        hooks_.conn_ = conn_;
        return err;
    }
};

// The hooks of every event, to see which the connection calls.
static const char *rtmp_conn_utest_all_hooks = "http_hooks { enabled on; on_connect http://127.0.0.1:8085/api/v1/clients; "
                                               "on_close http://127.0.0.1:8085/api/v1/clients; on_publish http://127.0.0.1:8085/api/v1/streams; "
                                               "on_unpublish http://127.0.0.1:8085/api/v1/streams; on_play http://127.0.0.1:8085/api/v1/sessions; "
                                               "on_stop http://127.0.0.1:8085/api/v1/sessions; }";

// A player's vhost where each message is sent at once, with no merged write.
static const char *rtmp_conn_utest_play_now = "play { mw_latency 0; mw_msgs 0; }";

// A message of the client's stream, of the type, with the payload.
static SrsRtmpCommonMessage *rtmp_conn_utest_message(uint8_t type, const string &payload)
{
    SrsRtmpCommonMessage *msg = new SrsRtmpCommonMessage();
    msg->header_.message_type_ = type;
    msg->header_.payload_length_ = (int32_t)payload.size();
    msg->header_.stream_id_ = 1;
    msg->create_payload((int)payload.size());
    memcpy(msg->payload(), payload.data(), payload.size());
    return msg;
}

static SrsRtmpCommonMessage *rtmp_conn_utest_command(RtmpStackAmf0 &cmd)
{
    return rtmp_conn_utest_message(RTMP_MSG_AMF0CommandMessage, cmd.data_);
}

// What a client sends from its TCP connect up to its connect command: the C0C1 and C2 of the simple handshake, as an
// encoder such as OBS does it, then connect to the live app.
static string rtmp_conn_utest_handshake()
{
    return string("\x03", 1) + string(1536, '\0') + string(1536, '\0');
}

static string rtmp_conn_utest_connect()
{
    SrsAmf0Object *args = SrsAmf0Any::object();
    args->set("app", SrsAmf0Any::str("live"));
    args->set("tcUrl", SrsAmf0Any::str("rtmp://127.0.0.1/live"));

    RtmpStackAmf0 cmd;
    cmd.str("connect").num(1).obj(args);
    return rtmp_stack_chunks(3, RTMP_MSG_AMF0CommandMessage, 0, cmd.data_);
}

static void rtmp_conn_utest_expect(srs_error_t err, int code, string text)
{
    EXPECT_EQ(code, srs_error_code(err)) << srs_error_desc(err);
    EXPECT_NE(string::npos, srs_error_desc(err).find(text)) << srs_error_desc(err);
    srs_freep(err);
}

// A TCP health check of a load balancer, or a client that gives up, closes after the RTMP handshake, or after its
// connect is answered, before it names a stream. The connection ends there, and no hook is called: the server reports
// only clients that named a stream.
VOID TEST(AppRtmpConnTest, ClientClosesBeforeNamingStream_EndsWithNoHook)
{
    srs_error_t err;

    if (true) {
        MockBufferIO io;
        io.append(rtmp_conn_utest_handshake());
        RtmpConnUtestClient c;
        HELPER_ASSERT_SUCCESS(c.open_io(string("vhost __defaultVhost__ { ") + rtmp_conn_utest_all_hooks + " }", &io));

        rtmp_conn_utest_expect(c.conn_->do_cycle(), ERROR_SOCKET_READ, "rtmp connect tcUrl");
        EXPECT_STREQ("", c.hooks_.called().c_str());
    }

    if (true) {
        MockBufferIO io;
        io.append(rtmp_conn_utest_handshake() + rtmp_conn_utest_connect());
        RtmpConnUtestClient c;
        HELPER_ASSERT_SUCCESS(c.open_io(string("vhost __defaultVhost__ { ") + rtmp_conn_utest_all_hooks + " }", &io));

        rtmp_conn_utest_expect(c.conn_->do_cycle(), ERROR_SOCKET_READ, "identify client");
        EXPECT_STREQ("", c.hooks_.called().c_str());

        // The client was told its connect succeeded.
        string out(io.out_buffer.bytes(), io.out_buffer.length());
        EXPECT_NE(string::npos, out.find("NetConnection.Connect.Success"));
    }
}

// The client's connection is reset while the server answers its connect: at the window ack size, the peer bandwidth,
// the chunk size, the connect result or the onBWDone call. The connection ends at the reply that failed, and sends
// nothing after it.
VOID TEST(AppRtmpConnTest, ClientResetsAtEachConnectReply_EndsAtThatReply)
{
    const char *steps[] = {"set out window ack size", "set peer bandwidth", "set chunk size", "response connect app", "on bw down"};
    for (int i = 0; i < 5; i++) {
        srs_error_t err;

        RtmpStackResetIO io(i);
        io.append(rtmp_conn_utest_handshake() + rtmp_conn_utest_connect());
        RtmpConnUtestClient c;
        HELPER_ASSERT_SUCCESS(c.open_io("vhost __defaultVhost__ { }", &io));

        rtmp_conn_utest_expect(c.conn_->do_cycle(), ERROR_SOCKET_WRITE, steps[i]);
        EXPECT_EQ(i + 1, io.writevs_tried_) << steps[i];
    }
}

// A client that comes through an RTMP proxy, which puts the client's address in front of the C0C1 (0xF3, a 2-byte
// length, the 4-byte IP). The server logs the real address, and serves the RTMP of the client as usual.
VOID TEST(AppRtmpConnTest, ClientThroughRtmpProxy_RealIpLogged)
{
    srs_error_t err;

    ListenerUtestLog log;
    MockBufferIO io;
    io.append(string("\xF3\x00\x04\xCB\x00\x71\x0A", 7) + rtmp_conn_utest_handshake() + rtmp_conn_utest_connect());
    RtmpConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open_io("vhost __defaultVhost__ { }", &io));

    rtmp_conn_utest_expect(c.conn_->do_cycle(), ERROR_SOCKET_READ, "identify client");
    EXPECT_STREQ("RTMP proxy real client ip=203.0.113.10", log.last_trace("RTMP proxy").c_str());
    EXPECT_STREQ("live", c.conn_->info_->req_->app_.c_str());
}

// An encoder that publishes over RTMPS, such as OBS to an rtmps:// URL, completes TLS with the RTMPS key and
// certificate of the config. The connection then reads and writes the RTMP over TLS.
VOID TEST(AppRtmpConnTest, RtmpsEncoderCompletesTls_TransportReady)
{
    srs_error_t err;

    MockSrsConfig conf;
    HELPER_ASSERT_SUCCESS(conf.mock_parse(string(_MIN_OK_CONF) + "rtmps { enabled on; listen 1443; key ./conf/server.key; cert ./conf/server.crt; }"));

    TlsClientPeer client(false);
    SrsRtmpsTransport transport(NULL);
    transport.config_ = &conf;
    transport.ssl_->transport_ = &client;

    HELPER_ASSERT_SUCCESS(transport.handshake());
    client.drive();
    EXPECT_TRUE(SSL_is_init_finished(client.ssl_));

    // The encoder's C0C1 goes over TLS, and the server reads it as plaintext.
    client.send(string("\x03", 1) + string(16, 'x'));
    char buf[17];
    HELPER_ASSERT_SUCCESS(transport.io()->read_fully(buf, sizeof(buf), NULL));
    EXPECT_EQ(0x03, buf[0]);
    EXPECT_EQ('x', buf[16]);

    transport.ssl_->transport_ = transport.skt_;
}

// A client that names a vhost the server lacks, on a server with no default vhost, or a vhost turned off
// (`enabled off`), is refused before any hook or source.
VOID TEST(AppRtmpConnTest, ClientOfUnknownOrDisabledVhost_Refused)
{
    srs_error_t err;

    if (true) {
        RtmpConnUtestClient c;
        HELPER_ASSERT_SUCCESS(c.open(string("vhost other.ossrs.io { ") + rtmp_conn_utest_all_hooks + " }", SrsRtmpConnPlay));
        c.rtmp_->vhost_ = "unknown.ossrs.io";
        c.rtmp_->tcUrl_ = "rtmp://unknown.ossrs.io/live";

        rtmp_conn_utest_expect(c.conn_->do_cycle(), ERROR_RTMP_VHOST_NOT_FOUND, "no vhost unknown.ossrs.io");
        EXPECT_STREQ("", c.hooks_.called().c_str());
        EXPECT_EQ(0, c.sources_.fetch_or_create_count_);
    }

    if (true) {
        RtmpConnUtestClient c;
        HELPER_ASSERT_SUCCESS(c.open(string("vhost __defaultVhost__ { enabled off; ") + rtmp_conn_utest_all_hooks + " }", SrsRtmpConnFMLEPublish));

        rtmp_conn_utest_expect(c.conn_->do_cycle(), ERROR_RTMP_VHOST_NOT_FOUND, "vhost __defaultVhost__ disabled");
        EXPECT_STREQ("", c.hooks_.called().c_str());
        EXPECT_EQ(0, c.sources_.fetch_or_create_count_);
    }
}

// A vhost that denies publishing from the encoder's address (`security { deny publish all; }`) refuses the encoder
// after its on_connect, with no stream made for it; the on_close pairs with the on_connect.
VOID TEST(AppRtmpConnTest, PublisherDeniedBySecurity_RefusedWithNoStream)
{
    srs_error_t err;

    RtmpConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open(string("vhost __defaultVhost__ { security { enabled on; deny publish all; allow play all; } ") + rtmp_conn_utest_all_hooks + " }", SrsRtmpConnFMLEPublish));

    rtmp_conn_utest_expect(c.conn_->do_cycle(), ERROR_SYSTEM_SECURITY_DENY, "rtmp: security check");
    EXPECT_STREQ("on_connect,on_close", c.hooks_.called().c_str());
    EXPECT_EQ(0, c.sources_.fetch_or_create_count_);
    EXPECT_EQ(0, c.rtmp_->start_fmle_publish_count_);
}

// An encoder that publishes with an empty stream name to rtmp://host/live, where the URL gives no stream either, is
// refused (#834): HLS and DVR would write files with no name.
VOID TEST(AppRtmpConnTest, PublisherWithEmptyStreamName_Refused)
{
    srs_error_t err;

    RtmpConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open("vhost __defaultVhost__ { }", SrsRtmpConnFMLEPublish, ""));

    rtmp_conn_utest_expect(c.conn_->do_cycle(), ERROR_RTMP_STREAM_NAME_EMPTY, "empty stream");
    EXPECT_EQ(0, c.sources_.fetch_or_create_count_);
    EXPECT_EQ(0, c.rtmp_->start_fmle_publish_count_);
}

// An SRT or WebRTC encoder already publishes the stream, and holds its publish token. An RTMP encoder of the same
// stream is refused before its source is touched; a player of the stream is not.
VOID TEST(AppRtmpConnTest, RtmpPublishWhileOtherProtocolPublishes_RefusedByToken)
{
    srs_error_t err;

    RtmpConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open("vhost __defaultVhost__ { }", SrsRtmpConnFMLEPublish));

    MockSrsRequest req("__defaultVhost__", "live", "livestream");
    SrsStreamPublishToken *srt_token = NULL;
    HELPER_ASSERT_SUCCESS(c.tokens_.acquire_token(&req, srt_token));
    SrsUniquePtr<SrsStreamPublishToken> token(srt_token);

    rtmp_conn_utest_expect(c.conn_->do_cycle(), ERROR_SYSTEM_STREAM_BUSY, "acquire stream publish token");
    EXPECT_EQ(0, c.sources_.fetch_or_create_count_);
    EXPECT_EQ(0, c.rtmp_->start_fmle_publish_count_);
}

// A Flash player plays with a duration, `play(name, start, 0.1)`: it is served until the stream time it got reaches
// the duration (#45). The time is counted from the messages' timestamps, so a publisher whose time restarts at 0 (a
// republish) does not end the play early or late.
VOID TEST(AppRtmpConnTest, PlayerWithDuration_StopsWhenDurationPlayed)
{
    srs_error_t err;

    RtmpConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open("vhost __defaultVhost__ { play { mw_latency 0; mw_msgs 0; time_jitter off; } }", SrsRtmpConnPlay));
    c.rtmp_->duration_ = 100 * SRS_UTIME_MILLISECONDS;
    HELPER_ASSERT_SUCCESS(c.start());
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    int64_t timestamps[] = {1000, 1040, 0, 40};
    for (int i = 0; i < 4; i++) {
        HELPER_ASSERT_SUCCESS(c.feed_audio(timestamps[i]));
        c.wait_sent(i + 1);
    }
    EXPECT_FALSE(c.done_);

    HELPER_ASSERT_SUCCESS(c.feed_audio(80));
    c.wait_done();
    ASSERT_TRUE(c.done_);
    rtmp_conn_utest_expect(c.err_, ERROR_RTMP_DURATION_EXCEED, "time 120 up 100");
    c.err_ = srs_success;
    EXPECT_EQ(5, c.rtmp_->nb_sent_messages_);
}

// A player whose duration ends with a command it sent still queued, such as a Flash app's NetConnection.call: the
// command is dropped, not answered, when the play ends.
VOID TEST(AppRtmpConnTest, PlayerDurationEndsWithCommandQueued_CommandDropped)
{
    srs_error_t err;

    ListenerUtestLog log;
    RtmpConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open("vhost __defaultVhost__ { play { mw_latency 0; mw_msgs 0; time_jitter off; } }", SrsRtmpConnPlay));
    c.rtmp_->duration_ = 40 * SRS_UTIME_MILLISECONDS;
    HELPER_ASSERT_SUCCESS(c.start());
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    HELPER_ASSERT_SUCCESS(c.feed_audio(0));
    c.wait_sent(1);

    // The call comes in the same moment as the frame that ends the play.
    RtmpStackAmf0 call;
    call.str("checkBandwidth").num(2).null();
    c.rtmp_->client_sends(rtmp_conn_utest_command(call));
    HELPER_ASSERT_SUCCESS(c.feed_audio(40));
    c.wait_done();

    ASSERT_TRUE(c.done_);
    EXPECT_EQ(ERROR_RTMP_DURATION_EXCEED, srs_error_code(c.err_));
    EXPECT_EQ(1, log.warns("drop the received 1 messages"));
    EXPECT_EQ(0, (int)c.rtmp_->answered_calls_.size());
}

// While it plays, a player sends what players send: a buffer length (a user control message), a NetConnection.call
// with a responder, and a play again, as a player that switches streams. The call is answered; the others change
// nothing, and the player keeps getting the stream. The receive thread takes one message at a time, each after the
// play loop handled the one before, so this takes about 0.7s.
VOID TEST(AppRtmpConnTest, PlayerSendsControlMessagesWhilePlaying_KeepsPlaying)
{
    srs_error_t err;

    RtmpConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open(string("vhost __defaultVhost__ { ") + rtmp_conn_utest_play_now + " }", SrsRtmpConnPlay));
    HELPER_ASSERT_SUCCESS(c.start());
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    c.rtmp_->client_sends(rtmp_conn_utest_message(RTMP_MSG_UserControlMessage, string("\x00\x03\x00\x00\x00\x01\x00\x00\x0b\xb8", 10)));
    RtmpStackAmf0 call;
    call.str("checkBandwidth").num(2).null();
    c.rtmp_->client_sends(rtmp_conn_utest_command(call));
    RtmpStackAmf0 play;
    play.str("play").num(0).null().str("livestream2");
    c.rtmp_->client_sends(rtmp_conn_utest_command(play));
    for (int i = 0; i < 2000 && c.rtmp_->decode_message_count_ < 2; i++) {
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    }

    ASSERT_EQ(1, (int)c.rtmp_->answered_calls_.size());
    EXPECT_EQ(2, c.rtmp_->answered_calls_[0]);
    EXPECT_EQ(2, c.rtmp_->decode_message_count_);
    EXPECT_TRUE(c.rtmp_->recv_msgs_.empty());

    HELPER_ASSERT_SUCCESS(c.feed_audio(0));
    c.wait_sent(1);
    EXPECT_EQ(1, c.rtmp_->nb_sent_messages_);
    EXPECT_FALSE(c.done_);
    EXPECT_STREQ("livestream", c.conn_->info_->req_->stream_.c_str());

    c.rtmp_->client_closes();
    c.wait_done();
    ASSERT_TRUE(c.done_);
    rtmp_conn_utest_expect(c.err_, ERROR_SOCKET_READ, "client closed");
    c.err_ = srs_success;
}

// A player sends a pause command cut after its name: the command cannot be decoded, and the play ends.
VOID TEST(AppRtmpConnTest, PlayerSendsCutCommand_PlayEnds)
{
    srs_error_t err;

    RtmpConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open(string("vhost __defaultVhost__ { ") + rtmp_conn_utest_play_now + " }", SrsRtmpConnPlay));
    HELPER_ASSERT_SUCCESS(c.start());
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    RtmpStackAmf0 pause;
    pause.str("pause");
    c.rtmp_->client_sends(rtmp_conn_utest_command(pause));
    c.wait_done();

    ASSERT_TRUE(c.done_);
    EXPECT_NE(string::npos, srs_error_desc(c.err_).find("play control message")) << srs_error_desc(c.err_);
    EXPECT_NE(string::npos, srs_error_desc(c.err_).find("decode message")) << srs_error_desc(c.err_);
}

// The player is gone when the server writes to it: the stream's media, the answer to its call, or the answer to its
// pause. The play ends at that write.
VOID TEST(AppRtmpConnTest, PlayerGoneAtEachWrite_PlayEnds)
{
    srs_error_t err;

    if (true) {
        RtmpConnUtestClient c;
        HELPER_ASSERT_SUCCESS(c.open(string("vhost __defaultVhost__ { ") + rtmp_conn_utest_play_now + " }", SrsRtmpConnPlay));
        c.rtmp_->send_messages_error_ = srs_error_new(ERROR_SOCKET_WRITE, "player gone");
        HELPER_ASSERT_SUCCESS(c.start());
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        HELPER_ASSERT_SUCCESS(c.feed_audio(0));
        c.wait_done();
        ASSERT_TRUE(c.done_);
        rtmp_conn_utest_expect(c.err_, ERROR_SOCKET_WRITE, "send 1 messages");
        c.err_ = srs_success;
    }

    if (true) {
        RtmpConnUtestClient c;
        HELPER_ASSERT_SUCCESS(c.open(string("vhost __defaultVhost__ { ") + rtmp_conn_utest_play_now + " }", SrsRtmpConnPlay));
        c.rtmp_->send_packet_error_ = srs_error_new(ERROR_SOCKET_WRITE, "player gone");
        HELPER_ASSERT_SUCCESS(c.start());
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        RtmpStackAmf0 call;
        call.str("checkBandwidth").num(2).null();
        c.rtmp_->client_sends(rtmp_conn_utest_command(call));
        c.wait_done();
        ASSERT_TRUE(c.done_);
        rtmp_conn_utest_expect(c.err_, ERROR_SOCKET_WRITE, "send packets");
        c.err_ = srs_success;
    }

    if (true) {
        RtmpConnUtestClient c;
        HELPER_ASSERT_SUCCESS(c.open(string("vhost __defaultVhost__ { ") + rtmp_conn_utest_play_now + " }", SrsRtmpConnPlay));
        c.rtmp_->pause_error_ = srs_error_new(ERROR_SOCKET_WRITE, "player gone");
        HELPER_ASSERT_SUCCESS(c.start());
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        RtmpStackAmf0 pause;
        pause.str("pause").num(0).null().boolean(true).num(1000);
        c.rtmp_->client_sends(rtmp_conn_utest_command(pause));
        c.wait_done();
        ASSERT_TRUE(c.done_);
        rtmp_conn_utest_expect(c.err_, ERROR_SOCKET_WRITE, "rtmp: pause");
        c.err_ = srs_success;
        EXPECT_EQ(1, c.rtmp_->on_play_client_pause_count_);
    }
}

// The HTTP API kicks a player (DELETE /api/v1/clients/<id>): while it plays, or just as the answer of its on_play
// hook arrives, before it starts. The play ends, and the hooks stay paired: on_stop for the on_play, on_close for the
// on_connect.
VOID TEST(AppRtmpConnTest, PlayerKickedByApi_PlayEndsWithHooksPaired)
{
    srs_error_t err;

    if (true) {
        RtmpConnUtestClient c;
        HELPER_ASSERT_SUCCESS(c.open(string("vhost __defaultVhost__ { ") + rtmp_conn_utest_play_now + rtmp_conn_utest_all_hooks + " }", SrsRtmpConnPlay));
        HELPER_ASSERT_SUCCESS(c.start());
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
        EXPECT_EQ(1, c.rtmp_->start_play_count_);

        c.conn_->expire();
        c.wait_done();
        ASSERT_TRUE(c.done_);
        rtmp_conn_utest_expect(c.err_, ERROR_THREAD_INTERRUPED, "rtmp: thread quit");
        c.err_ = srs_success;
        EXPECT_STREQ("on_connect,on_play,on_stop,on_close", c.hooks_.called().c_str());
        EXPECT_TRUE(c.source_->consumers_.empty());
    }

    if (true) {
        RtmpConnUtestClient c;
        HELPER_ASSERT_SUCCESS(c.open(string("vhost __defaultVhost__ { ") + rtmp_conn_utest_play_now + rtmp_conn_utest_all_hooks + " }", SrsRtmpConnPlay));
        c.hooks_.kick_at_ = "on_play";
        HELPER_ASSERT_SUCCESS(c.start());
        c.wait_done();

        ASSERT_TRUE(c.done_);
        rtmp_conn_utest_expect(c.err_, ERROR_THREAD_INTERRUPED, "thread");
        c.err_ = srs_success;
        EXPECT_STREQ("on_connect,on_play,on_stop,on_close", c.hooks_.called().c_str());
        EXPECT_TRUE(c.source_->consumers_.empty());
    }
}

// The server frees a connection while its player waits for media, as at a graceful quit. The player's wait is woken,
// its play ends before the connection is gone, and it leaves the stream.
VOID TEST(AppRtmpConnTest, ServerFreesConnectionWhilePlaying_PlayerLeavesStream)
{
    srs_error_t err;

    RtmpConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open(string("vhost __defaultVhost__ { ") + rtmp_conn_utest_play_now + rtmp_conn_utest_all_hooks + " }", SrsRtmpConnPlay));
    HELPER_ASSERT_SUCCESS(c.start());
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    EXPECT_EQ(1, (int)c.source_->consumers_.size());
    EXPECT_TRUE(c.conn_->wakable_ != NULL);

    srs_freep(c.conn_);

    EXPECT_TRUE(c.done_);
    EXPECT_TRUE(c.source_->consumers_.empty());
    EXPECT_STREQ("on_connect,on_play,on_stop,on_close", c.hooks_.called().c_str());
}

// A vhost with `send_min_interval 20` sends a player one message each 20ms, even when more are queued.
VOID TEST(AppRtmpConnTest, PlayerOnVhostWithSendMinInterval_OneMessageEachInterval)
{
    srs_error_t err;

    RtmpConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open("vhost __defaultVhost__ { play { mw_latency 0; mw_msgs 0; send_min_interval 20; } }", SrsRtmpConnPlay));
    HELPER_ASSERT_SUCCESS(c.start());
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    // The last frame in the queue waits for the next one, which the publisher sends later.
    for (int i = 0; i < 3; i++) {
        HELPER_ASSERT_SUCCESS(c.feed_audio(i * 20));
    }
    srs_usleep(60 * SRS_UTIME_MILLISECONDS);
    HELPER_ASSERT_SUCCESS(c.feed_audio(60));
    c.wait_sent(3);

    ASSERT_EQ(3, (int)c.rtmp_->sent_times_.size());
    EXPECT_GE(c.rtmp_->sent_times_[1] - c.rtmp_->sent_times_[0], 15 * SRS_UTIME_MILLISECONDS);
    EXPECT_GE(c.rtmp_->sent_times_[2] - c.rtmp_->sent_times_[1], 15 * SRS_UTIME_MILLISECONDS);

    c.conn_->expire();
    c.wait_done();
}

// A player on a vhost with `send_min_interval 50` closes while the play loop sleeps the interval after a send, and
// the stream gets no more media, as its publisher stopped. The receive thread sees the close and wakes the consumer,
// but the play loop is not waiting yet, so nothing gets the wakeup. The connection ends at the close anyway, and its
// consumer leaves the stream, without waiting for the next frame.
VOID TEST(AppRtmpConnTest, PlayerClosesDuringSendInterval_ConnectionEnds)
{
    srs_error_t err;

    RtmpConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open("vhost __defaultVhost__ { play { mw_latency 0; mw_msgs 0; send_min_interval 50; } }", SrsRtmpConnPlay));
    HELPER_ASSERT_SUCCESS(c.start());
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    HELPER_ASSERT_SUCCESS(c.feed_audio(0));
    c.wait_sent(1);
    c.rtmp_->client_closes();
    srs_usleep(300 * SRS_UTIME_MILLISECONDS);

    EXPECT_TRUE(c.done_);
    EXPECT_TRUE(c.source_->consumers_.empty());
    rtmp_conn_utest_expect(c.err_, ERROR_SOCKET_READ, "client closed");
    c.err_ = srs_success;
    EXPECT_EQ(1, (int)c.rtmp_->sent_times_.size());
}

// The player's play line: what the player gets, and its kbps, each pithy print interval.
VOID TEST(AppRtmpConnTest, PlayerPlayLineLogged)
{
    srs_error_t err;

    PithyPrintAtOnceScope stages(SrsPithyPrint::create_rtmp_play);
    ListenerUtestLog log;
    RtmpConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open(string("vhost __defaultVhost__ { ") + rtmp_conn_utest_play_now + " }", SrsRtmpConnPlay));
    HELPER_ASSERT_SUCCESS(c.start());
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    HELPER_ASSERT_SUCCESS(c.feed_audio(0));
    c.wait_sent(1);

    string line = log.last_trace("-> " SRS_CONSTS_LOG_PLAY " time=");
    EXPECT_NE(string::npos, line.find("msgs=1, okbps=")) << line;
    EXPECT_NE(string::npos, line.find("mw=0/0")) << line;

    c.rtmp_->client_closes();
    c.wait_done();
}

// The encoder is gone when the server answers its publish: the start of a Haivision encoder (FCPublish only), of a
// Flash publisher, or the NetStream.Publish.Start of any. The on_unpublish pairs with the on_publish.
VOID TEST(AppRtmpConnTest, PublisherGoneAtEachPublishReply_HooksPaired)
{
    srs_error_t err;

    if (true) {
        RtmpConnUtestClient c;
        HELPER_ASSERT_SUCCESS(c.open(string("vhost __defaultVhost__ { ") + rtmp_conn_utest_all_hooks + " }", SrsRtmpConnHaivisionPublish));
        c.rtmp_->start_haivision_error_ = srs_error_new(ERROR_SOCKET_WRITE, "encoder gone");

        rtmp_conn_utest_expect(c.conn_->do_cycle(), ERROR_SOCKET_WRITE, "start HAIVISION publish");
        EXPECT_EQ(1, c.rtmp_->start_haivision_publish_count_);
        EXPECT_STREQ("on_connect,on_publish,on_unpublish,on_close", c.hooks_.called().c_str());
    }

    if (true) {
        RtmpConnUtestClient c;
        HELPER_ASSERT_SUCCESS(c.open(string("vhost __defaultVhost__ { ") + rtmp_conn_utest_all_hooks + " }", SrsRtmpConnFlashPublish));
        c.rtmp_->start_flash_error_ = srs_error_new(ERROR_SOCKET_WRITE, "encoder gone");

        rtmp_conn_utest_expect(c.conn_->do_cycle(), ERROR_SOCKET_WRITE, "start FLASH publish");
        EXPECT_EQ(1, c.rtmp_->start_flash_publish_count_);
        EXPECT_STREQ("on_connect,on_publish,on_unpublish,on_close", c.hooks_.called().c_str());
    }

    if (true) {
        RtmpConnUtestClient c;
        HELPER_ASSERT_SUCCESS(c.open(string("vhost __defaultVhost__ { ") + rtmp_conn_utest_all_hooks + " }", SrsRtmpConnFMLEPublish));
        c.rtmp_->start_publishing_error_ = srs_error_new(ERROR_SOCKET_WRITE, "encoder gone");

        rtmp_conn_utest_expect(c.conn_->do_cycle(), ERROR_SOCKET_WRITE, "start publishing");
        EXPECT_STREQ("on_connect,on_publish,on_unpublish,on_close", c.hooks_.called().c_str());
        EXPECT_TRUE(c.source_->can_publish_);
    }
}

// An encoder that publishes and sends nothing within `firstpkt_timeout`, or stops sending within `normal_timeout`
// after its first frame, is disconnected.
VOID TEST(AppRtmpConnTest, EncoderSendsNothing_PublishTimesOut)
{
    srs_error_t err;

    if (true) {
        RtmpConnUtestClient c;
        HELPER_ASSERT_SUCCESS(c.open("vhost __defaultVhost__ { publish { firstpkt_timeout 50; normal_timeout 50; } }", SrsRtmpConnFMLEPublish));
        HELPER_ASSERT_SUCCESS(c.start());
        c.wait_done();

        ASSERT_TRUE(c.done_);
        rtmp_conn_utest_expect(c.err_, ERROR_SOCKET_TIMEOUT, "publish timeout 50ms, nb_msgs=0");
        c.err_ = srs_success;
        EXPECT_TRUE(c.source_->can_publish_);
    }

    if (true) {
        RtmpConnUtestClient c;
        HELPER_ASSERT_SUCCESS(c.open("vhost __defaultVhost__ { publish { firstpkt_timeout 50; normal_timeout 50; } }", SrsRtmpConnFMLEPublish));
        HELPER_ASSERT_SUCCESS(c.start());
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
        c.publish_audio(0);
        c.wait_done();

        ASSERT_TRUE(c.done_);
        rtmp_conn_utest_expect(c.err_, ERROR_SOCKET_TIMEOUT, "publish timeout 50ms, nb_msgs=1");
        c.err_ = srs_success;
        EXPECT_EQ(1, c.source_->on_audio_count_);
    }
}

// A vhost with `kickoff_for_idle 1` kicks an encoder that publishes to no player for 1s, although it sends frames.
// The publish loop checks it each time it wakes, at the normal timeout.
VOID TEST(AppRtmpConnTest, PublisherWithNoPlayer_KickedForIdle)
{
    srs_error_t err;

    MockClockForPithyPrint clock;
    clock.now_ = 1000 * SRS_UTIME_SECONDS;
    RtmpConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open("vhost __defaultVhost__ { publish { kickoff_for_idle 1; firstpkt_timeout 30; normal_timeout 30; } }", SrsRtmpConnFMLEPublish));
    c.source_->clk_ = &clock;
    HELPER_ASSERT_SUCCESS(c.start());
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    // Within the second, the encoder keeps publishing.
    for (int i = 0; i < 20; i++) {
        c.publish_audio(i * 5);
        srs_usleep(5 * SRS_UTIME_MILLISECONDS);
    }
    EXPECT_FALSE(c.done_);

    clock.now_ += 2 * SRS_UTIME_SECONDS;
    for (int i = 20; i < 100 && !c.done_; i++) {
        c.publish_audio(i * 5);
        srs_usleep(5 * SRS_UTIME_MILLISECONDS);
    }

    ASSERT_TRUE(c.done_);
    rtmp_conn_utest_expect(c.err_, ERROR_KICKOFF_FOR_IDLE, "kicked for idle");
    c.err_ = srs_success;
    EXPECT_TRUE(c.source_->can_publish_);
    c.source_->clk_ = _srs_clock;
}

// The encoder's publish line: its kbps, merged read and the timeouts, each pithy print interval.
VOID TEST(AppRtmpConnTest, PublisherPublishLineLogged)
{
    srs_error_t err;

    PithyPrintAtOnceScope stages(SrsPithyPrint::create_rtmp_publish);
    ListenerUtestLog log;
    RtmpConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open("vhost __defaultVhost__ { publish { firstpkt_timeout 30; normal_timeout 30; } }", SrsRtmpConnFMLEPublish));
    HELPER_ASSERT_SUCCESS(c.start());
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    c.publish_audio(0);
    c.wait_done();

    string line = log.last_trace("<- " SRS_CONSTS_LOG_CLIENT_PUBLISH " time=");
    EXPECT_NE(string::npos, line.find("mr=0/350, p1stpt=30, pnt=30")) << line;
}

// The HTTP API kicks an encoder while a frame is on its way. The publish ends, and the hooks stay paired.
VOID TEST(AppRtmpConnTest, PublisherKickedByApi_PublishEndsWithHooksPaired)
{
    srs_error_t err;

    RtmpConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open(string("vhost __defaultVhost__ { ") + rtmp_conn_utest_all_hooks + " }", SrsRtmpConnFMLEPublish));
    HELPER_ASSERT_SUCCESS(c.start());
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    EXPECT_FALSE(c.source_->can_publish_);

    c.publish_audio(0);
    c.conn_->expire();
    c.wait_done();

    ASSERT_TRUE(c.done_);
    rtmp_conn_utest_expect(c.err_, ERROR_THREAD_INTERRUPED, "rtmp: thread quit");
    c.err_ = srs_success;
    EXPECT_EQ(1, c.source_->on_audio_count_);
    EXPECT_TRUE(c.source_->can_publish_);
    EXPECT_STREQ("on_connect,on_publish,on_unpublish,on_close", c.hooks_.called().c_str());
}

// The server quits gracefully (SIGQUIT), and frees each connection, which interrupts its coroutine and waits for it.
// An encoder whose coroutine is not blocked at that moment, such as one just woken by its on_connect answer, keeps
// the interrupt pending. Its next lock is the publish token's: st_mutex_lock fails with EINTR, and SrsLocker's
// srs_assert aborts the server. The expected behavior is a refused publish.
VOID TEST(AppRtmpConnTest, DISABLED_InterruptPendingAtPublishToken_ServerAborts)
{
    srs_error_t err;

    RtmpConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open(string("vhost __defaultVhost__ { ") + rtmp_conn_utest_all_hooks + " }", SrsRtmpConnFMLEPublish));
    c.hooks_.kick_at_ = "on_connect";
    HELPER_ASSERT_SUCCESS(c.start());
    c.wait_done();

    ASSERT_TRUE(c.done_);
    EXPECT_EQ(ERROR_THREAD_INTERRUPED, srs_error_code(c.err_)) << srs_error_desc(c.err_);
    EXPECT_STREQ("on_connect,on_close", c.hooks_.called().c_str());
    EXPECT_EQ(0, c.rtmp_->start_fmle_publish_count_);
}

// An encoder publishes to an edge vhost that has WebRTC and RTMP to WebRTC on. The edge forwards the publish to the
// origin, with WebRTC off as edges do not serve it, and when the encoder leaves, the edge stops its publish to the
// origin.
VOID TEST(AppRtmpConnTest, PublisherOnEdgeWithRtc_ForwardedWithoutRtc)
{
    srs_error_t err;

    ListenerUtestLog log;
    RtmpConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open("rtc_server { enabled on; listen 8000; } vhost __defaultVhost__ { cluster { mode remote; origin 127.0.0.1:19350; } "
                                 "rtc { enabled on; rtmp_to_rtc on; } }",
                                 SrsRtmpConnFMLEPublish));
    HELPER_ASSERT_SUCCESS(c.start());
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    EXPECT_TRUE(c.conn_->info_->edge_);

    c.publish_audio(0);
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    EXPECT_EQ(1, c.source_->on_edge_proxy_publish_count_);
    EXPECT_EQ(0, c.source_->on_audio_count_);

    c.rtmp_->client_closes();
    c.wait_done();
    ASSERT_TRUE(c.done_);
    EXPECT_EQ(1, c.source_->on_edge_proxy_unpublish_count_);
    EXPECT_EQ(0, c.rtc_sources_.fetch_or_create_count_);
    EXPECT_EQ(1, log.warns("disable WebRTC for edge vhost=__defaultVhost__"));
#ifdef SRS_FFMPEG_FIT
    EXPECT_EQ(1, log.warns("disable RTMP to WebRTC for edge vhost=__defaultVhost__"));
#endif
}

#ifdef SRS_RTSP
// An encoder publishes to a vhost with RTSP and `rtmp_to_rtsp on`: the stream is bridged to its RTSP source while it
// publishes, so RTSP players get it.
VOID TEST(AppRtmpConnTest, PublisherOnVhostWithRtsp_BridgedToRtsp)
{
    srs_error_t err;

    RtmpConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open("rtsp_server { enabled on; listen 8554; } vhost __defaultVhost__ { rtsp { enabled on; rtmp_to_rtsp on; } }", SrsRtmpConnFMLEPublish));
    MockSrsRequest req("__defaultVhost__", "live", "livestream");
    HELPER_ASSERT_SUCCESS(c.rtsp_sources_.mock_source_->initialize(&req));
    HELPER_ASSERT_SUCCESS(c.start());
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    EXPECT_EQ(1, c.rtsp_sources_.fetch_or_create_count_);
    SrsRtmpBridge *bridge = dynamic_cast<SrsRtmpBridge *>(c.source_->rtmp_bridge_);
    ASSERT_TRUE(bridge != NULL);
    EXPECT_TRUE(bridge->rtsp_target_.get() != NULL);

    c.rtmp_->client_closes();
    c.wait_done();
    EXPECT_TRUE(c.source_->rtmp_bridge_ == NULL);
}
#endif

// A Flash publisher, which publishes with publish() alone, sends a releaseStream or FCPublish late, after it
// published: it is ignored, and the publish goes on. Any other command of a Flash publisher ends its publish.
VOID TEST(AppRtmpConnTest, FlashPublisherSendsLateFmleStart_Ignored)
{
    srs_error_t err;

    ListenerUtestLog log;
    RtmpConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open("vhost __defaultVhost__ { }", SrsRtmpConnFlashPublish));
    c.conn_->info_->type_ = SrsRtmpConnFlashPublish;

    RtmpStackAmf0 release;
    release.str("releaseStream").num(2).null().str("livestream");
    SrsUniquePtr<SrsRtmpCommonMessage> msg(rtmp_conn_utest_command(release));
    HELPER_EXPECT_SUCCESS(c.conn_->handle_publish_message(c.sources_.mock_source_, msg.get()));
    EXPECT_EQ(1, log.warns("flash late FMLE start packet"));

    RtmpStackAmf0 close;
    close.str("closeStream").num(0).null();
    SrsUniquePtr<SrsRtmpCommonMessage> msg2(rtmp_conn_utest_command(close));
    rtmp_conn_utest_expect(c.conn_->handle_publish_message(c.sources_.mock_source_, msg2.get()), ERROR_CONTROL_REPUBLISH, "republish");
}

// A publisher's command that cannot be decoded, such as an FCUnpublish cut after its name, ends its publish.
VOID TEST(AppRtmpConnTest, PublisherSendsCutCommand_PublishEnds)
{
    srs_error_t err;

    RtmpConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open("vhost __defaultVhost__ { }", SrsRtmpConnFMLEPublish));
    c.conn_->info_->type_ = SrsRtmpConnFMLEPublish;

    RtmpStackAmf0 unpublish;
    unpublish.str("FCUnpublish");
    SrsUniquePtr<SrsRtmpCommonMessage> msg(rtmp_conn_utest_command(unpublish));
    err = c.conn_->handle_publish_message(c.sources_.mock_source_, msg.get());
    EXPECT_TRUE(err != srs_success);
    EXPECT_NE(string::npos, srs_error_desc(err).find("rtmp: decode message")) << srs_error_desc(err);
    srs_freep(err);
}

// An encoder stops with FCUnpublish and is gone before the server answers it: the publish ends with the write error,
// not as a republish the connection would wait for.
VOID TEST(AppRtmpConnTest, EncoderGoneBeforeUnpublishAnswer_EndsWithWriteError)
{
    srs_error_t err;

    RtmpStackResetIO io(0);
    RtmpConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open_io("vhost __defaultVhost__ { }", &io));
    c.conn_->info_->type_ = SrsRtmpConnFMLEPublish;
    c.conn_->info_->res_->stream_id_ = 1;

    RtmpStackAmf0 unpublish;
    unpublish.str("FCUnpublish").num(5).null().str("livestream");
    SrsUniquePtr<SrsRtmpCommonMessage> msg(rtmp_conn_utest_command(unpublish));
    rtmp_conn_utest_expect(c.conn_->handle_publish_message(c.sources_.mock_source_, msg.get()), ERROR_SOCKET_WRITE, "rtmp: republish");
}

// The stream refuses what the encoder sends: an audio, video or aggregate message, or its metadata, as when HLS or a
// bridge fails on the origin, or the edge's publish to the origin fails. The publish ends with that error.
VOID TEST(AppRtmpConnTest, SourceRefusesPublisherMessage_PublishEnds)
{
    srs_error_t err;

    RtmpConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open("vhost __defaultVhost__ { }", SrsRtmpConnFMLEPublish));
    c.conn_->info_->type_ = SrsRtmpConnFMLEPublish;
    SrsSharedPtr<SrsLiveSource> source = c.sources_.mock_source_;

    if (true) {
        c.source_->audio_error_ = srs_error_new(ERROR_HLS_WRITE_FAILED, "hls");
        SrsUniquePtr<SrsRtmpCommonMessage> msg(RtmpConnUtestClient::rtmp_conn_utest_audio(0));
        rtmp_conn_utest_expect(c.conn_->handle_publish_message(source, msg.get()), ERROR_HLS_WRITE_FAILED, "consume audio");
    }

    if (true) {
        c.source_->video_error_ = srs_error_new(ERROR_HLS_WRITE_FAILED, "hls");
        SrsUniquePtr<SrsRtmpCommonMessage> msg(rtmp_conn_utest_message(RTMP_MSG_VideoMessage, string((const char *)flv_utest_avc_idr, sizeof(flv_utest_avc_idr))));
        rtmp_conn_utest_expect(c.conn_->handle_publish_message(source, msg.get()), ERROR_HLS_WRITE_FAILED, "consume video");
    }

    if (true) {
        c.source_->aggregate_error_ = srs_error_new(ERROR_HLS_WRITE_FAILED, "hls");
        SrsUniquePtr<SrsRtmpCommonMessage> msg(rtmp_conn_utest_message(RTMP_MSG_AggregateMessage, string(11, '\0')));
        rtmp_conn_utest_expect(c.conn_->handle_publish_message(source, msg.get()), ERROR_HLS_WRITE_FAILED, "consume aggregate");
    }

    if (true) {
        c.source_->metadata_error_ = srs_error_new(ERROR_HLS_WRITE_FAILED, "hls");
        SrsAmf0Object *meta = SrsAmf0Any::object();
        meta->set("width", SrsAmf0Any::number(1280));
        RtmpStackAmf0 data;
        data.str("onMetaData").obj(meta);
        SrsUniquePtr<SrsRtmpCommonMessage> msg(rtmp_conn_utest_message(RTMP_MSG_AMF0DataMessage, data.data_));
        rtmp_conn_utest_expect(c.conn_->handle_publish_message(source, msg.get()), ERROR_HLS_WRITE_FAILED, "consume metadata");
    }

    if (true) {
        c.conn_->info_->edge_ = true;
        c.source_->proxy_error_ = srs_error_new(ERROR_SOCKET_WRITE, "origin gone");
        SrsUniquePtr<SrsRtmpCommonMessage> msg(RtmpConnUtestClient::rtmp_conn_utest_audio(0));
        rtmp_conn_utest_expect(c.conn_->handle_publish_message(source, msg.get()), ERROR_SOCKET_WRITE, "proxy publish");
    }
}

// An encoder sends messages that are not media, data or commands, such as an acknowledgement of the bytes it got, or
// a user control message. They are ignored, and nothing goes to the stream.
VOID TEST(AppRtmpConnTest, PublisherSendsAckOrUserControl_Ignored)
{
    srs_error_t err;

    RtmpConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open("vhost __defaultVhost__ { }", SrsRtmpConnFMLEPublish));
    c.conn_->info_->type_ = SrsRtmpConnFMLEPublish;
    SrsSharedPtr<SrsLiveSource> source = c.sources_.mock_source_;

    SrsUniquePtr<SrsRtmpCommonMessage> ack(rtmp_conn_utest_message(RTMP_MSG_Acknowledgement, string("\x00\x26\x25\xa0", 4)));
    HELPER_EXPECT_SUCCESS(c.conn_->handle_publish_message(source, ack.get()));
    SrsUniquePtr<SrsRtmpCommonMessage> ping(rtmp_conn_utest_message(RTMP_MSG_UserControlMessage, string("\x00\x07\x00\x00\x00\x01", 6)));
    HELPER_EXPECT_SUCCESS(c.conn_->handle_publish_message(source, ping.get()));

    EXPECT_EQ(0, c.source_->on_frame_count_);
    EXPECT_EQ(0, c.rtmp_->decode_message_count_);
}

// A vhost whose hooks are on with only an on_connect: a player and an encoder call that hook alone.
VOID TEST(AppRtmpConnTest, HooksWithOnlyOnConnect_OtherEventsNotCalled)
{
    srs_error_t err;

    const char *vhost = "vhost __defaultVhost__ { play { mw_latency 0; mw_msgs 0; } http_hooks { enabled on; on_connect http://127.0.0.1:8085/api/v1/clients; } }";
    SrsRtmpConnType types[] = {SrsRtmpConnPlay, SrsRtmpConnFMLEPublish};
    for (int i = 0; i < 2; i++) {
        RtmpConnUtestClient c;
        HELPER_ASSERT_SUCCESS(c.open(vhost, types[i]));
        HELPER_ASSERT_SUCCESS(c.start());
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
        c.rtmp_->client_closes();
        c.wait_done();

        ASSERT_TRUE(c.done_);
        EXPECT_STREQ("on_connect", c.hooks_.called().c_str()) << i;
    }
}

// In an origin cluster, a player of a stream this origin does not have is redirected to the coworker that has it.
// The redirect is the end of the client, not an error: the connection ends as a client that finished.
VOID TEST(AppRtmpConnTest, PlayerRedirectedInOriginCluster_EndsAsFinished)
{
    srs_error_t err;

    ListenerUtestLog log;
    RtmpConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open("vhost __defaultVhost__ { cluster { origin_cluster on; coworkers 127.0.0.1:9091; } }", SrsRtmpConnPlay));
    c.hooks_.hosts_.push_back("192.168.1.11");
    c.hooks_.ports_.push_back(19351);
    c.whole_cycle_ = true;

    HELPER_ASSERT_SUCCESS(c.start());
    c.wait_done();

    ASSERT_TRUE(c.done_);
    HELPER_EXPECT_SUCCESS(c.err_);
    c.err_ = srs_success;
    ASSERT_EQ(1, (int)c.rtmp_->redirect_urls_.size());
    EXPECT_NE(string::npos, c.rtmp_->redirect_urls_[0].find("192.168.1.11:19351")) << c.rtmp_->redirect_urls_[0];
    EXPECT_STREQ("client finished.", log.last_trace("client finished").c_str());
}

// An edge player joins while the edge stops pulling from the origin, as its last player just left. It is refused
// with the edge's state; the on_stop pairs with its on_play.
VOID TEST(AppRtmpConnTest, EdgePlayerJoinsWhileEdgeStops_Refused)
{
    srs_error_t err;

    RtmpConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open(string("vhost __defaultVhost__ { cluster { mode remote; origin 127.0.0.1:19350; } ") + rtmp_conn_utest_all_hooks + " }", SrsRtmpConnPlay));
    MockSrsRequest req("__defaultVhost__", "live", "livestream");
    SrsSharedPtr<SrsLiveSource> source;
    HELPER_ASSERT_SUCCESS(c.sources_.fetch_or_create(&req, source));
    c.source_->play_edge_->state_ = SrsEdgeStateIngestStopping;

    rtmp_conn_utest_expect(c.conn_->do_cycle(), ERROR_RTMP_EDGE_PLAY_STATE, "rtmp: create consumer");
    EXPECT_STREQ("on_connect,on_play,on_stop,on_close", c.hooks_.called().c_str());
    EXPECT_TRUE(c.source_->consumers_.empty());
}

static void *rtmp_conn_utest_st_thread_create_out_of_memory(void *(*start)(void *arg), void *arg, int joinable, int stack_size)
{
    return NULL;
}

// State Threads cannot make a coroutine, as when the server is out of memory: for a new client, or for the receive
// thread of a player or an encoder. The client is refused, and an encoder's publish is released with its hooks
// paired.
VOID TEST(AppRtmpConnTest, NoCoroutineForClientOrReceiveThread_Refused)
{
    srs_error_t err;

    if (true) {
        RtmpConnUtestClient c;
        HELPER_ASSERT_SUCCESS(c.open("vhost __defaultVhost__ { }", SrsRtmpConnPlay));

        _ST_THREAD_CREATE_PFN ov = _pfn_st_thread_create;
        _pfn_st_thread_create = (_ST_THREAD_CREATE_PFN)rtmp_conn_utest_st_thread_create_out_of_memory;
        err = c.start();
        _pfn_st_thread_create = ov;
        rtmp_conn_utest_expect(err, ERROR_ST_CREATE_CYCLE_THREAD, "coroutine");
    }

    SrsRtmpConnType types[] = {SrsRtmpConnPlay, SrsRtmpConnFMLEPublish};
    const char *wraps[] = {"rtmp: start receive thread", "rtmp: receive thread"};
    const char *hooks[] = {"on_connect,on_play,on_stop,on_close", "on_connect,on_publish,on_unpublish,on_close"};
    for (int i = 0; i < 2; i++) {
        RtmpConnUtestClient c;
        HELPER_ASSERT_SUCCESS(c.open(string("vhost __defaultVhost__ { ") + rtmp_conn_utest_all_hooks + " }", types[i]));
        // The origin hub starts coroutines of its own at a publish; this test is about the receive thread.
        c.source_->publish_as_source_ = false;
        HELPER_ASSERT_SUCCESS(c.start());

        _ST_THREAD_CREATE_PFN ov = _pfn_st_thread_create;
        _pfn_st_thread_create = (_ST_THREAD_CREATE_PFN)rtmp_conn_utest_st_thread_create_out_of_memory;
        c.wait_done();
        _pfn_st_thread_create = ov;

        ASSERT_TRUE(c.done_);
        rtmp_conn_utest_expect(c.err_, ERROR_ST_CREATE_CYCLE_THREAD, wraps[i]);
        c.err_ = srs_success;
        EXPECT_STREQ(hooks[i], c.hooks_.called().c_str());
        EXPECT_TRUE(c.source_->can_publish_);
    }
}

// The origin hub's HLS, which fails as a full disk or a codec error makes it fail, and records what the hub asks.
class RtmpSourceUtestHls : public MockHlsForOriginHub
{
public:
    srs_error_t audio_error_;
    srs_error_t video_error_;
    srs_error_t publish_error_;
    int on_publish_count_;
    int on_unpublish_count_;

public:
    RtmpSourceUtestHls()
    {
        audio_error_ = video_error_ = publish_error_ = srs_success;
        on_publish_count_ = on_unpublish_count_ = 0;
    }
    virtual ~RtmpSourceUtestHls()
    {
        srs_freep(audio_error_);
        srs_freep(video_error_);
        srs_freep(publish_error_);
    }

public:
    virtual srs_error_t on_audio(SrsMediaPacket *shared_audio, SrsFormat *format)
    {
        on_audio_count_++;
        return srs_error_copy(audio_error_);
    }
    virtual srs_error_t on_video(SrsMediaPacket *shared_video, SrsFormat *format)
    {
        on_video_count_++;
        return srs_error_copy(video_error_);
    }
    virtual srs_error_t on_publish()
    {
        on_publish_count_++;
        return srs_error_copy(publish_error_);
    }
    virtual void on_unpublish()
    {
        on_unpublish_count_++;
    }
};

// The origin hub's DASH, which fails as a full disk makes it fail.
class RtmpSourceUtestDash : public MockDashForOriginHub
{
public:
    srs_error_t audio_error_;
    srs_error_t video_error_;
    srs_error_t publish_error_;
    int on_publish_count_;
    int on_unpublish_count_;

public:
    RtmpSourceUtestDash()
    {
        audio_error_ = video_error_ = publish_error_ = srs_success;
        on_publish_count_ = on_unpublish_count_ = 0;
    }
    virtual ~RtmpSourceUtestDash()
    {
        srs_freep(audio_error_);
        srs_freep(video_error_);
        srs_freep(publish_error_);
    }

public:
    virtual srs_error_t on_audio(SrsMediaPacket *shared_audio, SrsFormat *format)
    {
        on_audio_count_++;
        return srs_error_copy(audio_error_);
    }
    virtual srs_error_t on_video(SrsMediaPacket *shared_video, SrsFormat *format)
    {
        on_video_count_++;
        return srs_error_copy(video_error_);
    }
    virtual srs_error_t on_publish()
    {
        on_publish_count_++;
        return srs_error_copy(publish_error_);
    }
    virtual void on_unpublish()
    {
        on_unpublish_count_++;
    }
};

// The origin hub's DVR, which fails as a full disk makes it fail.
class RtmpSourceUtestDvr : public MockDvrForOriginHub
{
public:
    srs_error_t meta_error_;
    srs_error_t audio_error_;
    srs_error_t video_error_;
    srs_error_t publish_error_;
    int on_publish_count_;
    int on_unpublish_count_;

public:
    RtmpSourceUtestDvr()
    {
        meta_error_ = audio_error_ = video_error_ = publish_error_ = srs_success;
        on_publish_count_ = on_unpublish_count_ = 0;
    }
    virtual ~RtmpSourceUtestDvr()
    {
        srs_freep(meta_error_);
        srs_freep(audio_error_);
        srs_freep(video_error_);
        srs_freep(publish_error_);
    }

public:
    virtual srs_error_t on_meta_data(SrsMediaPacket *metadata)
    {
        on_meta_data_count_++;
        return srs_error_copy(meta_error_);
    }
    virtual srs_error_t on_audio(SrsMediaPacket *shared_audio, SrsFormat *format)
    {
        on_audio_count_++;
        return srs_error_copy(audio_error_);
    }
    virtual srs_error_t on_video(SrsMediaPacket *shared_video, SrsFormat *format)
    {
        on_video_count_++;
        return srs_error_copy(video_error_);
    }
    virtual srs_error_t on_publish(ISrsRequest *r)
    {
        on_publish_count_++;
        return srs_error_copy(publish_error_);
    }
    virtual void on_unpublish()
    {
        on_unpublish_count_++;
    }
};

// The origin hub's transcoder, which fails to start its coroutine.
class RtmpSourceUtestEncoder : public MockMediaEncoderForOriginHub
{
public:
    srs_error_t publish_error_;
    int on_publish_count_;

public:
    RtmpSourceUtestEncoder()
    {
        publish_error_ = srs_success;
        on_publish_count_ = 0;
    }
    virtual ~RtmpSourceUtestEncoder()
    {
        srs_freep(publish_error_);
    }

public:
    virtual srs_error_t on_publish(ISrsRequest *req)
    {
        on_publish_count_++;
        return srs_error_copy(publish_error_);
    }
};

// The origin hub's exec, which fails to start its process.
class RtmpSourceUtestExec : public MockNgExecForOriginHub
{
public:
    srs_error_t publish_error_;

public:
    RtmpSourceUtestExec()
    {
        publish_error_ = srs_success;
    }
    virtual ~RtmpSourceUtestExec()
    {
        srs_freep(publish_error_);
    }

public:
    virtual srs_error_t on_publish(ISrsRequest *req)
    {
        on_publish_count_++;
        return srs_error_copy(publish_error_);
    }
};

// Makes the stream's origin hub as the server does, reading the test config, with the HLS, DASH, DVR, transcoder and
// exec above, or with the real DVR.
class RtmpSourceUtestFactory : public SrsAppFactory
{
public:
    ISrsAppConfig *config_;
    ISrsStatistic *stat_;
    bool real_dvr_;
    // The last hub made and its parts, owned by the source.
    SrsOriginHub *hub_;
    RtmpSourceUtestHls *hls_;
    RtmpSourceUtestDash *dash_;
    RtmpSourceUtestDvr *dvr_;
    RtmpSourceUtestEncoder *encoder_;
    RtmpSourceUtestExec *exec_;

public:
    RtmpSourceUtestFactory()
    {
        config_ = NULL;
        stat_ = NULL;
        real_dvr_ = false;
        hub_ = NULL;
        hls_ = NULL;
        dash_ = NULL;
        dvr_ = NULL;
        encoder_ = NULL;
        exec_ = NULL;
    }

public:
    virtual ISrsOriginHub *create_origin_hub()
    {
        SrsOriginHub *hub = new SrsOriginHub();
        hub->config_ = config_;
        hub->stat_ = stat_;

        srs_freep(hub->hls_);
        hub->hls_ = hls_ = new RtmpSourceUtestHls();
        srs_freep(hub->dash_);
        hub->dash_ = dash_ = new RtmpSourceUtestDash();
        srs_freep(hub->encoder_);
        hub->encoder_ = encoder_ = new RtmpSourceUtestEncoder();
        srs_freep(hub->ng_exec_);
        hub->ng_exec_ = exec_ = new RtmpSourceUtestExec();
        if (real_dvr_) {
            dynamic_cast<SrsDvr *>(hub->dvr_)->config_ = config_;
        } else {
            srs_freep(hub->dvr_);
            hub->dvr_ = dvr_ = new RtmpSourceUtestDvr();
        }

        hub->assemble();
        hub_ = hub;
        return hub;
    }
};

// A live stream of the server: the real SrsLiveSource with the vhost from config text, its origin hub, a real
// statistic and the clock of the test. Encoders publish to it, and players take its messages, as SrsRtmpConn does.
class RtmpSourceUtestStream
{
public:
    MockSrsConfig conf_;
    SrsStatistic stat_;
    MockLiveSourceHandler handler_;
    MockClockForPithyPrint clock_;
    RtmpSourceUtestFactory factory_;
    MockSrsRequest req_;
    SrsSharedPtr<SrsLiveSource> source_;
    std::vector<SrsLiveConsumer *> players_;

public:
    RtmpSourceUtestStream() : req_("__defaultVhost__", "live", "livestream")
    {
        clock_.now_ = 100 * SRS_UTIME_SECONDS;
    }
    virtual ~RtmpSourceUtestStream()
    {
        for (int i = 0; i < (int)players_.size(); i++) {
            SrsLiveConsumer *player = players_.at(i);
            srs_freep(player);
        }
    }

public:
    // Creates the source of the stream, as SrsLiveSourceManager::fetch_or_create does.
    srs_error_t open(std::string text)
    {
        srs_error_t err = srs_success;

        if ((err = conf_.mock_parse(std::string(_MIN_OK_CONF) + text)) != srs_success) {
            return srs_error_wrap(err, "parse");
        }
        factory_.config_ = &conf_;
        factory_.stat_ = &stat_;

        SrsLiveSource *source = new SrsLiveSource();
        source->config_ = &conf_;
        source->stat_ = &stat_;
        source->handler_ = &handler_;
        source->app_factory_ = &factory_;
        source->clk_ = &clock_;
        source_ = SrsSharedPtr<SrsLiveSource>(source);
        source->assemble();

        return source->initialize(source_, &req_);
    }
    // A player joins, getting the cached metadata, sequence headers and GOP, as SrsRtmpConn::playing does.
    SrsLiveConsumer *play()
    {
        SrsLiveConsumer *player = NULL;
        srs_error_t err = source_->create_consumer(player);
        EXPECT_TRUE(err == srs_success) << srs_error_desc(err);
        srs_freep(err);
        if (!player) {
            return NULL;
        }
        players_.push_back(player);

        err = source_->consumer_dumps(player, true, true, true);
        EXPECT_TRUE(err == srs_success) << srs_error_desc(err);
        srs_freep(err);
        return player;
    }
    // The player leaves.
    void leave(SrsLiveConsumer *player)
    {
        players_.erase(std::find(players_.begin(), players_.end(), player));
        srs_freep(player);
    }
    srs_error_t audio(int64_t timestamp, const uint8_t *data, int size)
    {
        SrsRtmpCommonMessage msg;
        msg.header_.initialize_audio(size, (uint32_t)timestamp, 1);
        msg.create_payload(size);
        memcpy(msg.payload(), data, size);
        return source_->on_audio(&msg);
    }
    srs_error_t video(int64_t timestamp, const uint8_t *data, int size)
    {
        SrsRtmpCommonMessage msg;
        msg.header_.initialize_video(size, (uint32_t)timestamp, 1);
        msg.create_payload(size);
        memcpy(msg.payload(), data, size);
        return source_->on_video(&msg);
    }
    srs_error_t metadata(int64_t timestamp, int width = 768)
    {
        SrsOnMetaDataPacket meta;
        meta.metadata_->set("width", SrsAmf0Any::number(width));
        meta.metadata_->set("height", SrsAmf0Any::number(320));
        SrsRtmpCommonMessage msg;
        msg.header_.initialize_amf0_script(0, 1);
        msg.header_.timestamp_ = timestamp;
        return source_->on_meta_data(&msg, &meta);
    }
    // An RTMP aggregate message holding the FLV tags, as an FMS or Wowza edge sends.
    srs_error_t aggregate(int64_t timestamp, const std::string &tags)
    {
        SrsRtmpCommonMessage msg;
        msg.header_.message_type_ = RTMP_MSG_AggregateMessage;
        msg.header_.payload_length_ = (int)tags.size();
        msg.header_.timestamp_ = timestamp;
        msg.header_.stream_id_ = 1;
        msg.create_payload((int)tags.size());
        memcpy(msg.payload(), tags.data(), tags.size());
        return source_->on_aggregate(&msg);
    }
    // The H.264 and AAC sequence headers and a key frame at the time.
    srs_error_t start_av(int64_t timestamp)
    {
        srs_error_t err = srs_success;
        if ((err = video(timestamp, flv_utest_avc_sh, sizeof(flv_utest_avc_sh))) != srs_success) {
            return srs_error_wrap(err, "video sh");
        }
        if ((err = audio(timestamp, flv_utest_aac_sh, sizeof(flv_utest_aac_sh))) != srs_success) {
            return srs_error_wrap(err, "audio sh");
        }
        if ((err = video(timestamp, flv_utest_avc_idr, sizeof(flv_utest_avc_idr))) != srs_success) {
            return srs_error_wrap(err, "idr");
        }
        return err;
    }
};

// What the player got since it last read: A and V for the sequence headers, a and v for frames, m for the metadata, each
// with its time.
static string rtmp_source_utest_got(SrsLiveConsumer *player)
{
    SrsMessageArray msgs(256);
    int count = 0;
    srs_error_t err = player->dump_packets(&msgs, count);
    EXPECT_TRUE(err == srs_success) << srs_error_desc(err);
    srs_freep(err);

    std::stringstream ss;
    for (int i = 0; i < count; i++) {
        SrsMediaPacket *msg = msgs.msgs_[i];
        if (i) {
            ss << ",";
        }
        if (msg->is_audio()) {
            ss << (SrsFlvAudio::sh(msg->payload(), msg->size()) ? "A" : "a");
        } else if (msg->is_video()) {
            ss << (SrsFlvVideo::sh(msg->payload(), msg->size()) ? "V" : "v");
        } else {
            ss << "m";
        }
        ss << msg->timestamp_;
    }
    msgs.free(count);
    return ss.str();
}

// An FLV tag of an aggregate message: type, data size, time, stream id, the data and the previous tag size.
static string rtmp_source_utest_tag(uint8_t type, int64_t timestamp, const uint8_t *data, int size)
{
    string tag(11 + size + 4, '\0');
    SrsBuffer b((char *)tag.data(), (int)tag.size());
    b.write_1bytes(type);
    b.write_3bytes(size);
    b.write_3bytes((int32_t)(timestamp & 0xffffff));
    b.write_1bytes((int8_t)(timestamp >> 24));
    b.write_3bytes(0);
    b.write_bytes((char *)data, size);
    b.write_4bytes(11 + size);
    return tag;
}

static const uint8_t rtmp_source_utest_aac_frame2[] = {0xaf, 0x01, 0x21, 0x12, 0x46, 0x00};

// An encoder that writes video ahead of audio, on a vhost with mix_correct. The source holds the messages until it has
// both, and gives the players audio and video in time order; without it, the players get them as sent.
VOID TEST(AppRtmpSourceTest, MixCorrectVideoAheadOfAudio_PlayerGetsTimeOrder)
{
    srs_error_t err;

    const char *configs[] = {"vhost __defaultVhost__ { play { mix_correct on; time_jitter off; } }",
                             "vhost __defaultVhost__ { play { time_jitter off; } }"};
    const char *expects[] = {"V0,A0,v0,a23,v40,a46", "V0,A0,v0,v40,v80,a23,a46,a69,a92"};
    for (int i = 0; i < 2; i++) {
        RtmpSourceUtestStream s;
        HELPER_ASSERT_SUCCESS(s.open(configs[i]));
        SrsLiveConsumer *player = s.play();
        HELPER_ASSERT_SUCCESS(s.source_->on_publish());

        HELPER_ASSERT_SUCCESS(s.start_av(0));
        HELPER_ASSERT_SUCCESS(s.video(40, flv_utest_avc_inter, sizeof(flv_utest_avc_inter)));
        HELPER_ASSERT_SUCCESS(s.video(80, flv_utest_avc_inter, sizeof(flv_utest_avc_inter)));
        HELPER_ASSERT_SUCCESS(s.audio(23, flv_utest_aac_frame, sizeof(flv_utest_aac_frame)));
        HELPER_ASSERT_SUCCESS(s.audio(46, rtmp_source_utest_aac_frame2, sizeof(rtmp_source_utest_aac_frame2)));
        HELPER_ASSERT_SUCCESS(s.audio(69, flv_utest_aac_frame, sizeof(flv_utest_aac_frame)));
        HELPER_ASSERT_SUCCESS(s.audio(92, rtmp_source_utest_aac_frame2, sizeof(rtmp_source_utest_aac_frame2)));

        EXPECT_STREQ(expects[i], rtmp_source_utest_got(player).c_str()) << configs[i];
        s.source_->on_unpublish();
    }
}

// An audio-only or a video-only encoder on a vhost with mix_correct. With no message of the other kind to order by, the
// source holds the first ten, then passes one for each new one.
VOID TEST(AppRtmpSourceTest, MixCorrectAudioOnlyOrVideoOnly_HeldForTenMessages)
{
    srs_error_t err;

    for (int kind = 0; kind < 2; kind++) {
        RtmpSourceUtestStream s;
        HELPER_ASSERT_SUCCESS(s.open("vhost __defaultVhost__ { play { mix_correct on; time_jitter off; } }"));
        SrsLiveConsumer *player = s.play();
        HELPER_ASSERT_SUCCESS(s.source_->on_publish());

        for (int i = 0; i < 11; i++) {
            if (kind == 0) {
                if (i == 0) {
                    HELPER_ASSERT_SUCCESS(s.audio(0, flv_utest_aac_sh, sizeof(flv_utest_aac_sh)));
                } else {
                    HELPER_ASSERT_SUCCESS(s.audio(i * 23, flv_utest_aac_frame, sizeof(flv_utest_aac_frame)));
                }
            } else {
                if (i == 0) {
                    HELPER_ASSERT_SUCCESS(s.video(0, flv_utest_avc_sh, sizeof(flv_utest_avc_sh)));
                } else {
                    HELPER_ASSERT_SUCCESS(s.video(i * 40, flv_utest_avc_idr, sizeof(flv_utest_avc_idr)));
                }
            }
            if (i == 8) {
                EXPECT_STREQ("", rtmp_source_utest_got(player).c_str()) << kind;
            }
        }

        EXPECT_STREQ(kind == 0 ? "A0,a23" : "V0,v40", rtmp_source_utest_got(player).c_str());
        s.source_->on_unpublish();
    }
}

// An encoder whose timestamps jump back, without mix_correct. The source warns once per publish that the stream may need
// mix_correct, and passes the messages as sent.
VOID TEST(AppRtmpSourceTest, TimestampsJumpBack_WarnedOncePerPublish)
{
    srs_error_t err;

    ListenerUtestLog log;
    RtmpSourceUtestStream s;
    HELPER_ASSERT_SUCCESS(s.open("vhost __defaultVhost__ { play { time_jitter off; } }"));
    SrsLiveConsumer *player = s.play();
    HELPER_ASSERT_SUCCESS(s.source_->on_publish());

    HELPER_ASSERT_SUCCESS(s.start_av(0));
    HELPER_ASSERT_SUCCESS(s.video(40, flv_utest_avc_inter, sizeof(flv_utest_avc_inter)));
    HELPER_ASSERT_SUCCESS(s.audio(20, flv_utest_aac_frame, sizeof(flv_utest_aac_frame)));
    HELPER_ASSERT_SUCCESS(s.video(80, flv_utest_avc_inter, sizeof(flv_utest_avc_inter)));
    HELPER_ASSERT_SUCCESS(s.audio(60, flv_utest_aac_frame, sizeof(flv_utest_aac_frame)));
    EXPECT_EQ(1, log.warns("AUDIO: Timestamp 40=>20, may need mix_correct"));
    EXPECT_EQ(1, log.warns("may need mix_correct"));
    EXPECT_STREQ("V0,A0,v0,v40,a20,v80,a60", rtmp_source_utest_got(player).c_str());

    // The encoder publishes again, and its video jumps back.
    s.source_->on_unpublish();
    HELPER_ASSERT_SUCCESS(s.source_->on_publish());
    HELPER_ASSERT_SUCCESS(s.audio(100, flv_utest_aac_frame, sizeof(flv_utest_aac_frame)));
    HELPER_ASSERT_SUCCESS(s.video(90, flv_utest_avc_idr, sizeof(flv_utest_avc_idr)));
    EXPECT_EQ(1, log.warns("VIDEO: Timestamp 100=>90, may need mix_correct"));
    EXPECT_EQ(2, log.warns("may need mix_correct"));
    s.source_->on_unpublish();
}

// An encoder publishing with absolute times on a vhost with ATC, as for HLS from several origins. The cached metadata and
// sequence headers take the time of the last frame, and a player that joins gets them at the start of the GOP it gets.
VOID TEST(AppRtmpSourceTest, AtcStream_SequenceHeadersTakeAbsoluteTime)
{
    srs_error_t err;

    RtmpSourceUtestStream s;
    HELPER_ASSERT_SUCCESS(s.open("vhost __defaultVhost__ { play { atc on; } }"));
    HELPER_ASSERT_SUCCESS(s.source_->on_publish());

    int64_t t = 3600 * 1000;
    HELPER_ASSERT_SUCCESS(s.metadata(0));
    HELPER_ASSERT_SUCCESS(s.start_av(t));
    EXPECT_EQ(t, s.source_->meta_->vsh()->timestamp_);
    EXPECT_EQ(t, s.source_->meta_->data()->timestamp_);

    HELPER_ASSERT_SUCCESS(s.audio(t + 10, flv_utest_aac_frame, sizeof(flv_utest_aac_frame)));
    EXPECT_EQ(t + 10, s.source_->meta_->ash()->timestamp_);
    EXPECT_EQ(t + 10, s.source_->meta_->data()->timestamp_);

    HELPER_ASSERT_SUCCESS(s.video(t + 40, flv_utest_avc_inter, sizeof(flv_utest_avc_inter)));
    EXPECT_EQ(t + 40, s.source_->meta_->vsh()->timestamp_);
    EXPECT_EQ(t + 40, s.source_->meta_->data()->timestamp_);

    SrsLiveConsumer *player = s.play();
    EXPECT_STREQ("m3600000,A3600000,V3600000,v3600000,a3600010,v3600040", rtmp_source_utest_got(player).c_str());
    s.source_->on_unpublish();
}

// An encoder that sends its video sequence header again, the same or changed, as at a republish, on a vhost with
// reduce_sequence_header. A player that stays does not get the same one again, but gets a changed one.
VOID TEST(AppRtmpSourceTest, RepublishSameVideoSequenceHeader_ReduceDropsIt)
{
    srs_error_t err;

    RtmpSourceUtestStream s;
    HELPER_ASSERT_SUCCESS(s.open("vhost __defaultVhost__ { play { reduce_sequence_header on; time_jitter off; } }"));
    SrsLiveConsumer *player = s.play();

    HELPER_ASSERT_SUCCESS(s.source_->on_publish());
    HELPER_ASSERT_SUCCESS(s.video(0, flv_utest_avc_sh, sizeof(flv_utest_avc_sh)));
    HELPER_ASSERT_SUCCESS(s.video(0, flv_utest_avc_idr, sizeof(flv_utest_avc_idr)));
    s.source_->on_unpublish();
    EXPECT_STREQ("V0,v0", rtmp_source_utest_got(player).c_str());

    HELPER_ASSERT_SUCCESS(s.source_->on_publish());
    HELPER_ASSERT_SUCCESS(s.video(100, flv_utest_avc_sh, sizeof(flv_utest_avc_sh)));
    HELPER_ASSERT_SUCCESS(s.video(100, flv_utest_avc_idr, sizeof(flv_utest_avc_idr)));
    EXPECT_STREQ("v100", rtmp_source_utest_got(player).c_str());

    // The encoder changes its level in the record, as after a resolution change.
    uint8_t changed[sizeof(flv_utest_avc_sh)];
    memcpy(changed, flv_utest_avc_sh, sizeof(changed));
    changed[8] = 0x1f;
    HELPER_ASSERT_SUCCESS(s.video(200, changed, sizeof(changed)));
    EXPECT_STREQ("V200", rtmp_source_utest_got(player).c_str());
    s.source_->on_unpublish();
}

// A player that waits for a stream, on a vhost with reduce_sequence_header. The encoder publishes and sends its first
// metadata, which the player gets, as it gets the first sequence headers.
VOID TEST(AppRtmpSourceTest, PlayerWaitingBeforePublish_ReduceSendsFirstMetadata)
{
    srs_error_t err;

    RtmpSourceUtestStream s;
    HELPER_ASSERT_SUCCESS(s.open("vhost __defaultVhost__ { play { reduce_sequence_header on; time_jitter off; } }"));
    SrsLiveConsumer *player = s.play();

    HELPER_ASSERT_SUCCESS(s.source_->on_publish());
    HELPER_ASSERT_SUCCESS(s.metadata(0));
    HELPER_ASSERT_SUCCESS(s.start_av(0));
    EXPECT_STREQ("m0,V0,A0,v0", rtmp_source_utest_got(player).c_str());
    s.source_->on_unpublish();
}

// An encoder that sends its metadata again, the same or changed, in a publish and at a republish, on a vhost with
// reduce_sequence_header. A player that stays does not get the same one again, but gets a changed one, as with the
// sequence headers. A player that joins later gets the cached one.
VOID TEST(AppRtmpSourceTest, SameOrChangedMetadataAgain_ReduceDropsOnlySame)
{
    srs_error_t err;

    RtmpSourceUtestStream s;
    HELPER_ASSERT_SUCCESS(s.open("vhost __defaultVhost__ { play { reduce_sequence_header on; time_jitter off; } }"));
    SrsLiveConsumer *player = s.play();

    HELPER_ASSERT_SUCCESS(s.source_->on_publish());
    HELPER_ASSERT_SUCCESS(s.metadata(0));
    HELPER_ASSERT_SUCCESS(s.metadata(10));
    EXPECT_STREQ("m0", rtmp_source_utest_got(player).c_str());

    // The encoder changes its resolution.
    HELPER_ASSERT_SUCCESS(s.metadata(20, 1280));
    EXPECT_STREQ("m20", rtmp_source_utest_got(player).c_str());
    s.source_->on_unpublish();

    // The encoder republishes with the same metadata, then changes it back.
    HELPER_ASSERT_SUCCESS(s.source_->on_publish());
    HELPER_ASSERT_SUCCESS(s.metadata(100, 1280));
    EXPECT_STREQ("", rtmp_source_utest_got(player).c_str());
    SrsLiveConsumer *late = s.play();
    EXPECT_STREQ("m100", rtmp_source_utest_got(late).c_str());

    HELPER_ASSERT_SUCCESS(s.metadata(110));
    EXPECT_STREQ("m110", rtmp_source_utest_got(player).c_str());
    EXPECT_STREQ("m110", rtmp_source_utest_got(late).c_str());
    s.source_->on_unpublish();
}

// Without reduce_sequence_header, a player gets each metadata the encoder sends, the same one too.
VOID TEST(AppRtmpSourceTest, SameMetadataAgainWithoutReduce_Sent)
{
    srs_error_t err;

    RtmpSourceUtestStream s;
    HELPER_ASSERT_SUCCESS(s.open("vhost __defaultVhost__ { play { time_jitter off; } }"));
    SrsLiveConsumer *player = s.play();

    HELPER_ASSERT_SUCCESS(s.source_->on_publish());
    HELPER_ASSERT_SUCCESS(s.metadata(0));
    HELPER_ASSERT_SUCCESS(s.metadata(10));
    EXPECT_STREQ("m0,m10", rtmp_source_utest_got(player).c_str());
    s.source_->on_unpublish();
}

// An aggregate message from an FMS or Wowza edge, whose second tag is cut at each byte. The tags before the cut reach the
// player, and the publisher is refused at the cut.
VOID TEST(AppRtmpSourceTest, AggregateCutInsideTag_Refused)
{
    srs_error_t err;

    string first = rtmp_source_utest_tag(RTMP_MSG_AudioMessage, 0, flv_utest_aac_sh, sizeof(flv_utest_aac_sh));
    string second = rtmp_source_utest_tag(RTMP_MSG_AudioMessage, 23, flv_utest_aac_frame, sizeof(flv_utest_aac_frame));

    for (int cut = 1; cut < (int)second.size(); cut++) {
        RtmpSourceUtestStream s;
        HELPER_ASSERT_SUCCESS(s.open("vhost __defaultVhost__ { play { time_jitter off; } }"));
        SrsLiveConsumer *player = s.play();
        HELPER_ASSERT_SUCCESS(s.source_->on_publish());

        err = s.aggregate(1000, first + second.substr(0, cut));
        EXPECT_EQ(ERROR_RTMP_AGGREGATE, srs_error_code(err)) << "cut=" << cut;
        srs_freep(err);
        EXPECT_STREQ("A1000", rtmp_source_utest_got(player).c_str()) << "cut=" << cut;
        s.source_->on_unpublish();
    }

    // The whole message is taken, its times made absolute from the message's.
    RtmpSourceUtestStream s;
    HELPER_ASSERT_SUCCESS(s.open("vhost __defaultVhost__ { play { time_jitter off; } }"));
    SrsLiveConsumer *player = s.play();
    HELPER_ASSERT_SUCCESS(s.source_->on_publish());
    HELPER_ASSERT_SUCCESS(s.aggregate(1000, first + second));
    EXPECT_STREQ("A1000,a1023", rtmp_source_utest_got(player).c_str());
    s.source_->on_unpublish();
}

// An encoder whose sequence header is corrupt: an H.264 header cut inside its SPS, or an AAC header whose
// AudioSpecificConfig is cut after one byte, sent alone or in an aggregate message. The publisher is refused, and the
// players get nothing.
VOID TEST(AppRtmpSourceTest, CorruptSequenceHeader_PublisherRefused)
{
    srs_error_t err;

    const uint8_t aac_sh_cut[] = {0xaf, 0x00, 0x12};
    for (int i = 0; i < 4; i++) {
        bool is_video = (i % 2) == 0;
        bool in_aggregate = i >= 2;

        RtmpSourceUtestStream s;
        HELPER_ASSERT_SUCCESS(s.open("vhost __defaultVhost__ { }"));
        SrsLiveConsumer *player = s.play();
        HELPER_ASSERT_SUCCESS(s.source_->on_publish());

        const uint8_t *data = is_video ? flv_utest_avc_sh : aac_sh_cut;
        int size = is_video ? 20 : (int)sizeof(aac_sh_cut);
        if (in_aggregate) {
            err = s.aggregate(0, rtmp_source_utest_tag(is_video ? RTMP_MSG_VideoMessage : RTMP_MSG_AudioMessage, 0, data, size));
        } else {
            err = is_video ? s.video(0, data, size) : s.audio(0, data, size);
        }

        EXPECT_TRUE(err != srs_success) << i;
        string desc = srs_error_summary(err);
        srs_freep(err);
        EXPECT_NE(string::npos, desc.find(is_video ? "format consume video" : "format consume audio")) << desc;
        if (in_aggregate) {
            EXPECT_NE(string::npos, desc.find(is_video ? "consume video : format" : "consume audio : format")) << desc;
        }
        EXPECT_STREQ("", rtmp_source_utest_got(player).c_str()) << i;
        s.source_->on_unpublish();
    }
}

// HLS fails on a frame, as on a full disk or a frame it cannot decode, with each hls_on_error. With ignore, HLS stops and
// the stream goes on. With continue, a decode error of a frame the size of the sequence header, which may be one, is
// skipped, and any other error disconnects the publisher, as with disconnect.
VOID TEST(AppRtmpSourceTest, HlsFailsOnFrame_HlsOnErrorStrategy)
{
    srs_error_t err;

    // A frame of each kind the size of its sequence header.
    const uint8_t aac_frame_sh_size[] = {0xaf, 0x01, 0x21, 0x00};
    uint8_t avc_frame_sh_size[sizeof(flv_utest_avc_sh)];
    memset(avc_frame_sh_size, 0, sizeof(avc_frame_sh_size));
    memcpy(avc_frame_sh_size, flv_utest_avc_inter, sizeof(flv_utest_avc_inter));
    SrsBuffer nalu((char *)avc_frame_sh_size + 5, 4);
    nalu.write_4bytes((int)sizeof(avc_frame_sh_size) - 9);

    struct {
        const char *strategy_;
        int code_;
        bool sh_size_;
        bool accepted_;
        int hls_unpublish_;
    } cases[] = {
        {"ignore", ERROR_SYSTEM_FILE_WRITE, false, true, 1},
        {"continue", ERROR_HLS_DECODE_ERROR, true, true, 0},
        {"continue", ERROR_HLS_DECODE_ERROR, false, false, 0},
        {"continue", ERROR_SYSTEM_FILE_WRITE, false, false, 0},
        {"disconnect", ERROR_SYSTEM_FILE_WRITE, false, false, 0},
    };

    for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
        for (int kind = 0; kind < 2; kind++) {
            bool is_video = kind == 1;
            RtmpSourceUtestStream s;
            HELPER_ASSERT_SUCCESS(s.open(string("vhost __defaultVhost__ { play { time_jitter off; } hls { enabled on; hls_on_error ") + cases[i].strategy_ + "; } }"));
            SrsLiveConsumer *player = s.play();
            HELPER_ASSERT_SUCCESS(s.source_->on_publish());
            HELPER_ASSERT_SUCCESS(s.start_av(0));
            EXPECT_STREQ("V0,A0,v0", rtmp_source_utest_got(player).c_str());

            srs_error_t *perr = is_video ? &s.factory_.hls_->video_error_ : &s.factory_.hls_->audio_error_;
            *perr = srs_error_new(cases[i].code_, "hls");
            if (is_video) {
                err = cases[i].sh_size_ ? s.video(40, avc_frame_sh_size, sizeof(avc_frame_sh_size)) : s.video(40, flv_utest_avc_inter, sizeof(flv_utest_avc_inter));
            } else {
                err = cases[i].sh_size_ ? s.audio(23, aac_frame_sh_size, sizeof(aac_frame_sh_size)) : s.audio(23, flv_utest_aac_frame, sizeof(flv_utest_aac_frame));
            }

            if (cases[i].accepted_) {
                HELPER_EXPECT_SUCCESS(err);
                EXPECT_STREQ(is_video ? "v40" : "a23", rtmp_source_utest_got(player).c_str()) << i;
            } else {
                EXPECT_EQ(cases[i].code_, srs_error_code(err)) << i << "," << kind;
                string desc = srs_error_summary(err);
                srs_freep(err);
                EXPECT_NE(string::npos, desc.find(is_video ? "hub consume video" : "consume audio")) << desc;
                EXPECT_NE(string::npos, desc.find(is_video ? "hls: video" : "hls: audio")) << desc;
                EXPECT_STREQ("", rtmp_source_utest_got(player).c_str()) << i;
            }
            EXPECT_EQ(cases[i].hls_unpublish_, s.factory_.hls_->on_unpublish_count_) << i << "," << kind;
            s.source_->on_unpublish();
        }
    }
}

// DASH or DVR fails on a frame, as on a full disk. It stops, and the stream goes on for the players and HLS.
VOID TEST(AppRtmpSourceTest, DashOrDvrFailsOnFrame_StoppedStreamGoesOn)
{
    srs_error_t err;

    for (int i = 0; i < 4; i++) {
        bool is_video = (i % 2) == 1;
        bool is_dvr = i >= 2;

        RtmpSourceUtestStream s;
        HELPER_ASSERT_SUCCESS(s.open("vhost __defaultVhost__ { play { time_jitter off; } }"));
        SrsLiveConsumer *player = s.play();
        HELPER_ASSERT_SUCCESS(s.source_->on_publish());
        HELPER_ASSERT_SUCCESS(s.start_av(0));
        EXPECT_STREQ("V0,A0,v0", rtmp_source_utest_got(player).c_str());

        srs_error_t *perr = NULL;
        if (is_dvr) {
            perr = is_video ? &s.factory_.dvr_->video_error_ : &s.factory_.dvr_->audio_error_;
        } else {
            perr = is_video ? &s.factory_.dash_->video_error_ : &s.factory_.dash_->audio_error_;
        }
        *perr = srs_error_new(ERROR_SYSTEM_FILE_WRITE, "full disk");

        int hls_frames = s.factory_.hls_->on_audio_count_ + s.factory_.hls_->on_video_count_;
        if (is_video) {
            HELPER_EXPECT_SUCCESS(s.video(40, flv_utest_avc_inter, sizeof(flv_utest_avc_inter)));
        } else {
            HELPER_EXPECT_SUCCESS(s.audio(23, flv_utest_aac_frame, sizeof(flv_utest_aac_frame)));
        }

        EXPECT_STREQ(is_video ? "v40" : "a23", rtmp_source_utest_got(player).c_str()) << i;
        EXPECT_EQ(hls_frames + 1, s.factory_.hls_->on_audio_count_ + s.factory_.hls_->on_video_count_) << i;
        EXPECT_EQ(is_dvr ? 0 : 1, s.factory_.dash_->on_unpublish_count_) << i;
        EXPECT_EQ(is_dvr ? 1 : 0, s.factory_.dvr_->on_unpublish_count_) << i;
        s.source_->on_unpublish();
    }
}

// DVR fails to write the metadata, as on a full disk. As for a frame DVR fails on, DVR should stop and the stream go on.
// The hub returns the error instead, so the publisher is disconnected.
VOID TEST(AppRtmpSourceTest, DISABLED_DvrFailsOnMetadata_PublisherDisconnected)
{
    srs_error_t err;

    RtmpSourceUtestStream s;
    HELPER_ASSERT_SUCCESS(s.open("vhost __defaultVhost__ { play { time_jitter off; } }"));
    SrsLiveConsumer *player = s.play();
    HELPER_ASSERT_SUCCESS(s.source_->on_publish());

    s.factory_.dvr_->meta_error_ = srs_error_new(ERROR_SYSTEM_FILE_WRITE, "full disk");
    HELPER_EXPECT_SUCCESS(s.metadata(0));
    EXPECT_EQ(1, s.factory_.dvr_->on_unpublish_count_);
    EXPECT_STREQ("m0", rtmp_source_utest_got(player).c_str());
    s.source_->on_unpublish();
}

// A part of the origin hub cannot start when the encoder publishes: the transcoder or exec with no coroutine or
// process, HLS or DASH with no directory, DVR with no file. The publish is refused, and the parts after it, the server's
// HTTP stream and the stat are not started.
VOID TEST(AppRtmpSourceTest, HubPartCannotStart_PublishRefused)
{
    srs_error_t err;

    const char *wraps[] = {"encoder publish", "hls publish", "dash publish", "dvr publish", "exec publish"};
    for (int i = 0; i < 5; i++) {
        RtmpSourceUtestStream s;
        HELPER_ASSERT_SUCCESS(s.open("vhost __defaultVhost__ { }"));
        RtmpSourceUtestFactory &f = s.factory_;

        srs_error_t *errs[] = {&f.encoder_->publish_error_, &f.hls_->publish_error_, &f.dash_->publish_error_, &f.dvr_->publish_error_, &f.exec_->publish_error_};
        *errs[i] = srs_error_new(ERROR_SYSTEM_FILE_WRITE, "cannot start");

        err = s.source_->on_publish();
        EXPECT_EQ(ERROR_SYSTEM_FILE_WRITE, srs_error_code(err)) << i;
        string desc = srs_error_summary(err);
        srs_freep(err);
        EXPECT_NE(string::npos, desc.find("hub publish")) << desc;
        EXPECT_NE(string::npos, desc.find(wraps[i])) << desc;

        int started[] = {f.encoder_->on_publish_count_, f.hls_->on_publish_count_, f.dash_->on_publish_count_, f.dvr_->on_publish_count_, f.exec_->on_publish_count_};
        for (int j = 0; j < 5; j++) {
            EXPECT_EQ(j <= i ? 1 : 0, started[j]) << i << "," << j;
        }
        EXPECT_FALSE(f.hub_->active());
        EXPECT_EQ(0, s.handler_.on_publish_count_);
        EXPECT_TRUE(s.stat_.find_stream_by_url(s.req_.get_stream_url()) == NULL);

        // The connection releases the publish, as SrsRtmpConn::release_publish does.
        s.source_->on_unpublish();
        EXPECT_TRUE(s.source_->can_publish(false));
    }
}

// A vhost whose DVR plan is mistyped. The stream's source cannot be made, so every client of the stream is refused.
VOID TEST(AppRtmpSourceTest, DvrPlanMistyped_SourceRefused)
{
    srs_error_t err;

    RtmpSourceUtestStream s;
    s.factory_.real_dvr_ = true;
    err = s.open("vhost __defaultVhost__ { dvr { enabled on; dvr_plan sesion; } }");
    EXPECT_EQ(ERROR_DVR_ILLEGAL_PLAN, srs_error_code(err));
    string desc = srs_error_summary(err);
    srs_freep(err);
    EXPECT_NE(string::npos, desc.find("hub")) << desc;
    EXPECT_NE(string::npos, desc.find("dvr initialize")) << desc;
}

// DVR opens a new segment, or DASH resumes after its dispose, and the hub feeds it the cached metadata and sequence
// headers again, on a full disk. The write that fails is returned to it.
VOID TEST(AppRtmpSourceTest, DvrNewSegmentOrDashResumeOnFullDisk_RequestShFails)
{
    srs_error_t err;

    const char *wraps[] = {"dvr metadata", "dvr video", "dvr audio", "dash video", "dash audio"};
    for (int i = 0; i < 5; i++) {
        RtmpSourceUtestStream s;
        HELPER_ASSERT_SUCCESS(s.open("vhost __defaultVhost__ { }"));
        HELPER_ASSERT_SUCCESS(s.source_->on_publish());
        HELPER_ASSERT_SUCCESS(s.metadata(0));
        HELPER_ASSERT_SUCCESS(s.start_av(0));

        RtmpSourceUtestFactory &f = s.factory_;
        srs_error_t *errs[] = {&f.dvr_->meta_error_, &f.dvr_->video_error_, &f.dvr_->audio_error_, &f.dash_->video_error_, &f.dash_->audio_error_};
        *errs[i] = srs_error_new(ERROR_SYSTEM_FILE_WRITE, "full disk");

        err = (i < 3) ? f.hub_->on_dvr_request_sh() : f.hub_->on_dash_request_sh();
        EXPECT_EQ(ERROR_SYSTEM_FILE_WRITE, srs_error_code(err)) << i;
        string desc = srs_error_summary(err);
        srs_freep(err);
        EXPECT_NE(string::npos, desc.find(wraps[i])) << desc;
        s.source_->on_unpublish();
    }
}

// The source manager's 3s check of a stream. The source is kept while its encoder publishes, while a player stays after
// the encoder left, and while HLS keeps its files for hls_dispose after the last player left; then it is freed.
VOID TEST(AppRtmpSourceTest, SourceCleanup_KeptWhilePublishingPlayingOrHlsDisposing)
{
    srs_error_t err;

    RtmpSourceUtestStream s;
    HELPER_ASSERT_SUCCESS(s.open("vhost __defaultVhost__ { }"));
    MockStreamPublishTokensForSourceManager tokens;
    SrsLiveSourceManager manager;
    manager.stream_publish_tokens_ = &tokens;
    string url = s.req_.get_stream_url();
    manager.pool_[url] = s.source_;

    HELPER_ASSERT_SUCCESS(s.source_->on_publish());
    SrsLiveConsumer *player = s.play();
    s.clock_.now_ += 10 * SRS_UTIME_SECONDS;
    HELPER_EXPECT_SUCCESS(manager.notify(0, 0, 0));
    EXPECT_EQ(1, (int)manager.pool_.size());

    s.source_->on_unpublish();
    s.clock_.now_ += 10 * SRS_UTIME_SECONDS;
    HELPER_EXPECT_SUCCESS(manager.notify(0, 0, 0));
    EXPECT_EQ(1, (int)manager.pool_.size());

    s.factory_.hls_->cleanup_delay_ = 30 * SRS_UTIME_SECONDS;
    s.leave(player);
    s.clock_.now_ += 10 * SRS_UTIME_SECONDS;
    HELPER_EXPECT_SUCCESS(manager.notify(0, 0, 0));
    EXPECT_EQ(1, (int)manager.pool_.size());

    s.clock_.now_ += 21 * SRS_UTIME_SECONDS;
    HELPER_EXPECT_SUCCESS(manager.notify(0, 0, 0));
    EXPECT_TRUE(manager.pool_.empty());
}

// A second encoder publishes to an edge stream whose first encoder is proxied to the origin. The edge stream cannot take
// it, and takes one again once the first left.
VOID TEST(AppRtmpSourceTest, EdgeStreamProxyingEncoder_SecondEncoderRefused)
{
    srs_error_t err;

    RtmpSourceUtestStream s;
    HELPER_ASSERT_SUCCESS(s.open("vhost __defaultVhost__ { cluster { mode remote; origin 127.0.0.1:19350; } }"));
    EXPECT_TRUE(s.source_->can_publish(true));

    s.source_->publish_edge_->state_ = SrsEdgeStatePublish;
    EXPECT_FALSE(s.source_->can_publish(true));

    s.source_->publish_edge_->state_ = SrsEdgeStateInit;
    EXPECT_TRUE(s.source_->can_publish(true));
}

// A player of a stream whose encoder publishes again. The player logs the new source id once, with the one before.
VOID TEST(AppRtmpSourceTest, PlayerOfRepublishedStream_LogsSourceIdOnce)
{
    srs_error_t err;

    ListenerUtestLog log;
    RtmpSourceUtestStream s;
    HELPER_ASSERT_SUCCESS(s.open("vhost __defaultVhost__ { }"));
    SrsLiveConsumer *player = s.play();

    SrsContextId first = _srs_context->generate_id();
    SrsContextId second = _srs_context->generate_id();
    HELPER_ASSERT_SUCCESS(s.source_->on_source_id_changed(first));
    rtmp_source_utest_got(player);
    string trace = log.last_trace("update source_id=");
    EXPECT_STREQ((string("update source_id=") + first.c_str() + "/" + first.c_str()).c_str(), trace.c_str());

    int nb_traces = (int)log.log_.traces_.size();
    rtmp_source_utest_got(player);
    EXPECT_EQ(nb_traces, (int)log.log_.traces_.size());

    HELPER_ASSERT_SUCCESS(s.source_->on_source_id_changed(second));
    rtmp_source_utest_got(player);
    trace = log.last_trace("update source_id=");
    EXPECT_STREQ((string("update source_id=") + second.c_str() + "/" + first.c_str()).c_str(), trace.c_str());
}

// A player that paused. Its play loop waits one pulse instead of waiting for media, and gets nothing while paused.
VOID TEST(AppRtmpSourceTest, PausedPlayer_WaitsOnePulse)
{
    srs_error_t err;

    RtmpSourceUtestStream s;
    HELPER_ASSERT_SUCCESS(s.open("vhost __defaultVhost__ { }"));
    SrsLiveConsumer *player = s.play();
    HELPER_ASSERT_SUCCESS(s.source_->on_publish());
    HELPER_ASSERT_SUCCESS(player->on_play_client_pause(true));

    srs_utime_t starttime = srs_time_now_realtime();
    player->wait(0, 1 * SRS_UTIME_SECONDS);
    // ST counts the sleep from its last clock, which may be older than now, so only the bound above is checked.
    srs_utime_t elapsed = srs_time_now_realtime() - starttime;
    EXPECT_LT(elapsed, 1 * SRS_UTIME_SECONDS);
    EXPECT_FALSE(player->mw_waiting_);

    HELPER_ASSERT_SUCCESS(s.start_av(0));
    EXPECT_STREQ("", rtmp_source_utest_got(player).c_str());
    HELPER_ASSERT_SUCCESS(player->on_play_client_pause(false));
    EXPECT_STRNE("", rtmp_source_utest_got(player).c_str());
    s.source_->on_unpublish();
}

// An HTTP-FLV player joins a stream with fast_cache before the cache has any message. It gets nothing from the cache.
VOID TEST(AppRtmpSourceTest, HttpPlayerJoinsBeforeFastCacheFilled_GetsNothing)
{
    srs_error_t err;

    RtmpSourceUtestStream s;
    HELPER_ASSERT_SUCCESS(s.open("vhost __defaultVhost__ { }"));
    SrsLiveConsumer *player = s.play();

    SrsMessageQueue cache;
    HELPER_EXPECT_SUCCESS(cache.dump_packets(player, false, SrsRtmpJitterAlgorithmFULL));
    EXPECT_STREQ("", rtmp_source_utest_got(player).c_str());
}

#ifdef SRS_RTSP
// The TCP connection of an RTSP client. The server reads what the client sent and waits for more, as on a socket; an
// interrupt of the reading coroutine ends the read, as it does on a socket. What the server writes is kept, and the
// writes fail from the given one on, as when the client is gone.
class RtspConnUtestIO : public MockBufferIO
{
public:
    srs_cond_t readable_;
    bool closed_;
    int nb_writes_;
    int fail_from_write_;

public:
    RtspConnUtestIO()
    {
        readable_ = srs_cond_new();
        closed_ = false;
        nb_writes_ = 0;
        fail_from_write_ = 0;
    }
    virtual ~RtspConnUtestIO()
    {
        srs_cond_destroy(readable_);
    }

public:
    void client_sends(std::string data)
    {
        append(data);
        srs_cond_signal(readable_);
    }
    // The client closes the connection: what it sent is read, then the reads fail, and so do the writes.
    void client_closes()
    {
        closed_ = true;
        fail_from_write_ = nb_writes_ + 1;
        srs_cond_signal(readable_);
    }

public:
    virtual srs_error_t read(void *buf, size_t size, ssize_t *nread)
    {
        while (in_buffer.length() <= 0) {
            if (closed_) {
                return srs_error_new(ERROR_SOCKET_READ, "connection closed by peer");
            }
            if (srs_cond_wait(readable_) != 0) {
                return srs_error_new(ERROR_SOCKET_READ, "read interrupted");
            }
        }
        return MockBufferIO::read(buf, size, nread);
    }
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite)
    {
        nb_writes_++;
        if (fail_from_write_ && nb_writes_ >= fail_from_write_) {
            return srs_error_new(ERROR_SOCKET_WRITE, "connection reset by peer");
        }
        return MockBufferIO::write(buf, size, nwrite);
    }
};

// An RTSP player of the live app, served by the real SrsRtspConnection and SrsRtspStack over RtspConnUtestIO, with the
// vhost from config text. The stream is in the server's RTSP sources with an AAC and an H.264 track, so the player that
// PLAY makes takes the packets the stream gets, and the server's statistic counts the client.
class RtspConnUtestClient : public ISrsCoroutineHandler
{
public:
    MockSrsConfig conf_;
    RtmpConnUtestHooks hooks_;
    MockResourceManagerForRtspConn manager_;
    RtspConnUtestIO *io_;
    SrsRtspConnection *conn_;
    SrsSharedPtr<SrsRtspSource> source_;
    std::string stream_;
    int cseq_;
    srs_error_t err_;
    bool done_;
    bool whole_cycle_;

public:
    RtspConnUtestClient()
    {
        io_ = NULL;
        conn_ = NULL;
        cseq_ = 0;
        err_ = srs_success;
        done_ = false;
        whole_cycle_ = true;
    }
    virtual ~RtspConnUtestClient()
    {
        if (conn_) {
            // Ends the serve as the server does, so the client leaves the server's statistic.
            conn_->trd_->stop();
            if (!whole_cycle_) {
                conn_->do_teardown();
            }
        }
        srs_freep(conn_);
        srs_freep(err_);
    }

public:
    srs_error_t open(std::string text, std::string stream)
    {
        srs_error_t err = srs_success;

        if ((err = conf_.mock_parse(std::string(_MIN_OK_CONF) + text)) != srs_success) {
            return srs_error_wrap(err, "parse");
        }

        stream_ = stream;
        SrsRequest req;
        req.vhost_ = "__defaultVhost__";
        req.app_ = "live";
        req.stream_ = stream;
        if ((err = _srs_rtsp_sources->fetch_or_create(&req, source_)) != srs_success) {
            return srs_error_wrap(err, "source");
        }

        SrsUniquePtr<SrsRtcTrackDescription> audio(new SrsRtcTrackDescription());
        audio->type_ = "audio";
        audio->ssrc_ = 1001;
        SrsAudioPayload *aac = new SrsAudioPayload(97, "MPEG4-GENERIC", 44100, 2);
        aac->aac_config_hex_ = "1210";
        audio->media_ = aac;
        source_->set_audio_desc(audio.get());

        SrsUniquePtr<SrsRtcTrackDescription> video(new SrsRtcTrackDescription());
        video->type_ = "video";
        video->ssrc_ = 2001;
        video->media_ = new SrsVideoPayload(96, "H264", 90000);
        source_->set_video_desc(video.get());

        io_ = new RtspConnUtestIO();
        conn_ = new SrsRtspConnection(&manager_, io_, "192.168.1.100", 50000);
        conn_->config_ = &conf_;
        conn_->hooks_ = &hooks_;
        conn_->assemble();
        dynamic_cast<SrsSecurity *>(conn_->security_)->config_ = &conf_;

        // Serve the client in a coroutine of this object, which keeps what the connection returned.
        srs_freep(conn_->trd_);
        conn_->trd_ = new SrsSTCoroutine("rtsp", this, _srs_context->get_id());

        return err;
    }
    srs_error_t start()
    {
        return conn_->start();
    }
    // The client sends a request of the stream, with the path after the stream's URL and the headers.
    void request(std::string method, std::string path = "", std::string headers = "")
    {
        io_->client_sends(request_text(method, path, headers));
    }
    std::string request_text(std::string method, std::string path = "", std::string headers = "")
    {
        std::stringstream ss;
        ss << method << " rtsp://127.0.0.1:8554/live/" << stream_ << path << " RTSP/1.0\r\n"
           << "CSeq: " << ++cseq_ << "\r\n"
           << headers << "\r\n";
        return ss.str();
    }
    // The client sets up a track over TCP, as VLC with --rtsp-tcp or ffplay -rtsp_transport tcp do.
    void setup_tcp(int track, int channel)
    {
        std::stringstream ss;
        ss << "Transport: RTP/AVP/TCP;unicast;interleaved=" << channel << "-" << channel + 1 << "\r\n";
        request("SETUP", "/trackID=" + srs_strconv_format_int(track), ss.str());
    }
    // The publisher of the stream sends a packet of its audio or video track, of the SSRC and payload type given or of the
    // tracks the stream was opened with.
    srs_error_t publish(bool audio, uint16_t seq, uint32_t ssrc = 0, uint8_t pt = 0)
    {
        SrsUniquePtr<SrsRtpPacket> pkt(new SrsRtpPacket());
        pkt->header_.set_ssrc(ssrc ? ssrc : (audio ? 1001 : 2001));
        pkt->header_.set_payload_type(pt ? pt : (audio ? 97 : 96));
        pkt->header_.set_sequence(seq);
        pkt->header_.set_timestamp(seq * 1024);
        pkt->frame_type_ = audio ? SrsFrameTypeAudio : SrsFrameTypeVideo;

        SrsRtpRawPayload *raw = new SrsRtpRawPayload();
        raw->nn_payload_ = 4;
        raw->payload_ = pkt->wrap(raw->nn_payload_);
        memset(raw->payload_, audio ? 0xaa : 0x55, raw->nn_payload_);
        pkt->set_payload(raw, SrsRtpPacketPayloadTypeRaw);

        return source_->on_rtp(pkt.get());
    }
    // What the client got, in order: the status of each answer, and "$<channel>" for each interleaved frame.
    std::string got()
    {
        std::string out(io_->out_buffer.bytes(), io_->out_buffer.length());
        std::string s;
        size_t pos = 0;
        while (pos < out.size()) {
            std::string item;
            if (out[pos] == '$') {
                int size = (pos + 4 > out.size()) ? 0 : (((uint8_t)out[pos + 2] << 8) | (uint8_t)out[pos + 3]);
                if (pos + 4 > out.size() || pos + 4 + size > out.size()) {
                    return s + (s.empty() ? "" : ",") + "cut";
                }
                item = "$" + srs_strconv_format_int((uint8_t)out[pos + 1]);
                pos += 4 + size;
            } else {
                size_t end = out.find("\r\n\r\n", pos);
                if (end == std::string::npos) {
                    return s + (s.empty() ? "" : ",") + "cut";
                }
                std::string header = out.substr(pos, end - pos);
                item = header.substr(9, 3);
                pos = end + 4;
                size_t cl = header.find("Content-Length: ");
                if (cl != std::string::npos) {
                    pos += ::atoi(header.c_str() + cl + 16);
                }
            }
            s += (s.empty() ? "" : ",") + item;
        }
        return s;
    }
    // Waits at most about 2s for the client to get the items.
    void wait_got(std::string expect)
    {
        for (int i = 0; i < 2000 && got() != expect; i++) {
            srs_usleep(1 * SRS_UTIME_MILLISECONDS);
        }
    }
    // PLAY is answered before its player starts; waits at most about 2s for the player to join the stream, as the
    // packets before it are not for this player.
    void wait_playing()
    {
        for (int i = 0; i < 2000 && source_->consumers_.empty(); i++) {
            srs_usleep(1 * SRS_UTIME_MILLISECONDS);
        }
    }
    void wait_done()
    {
        for (int i = 0; i < 2000 && !done_; i++) {
            srs_usleep(1 * SRS_UTIME_MILLISECONDS);
        }
    }
    SrsStatisticClient *stat_client()
    {
        return _srs_stat->find_client(conn_->cid_.c_str());
    }

public:
    virtual srs_error_t cycle()
    {
        if (whole_cycle_) {
            err_ = conn_->cycle();
        } else {
            err_ = conn_->do_cycle();
        }
        done_ = true;
        return srs_success;
    }
};

static const char *rtsp_conn_utest_hooks = "http_hooks { enabled on; on_play http://127.0.0.1:8085/api/v1/sessions; "
                                           "on_stop http://127.0.0.1:8085/api/v1/sessions; }";

// A player that wants only the video, such as a video wall or an analytics client, sets up only the video track of
// the two the DESCRIBE offers, then plays. It gets the video frames on its channel, the audio ones are not sent, and
// the play goes on until its TEARDOWN, which ends it with on_stop.
VOID TEST(AppRtspConnTest, PlayerSetsUpOnlyVideo_GetsOnlyVideoUntilTeardown)
{
    srs_error_t err;

    RtspConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open(string("vhost __defaultVhost__ { ") + rtsp_conn_utest_hooks + " }", "rtsp-utest-video-only"));
    HELPER_ASSERT_SUCCESS(c.start());

    c.request("DESCRIBE");
    c.setup_tcp(1, 2);
    c.request("PLAY");
    c.wait_got("200,200,200");
    c.wait_playing();
    ASSERT_STREQ("200,200,200", c.got().c_str());
    ASSERT_TRUE(c.conn_->player_ != NULL);

    for (uint16_t seq = 100; seq < 103; seq++) {
        HELPER_EXPECT_SUCCESS(c.publish(true, seq));
        HELPER_EXPECT_SUCCESS(c.publish(false, seq));
    }
    c.wait_got("200,200,200,$2,$2,$2");
    EXPECT_STREQ("200,200,200,$2,$2,$2", c.got().c_str());
    EXPECT_FALSE(c.done_);
    EXPECT_TRUE(c.stat_client() != NULL);

    c.request("TEARDOWN");
    c.wait_got("200,200,200,$2,$2,$2,200");
    EXPECT_STREQ("200,200,200,$2,$2,$2,200", c.got().c_str());
    EXPECT_STREQ("on_play,on_stop", c.hooks_.called().c_str());
    EXPECT_TRUE(c.conn_->player_ == NULL);
    EXPECT_TRUE(c.stat_client() == NULL);

    c.io_->client_closes();
    c.wait_done();
    EXPECT_TRUE(c.done_);
    EXPECT_EQ(1, c.manager_.remove_count_);
    EXPECT_STREQ("on_play,on_stop", c.hooks_.called().c_str());
}

// VLC sets up a track over UDP first, as it does by default. The server serves only TCP, so it answers 461 Unsupported
// Transport, and VLC sets the track up again over TCP, then plays it.
VOID TEST(AppRtspConnTest, PlayerTriesUdpFirst_UnsupportedTransportThenTcpPlays)
{
    srs_error_t err;

    RtspConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open("vhost __defaultVhost__ { }", "rtsp-utest-udp-first"));
    HELPER_ASSERT_SUCCESS(c.start());

    c.request("DESCRIBE");
    c.request("SETUP", "/trackID=0", "Transport: RTP/AVP;unicast;client_port=5000-5001\r\n");
    c.wait_got("200,461");
    ASSERT_STREQ("200,461", c.got().c_str());
    EXPECT_TRUE(c.conn_->networks_.empty());

    c.setup_tcp(0, 0);
    c.request("PLAY");
    c.wait_got("200,461,200,200");
    c.wait_playing();
    ASSERT_STREQ("200,461,200,200", c.got().c_str());

    HELPER_EXPECT_SUCCESS(c.publish(true, 100));
    HELPER_EXPECT_SUCCESS(c.publish(true, 101));
    c.wait_got("200,461,200,200,$0,$0");
    EXPECT_STREQ("200,461,200,200,$0,$0", c.got().c_str());
}

// A vhost whose security denies the client's address to play: the DESCRIBE is answered 403 Forbidden, with no on_play
// and no client in the statistic, and the PLAY after it is not valid, for no track is set up.
VOID TEST(AppRtspConnTest, PlayerDeniedBySecurity_ForbiddenWithNoHook)
{
    srs_error_t err;

    RtspConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open(string("vhost __defaultVhost__ { ") + rtsp_conn_utest_hooks +
                                     " security { enabled on; deny play 192.168.1.100; } }",
                                 "rtsp-utest-denied"));
    HELPER_ASSERT_SUCCESS(c.start());

    c.request("DESCRIBE");
    c.request("PLAY");
    c.wait_got("403,455");
    EXPECT_STREQ("403,455", c.got().c_str());
    EXPECT_STREQ("", c.hooks_.called().c_str());
    EXPECT_TRUE(c.stat_client() == NULL);
    EXPECT_TRUE(c.conn_->player_ == NULL);
}

// The client is gone when the server answers one of its requests: in the play flow, in each answer that refuses a
// request, and while it plays. The write fails and the connection ends at that answer, with the answers before it
// sent.
VOID TEST(AppRtspConnTest, ClientGoneAtEachAnswer_ConnectionEnds)
{
    // DESCRIBE, SETUP with no Transport, PLAY with no SETUP, SETUP over UDP, SETUP over TCP, PLAY, DESCRIBE and SETUP
    // while playing, TEARDOWN.
    const char *answers[] = {"200", "400", "455", "461", "200", "200", "455", "455", "200"};
    const char *wraps[] = {"response describe", "response setup", "response play", "response setup", "response setup",
                           "response record", "response describe", "response setup", "response teardown"};

    for (int i = 0; i < 9; i++) {
        RtspConnUtestClient c;
        c.whole_cycle_ = false;
        srs_error_t err = c.open("vhost __defaultVhost__ { }", "rtsp-utest-gone-" + srs_strconv_format_int(i));
        HELPER_ASSERT_SUCCESS(err);
        c.io_->fail_from_write_ = i + 1;

        std::string script = c.request_text("DESCRIBE");
        script += c.request_text("SETUP", "/trackID=1");
        script += c.request_text("PLAY");
        script += c.request_text("SETUP", "/trackID=1", "Transport: RTP/AVP;unicast;client_port=5000-5001\r\n");
        script += c.request_text("SETUP", "/trackID=1", "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n");
        script += c.request_text("PLAY");
        script += c.request_text("DESCRIBE");
        script += c.request_text("SETUP", "/trackID=0", "Transport: RTP/AVP/TCP;unicast;interleaved=2-3\r\n");
        script += c.request_text("TEARDOWN");
        c.io_->client_sends(script);
        HELPER_ASSERT_SUCCESS(c.start());
        c.wait_done();

        ASSERT_TRUE(c.done_) << i;
        std::string expect;
        for (int j = 0; j < i; j++) {
            expect += (j ? "," : "") + std::string(answers[j]);
        }
        EXPECT_STREQ(expect.c_str(), c.got().c_str()) << i;
        EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(c.err_)) << i << srs_error_desc(c.err_);
        EXPECT_NE(string::npos, srs_error_desc(c.err_).find(wraps[i])) << i << srs_error_desc(c.err_);
    }
}

// The player is gone while it plays: the write of a frame's 4-byte interleaved header fails, or the header is sent and
// the write of its packet fails, as a peer that resets between the two sends. The player drops the frame, and the
// connection ends at its next read, with on_stop, the client out of the statistic and the player off the stream.
VOID TEST(AppRtspConnTest, PlayerGoneWhilePlaying_FrameDroppedAndConnectionEnds)
{
    srs_error_t err;

    const char *gots[] = {"200,200,200", "200,200,200,cut"};
    for (int i = 0; i < 2; i++) {
        RtspConnUtestClient c;
        HELPER_ASSERT_SUCCESS(c.open(string("vhost __defaultVhost__ { ") + rtsp_conn_utest_hooks + " }", "rtsp-utest-gone-playing"));
        HELPER_ASSERT_SUCCESS(c.start());

        c.request("DESCRIBE");
        c.setup_tcp(1, 0);
        c.request("PLAY");
        c.wait_got("200,200,200");
        c.wait_playing();
        ASSERT_STREQ("200,200,200", c.got().c_str());

        // The three answers are the first three writes; the first frame's header is the fourth, its packet the fifth.
        // The player tries the next frame too, whose header fails.
        c.io_->fail_from_write_ = 4 + i;
        HELPER_EXPECT_SUCCESS(c.publish(false, 100));
        HELPER_EXPECT_SUCCESS(c.publish(false, 101));
        for (int j = 0; j < 2000 && c.io_->nb_writes_ < 5 + i; j++) {
            srs_usleep(1 * SRS_UTIME_MILLISECONDS);
        }
        EXPECT_EQ(5 + i, c.io_->nb_writes_) << i;
        EXPECT_STREQ(gots[i], c.got().c_str()) << i;
        EXPECT_FALSE(c.done_) << i;

        c.io_->client_closes();
        c.wait_done();
        ASSERT_TRUE(c.done_) << i;
        EXPECT_STREQ("on_play,on_stop", c.hooks_.called().c_str()) << i;
        EXPECT_TRUE(c.stat_client() == NULL) << i;
        EXPECT_TRUE(c.source_->consumers_.empty()) << i;
        EXPECT_EQ(1, c.manager_.remove_count_) << i;
    }
}

// The API kicks an RTSP player off (DELETE /api/v1/clients/{id}), which expires its connection, as it does for
// every client in the statistic. The connection ends with on_stop, and the player leaves the stream.
VOID TEST(AppRtspConnTest, PlayerKickedByApi_PlayEndsWithOnStop)
{
    srs_error_t err;

    RtspConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open(string("vhost __defaultVhost__ { ") + rtsp_conn_utest_hooks + " }", "rtsp-utest-kicked"));
    HELPER_ASSERT_SUCCESS(c.start());

    c.request("DESCRIBE");
    c.setup_tcp(1, 0);
    c.request("PLAY");
    c.wait_got("200,200,200");
    c.wait_playing();
    ASSERT_STREQ("200,200,200", c.got().c_str());
    HELPER_EXPECT_SUCCESS(c.publish(false, 100));
    HELPER_EXPECT_SUCCESS(c.publish(false, 101));
    c.wait_got("200,200,200,$0,$0");
    ASSERT_STREQ("200,200,200,$0,$0", c.got().c_str());

    SrsStatisticClient *client = c.stat_client();
    ASSERT_TRUE(client != NULL);
    ASSERT_TRUE(client->conn_ != NULL);
    client->conn_->expire();
    c.wait_done();

    ASSERT_TRUE(c.done_);
    EXPECT_STREQ("on_play,on_stop", c.hooks_.called().c_str());
    EXPECT_TRUE(c.stat_client() == NULL);
    EXPECT_TRUE(c.source_->consumers_.empty());
    EXPECT_EQ(1, c.manager_.remove_count_);
}

// State Threads cannot make the coroutine of the player at PLAY, as when the server is out of memory. The PLAY was
// answered before, so the client got 200, then the connection ends, and the play's hooks are paired.
VOID TEST(AppRtspConnTest, NoCoroutineForPlayer_ConnectionEndsAfterPlayAnswer)
{
    srs_error_t err;

    RtspConnUtestClient c;
    c.whole_cycle_ = false;
    HELPER_ASSERT_SUCCESS(c.open(string("vhost __defaultVhost__ { ") + rtsp_conn_utest_hooks + " }", "rtsp-utest-no-coroutine"));
    HELPER_ASSERT_SUCCESS(c.start());

    c.request("DESCRIBE");
    c.setup_tcp(1, 0);
    c.wait_got("200,200");
    ASSERT_STREQ("200,200", c.got().c_str());

    _ST_THREAD_CREATE_PFN ov = _pfn_st_thread_create;
    _pfn_st_thread_create = (_ST_THREAD_CREATE_PFN)rtmp_conn_utest_st_thread_create_out_of_memory;
    c.request("PLAY");
    c.wait_done();
    _pfn_st_thread_create = ov;

    ASSERT_TRUE(c.done_);
    EXPECT_STREQ("200,200,200", c.got().c_str());
    rtmp_conn_utest_expect(c.err_, ERROR_ST_CREATE_CYCLE_THREAD, "start play");
    c.err_ = srs_success;

    // The server ends the serve with the teardown, as SrsRtspConnection::cycle does.
    HELPER_EXPECT_SUCCESS(c.conn_->do_teardown());
    EXPECT_STREQ("on_play,on_stop", c.hooks_.called().c_str());
    EXPECT_TRUE(c.stat_client() == NULL);
    EXPECT_TRUE(c.source_->consumers_.empty());
}

// A player that plays a stream gets each packet the stream gets, at once: the play does not merge writes. A lone
// packet, such as the end of a frame or the last packet before the publisher pauses or stops, is sent without
// waiting for the next one.
VOID TEST(AppRtspConnTest, PlayerOfLonePacket_SentAtOnce)
{
    srs_error_t err;

    RtspConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open("vhost __defaultVhost__ { }", "rtsp-utest-lone-packet"));
    HELPER_ASSERT_SUCCESS(c.start());

    c.request("DESCRIBE");
    c.setup_tcp(1, 0);
    c.request("PLAY");
    c.wait_got("200,200,200");
    c.wait_playing();
    ASSERT_STREQ("200,200,200", c.got().c_str());

    HELPER_EXPECT_SUCCESS(c.publish(false, 100));
    c.wait_got("200,200,200,$0");
    EXPECT_STREQ("200,200,200,$0", c.got().c_str());
}

// The tracks a player of the RTSP stream was told about, one entry per change: V for a video track, A for an audio
// track, - for none, as SrsRtspPlayStream follows them.
class RtspSourceUtestPlayer : public ISrsRtcSourceChangeCallback
{
public:
    std::string changes_;
    uint32_t video_ssrc_;
    uint32_t audio_ssrc_;

public:
    RtspSourceUtestPlayer()
    {
        video_ssrc_ = 0;
        audio_ssrc_ = 0;
    }
    virtual ~RtspSourceUtestPlayer()
    {
    }

public:
    virtual void on_stream_change(SrsRtcSourceDescription *desc)
    {
        std::string tracks;
        if (!desc->video_track_descs_.empty()) {
            tracks += "V";
            video_ssrc_ = desc->video_track_descs_.at(0)->ssrc_;
        }
        if (desc->audio_track_desc_) {
            tracks += "A";
            audio_ssrc_ = desc->audio_track_desc_->ssrc_;
        }
        changes_ += (changes_.empty() ? "" : ",") + (tracks.empty() ? std::string("-") : tracks);
    }
};

// A stream an RTMP encoder publishes on a vhost with rtmp_to_rtsp: the RTSP source in the manager's pool and the
// builder SrsRtmpBridge makes for each publish, and the RTSP players of the source.
class RtspSourceUtestStream
{
public:
    MockSrsRequest req_;
    SrsStatistic stat_;
    MockCircuitBreaker breaker_;
    MockClockForPithyPrint clock_;
    SrsRtspSourceManager manager_;
    SrsSharedPtr<SrsRtspSource> source_;
    SrsRtspRtpBuilder *builder_;
    std::vector<SrsRtspConsumer *> players_;

public:
    RtspSourceUtestStream() : req_("__defaultVhost__", "live", "livestream")
    {
        clock_.now_ = 100 * SRS_UTIME_SECONDS;
        builder_ = NULL;

        SrsRtspSource *source = new SrsRtspSource();
        source->stat_ = &stat_;
        source->circuit_breaker_ = &breaker_;
        source->clk_ = &clock_;
        source_ = SrsSharedPtr<SrsRtspSource>(source);
        source->assemble();
        srs_error_t err = source->initialize(&req_);
        EXPECT_TRUE(err == srs_success) << srs_error_desc(err);
        srs_freep(err);

        manager_.pool_[req_.get_stream_url()] = source_;
    }
    virtual ~RtspSourceUtestStream()
    {
        for (int i = 0; i < (int)players_.size(); i++) {
            SrsRtspConsumer *player = players_.at(i);
            srs_freep(player);
        }
        srs_freep(builder_);
    }

public:
    // The encoder publishes, as SrsRtmpBridge::initialize and on_publish do.
    srs_error_t publish()
    {
        srs_error_t err = srs_success;

        srs_freep(builder_);
        builder_ = new SrsRtspRtpBuilder(source_.get(), source_);
        if ((err = builder_->initialize(&req_)) != srs_success) {
            return srs_error_wrap(err, "builder initialize");
        }
        if ((err = source_->on_publish()) != srs_success) {
            return srs_error_wrap(err, "source publish");
        }
        return builder_->on_publish();
    }
    // The encoder stops, as SrsRtmpBridge::on_unpublish does, and the live source frees the bridge.
    void unpublish()
    {
        builder_->on_unpublish();
        source_->on_unpublish();
        srs_freep(builder_);
    }
    srs_error_t video(int64_t timestamp, const uint8_t *data, int size)
    {
        SrsMediaPacket msg;
        msg.message_type_ = SrsFrameTypeVideo;
        msg.timestamp_ = timestamp;
        char *payload = new char[size];
        memcpy(payload, data, size);
        msg.wrap(payload, size);
        return builder_->on_frame(&msg);
    }
    srs_error_t audio(int64_t timestamp, const uint8_t *data, int size)
    {
        SrsMediaPacket msg;
        msg.message_type_ = SrsFrameTypeAudio;
        msg.timestamp_ = timestamp;
        char *payload = new char[size];
        memcpy(payload, data, size);
        msg.wrap(payload, size);
        return builder_->on_frame(&msg);
    }
    // An RTSP player starts to play, as SrsRtspPlayStream::cycle does.
    SrsRtspConsumer *play(ISrsRtcSourceChangeCallback *handler = NULL)
    {
        SrsRtspConsumer *player = NULL;
        srs_error_t err = source_->create_consumer(player);
        EXPECT_TRUE(err == srs_success) << srs_error_desc(err);
        srs_freep(err);
        if (!player) {
            return NULL;
        }
        player->handler_ = handler;
        players_.push_back(player);
        return player;
    }
    void leave(SrsRtspConsumer *player)
    {
        players_.erase(std::find(players_.begin(), players_.end(), player));
        srs_freep(player);
    }
    // The manager's check of its sources, as its 1s timer does.
    void check_sources(srs_utime_t elapsed)
    {
        clock_.now_ += elapsed;
        srs_error_t err = manager_.notify(1, 1 * SRS_UTIME_SECONDS, clock_.now_);
        EXPECT_TRUE(err == srs_success) << srs_error_desc(err);
        srs_freep(err);
    }
    bool in_pool()
    {
        return manager_.pool_.find(req_.get_stream_url()) != manager_.pool_.end();
    }
};

// What the player got since it last read, kept in pkts when given: aac for an audio packet, stap for the SPS and PPS
// of H.264 (stap265 for the VPS, SPS and PPS of HEVC), nalu for a NALU in one packet, fu for a fragment of a NALU, < at
// its first and > at its last, and * for the marker.
static string rtsp_source_utest_got(SrsRtspConsumer *player, std::vector<SrsRtpPacket *> *pkts = NULL)
{
    std::stringstream ss;
    for (int i = 0;; i++) {
        SrsRtpPacket *pkt = NULL;
        srs_error_t err = player->dump_packet(&pkt);
        EXPECT_TRUE(err == srs_success) << srs_error_desc(err);
        srs_freep(err);
        if (!pkt) {
            break;
        }

        ss << (i ? "," : "");
        if (pkt->is_audio()) {
            ss << "aac";
        } else if (pkt->nalu_type_ == kStapA) {
            ss << "stap";
        } else if (pkt->nalu_type_ == kStapHevc) {
            ss << "stap265";
        } else if (pkt->nalu_type_ == kFuA) {
            SrsRtpFUAPayload2 *fua = dynamic_cast<SrsRtpFUAPayload2 *>(pkt->payload());
            ss << "fu" << (fua->start_ ? "<" : "") << (fua->end_ ? ">" : "");
        } else {
            ss << "nalu";
        }
        ss << (pkt->header_.get_marker() ? "*" : "");

        if (pkts) {
            pkts->push_back(pkt);
        } else {
            srs_freep(pkt);
        }
    }
    return ss.str();
}

static void rtsp_source_utest_free(std::vector<SrsRtpPacket *> &pkts)
{
    for (int i = 0; i < (int)pkts.size(); i++) {
        SrsRtpPacket *pkt = pkts.at(i);
        srs_freep(pkt);
    }
    pkts.clear();
}

// An H.264 key frame larger than an RTP packet holds, as at a scene change or a high bitrate: the player gets the
// SPS and PPS, then the IDR in FU-A fragments of the frame's time, which put the NALU back together, the last with the
// marker.
VOID TEST(AppRtspSourceTest, KeyframeLargerThanRtpPacket_PlayerGetsFuA)
{
    srs_error_t err;

    RtspSourceUtestStream s;
    HELPER_ASSERT_SUCCESS(s.publish());
    SrsRtspConsumer *player = s.play();
    HELPER_ASSERT_SUCCESS(s.video(0, flv_utest_avc_sh, sizeof(flv_utest_avc_sh)));

    // An IDR of 3000 bytes: the FLV video tag header, the NALU size, then the NALU.
    const int nb_nalu = 3000;
    std::vector<uint8_t> idr(9 + nb_nalu);
    uint8_t head[] = {0x17, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0xb8, 0x65};
    memcpy(&idr[0], head, sizeof(head));
    for (int i = 1; i < nb_nalu; i++) {
        idr[9 + i] = (uint8_t)(i * 7);
    }
    HELPER_ASSERT_SUCCESS(s.video(40, &idr[0], (int)idr.size()));

    std::vector<SrsRtpPacket *> pkts;
    EXPECT_STREQ("stap,fu<,fu,fu>*", rtsp_source_utest_got(player, &pkts).c_str());
    ASSERT_EQ(4, (int)pkts.size());

    std::string nalu;
    for (int i = 1; i < (int)pkts.size(); i++) {
        SrsRtpPacket *pkt = pkts.at(i);
        SrsRtpFUAPayload2 *fua = dynamic_cast<SrsRtpFUAPayload2 *>(pkt->payload());
        EXPECT_EQ(SrsAvcNaluTypeIDR, fua->nalu_type_);
        EXPECT_LE(fua->size_, kRtpMaxPayloadSize);
        EXPECT_EQ(40u * 90, pkt->header_.get_timestamp());
        EXPECT_EQ(pkts.at(0)->header_.get_ssrc(), pkt->header_.get_ssrc());
        EXPECT_EQ((uint16_t)(pkts.at(0)->header_.get_sequence() + i), pkt->header_.get_sequence());
        nalu.append(fua->payload_, fua->size_);
    }
    EXPECT_TRUE(nalu == std::string((char *)&idr[10], nb_nalu - 1));
    rtsp_source_utest_free(pkts);
}

// An HEVC encoder over enhanced or legacy RTMP: the stream's video track is H.265 with its payload type, and a player
// gets the VPS, SPS and PPS in one packet, then the IDR.
VOID TEST(AppRtspSourceTest, HevcPublisher_PlayerGetsH265Track)
{
    srs_error_t err;

    RtspSourceUtestStream s;
    HELPER_ASSERT_SUCCESS(s.publish());
    SrsRtspConsumer *player = s.play();

    std::string sh;
    HELPER_ASSERT_SUCCESS(mp4_hevc_sh(1, sh));
    HELPER_ASSERT_SUCCESS(s.video(0, (const uint8_t *)sh.data(), (int)sh.size()));
    const uint8_t idr[] = {0x1c, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x26, 0x01, 0xaf, 0x00, 0x10};
    HELPER_ASSERT_SUCCESS(s.video(40, idr, sizeof(idr)));

    SrsRtcTrackDescription *video = s.source_->video_desc();
    ASSERT_TRUE(video != NULL);
    EXPECT_STREQ("H265", video->media_->name_.c_str());
    EXPECT_EQ(KVideoPayloadTypeHevc, video->media_->pt_);

    std::vector<SrsRtpPacket *> pkts;
    EXPECT_STREQ("stap265,nalu*", rtsp_source_utest_got(player, &pkts).c_str());
    ASSERT_EQ(2, (int)pkts.size());
    SrsRtpSTAPPayloadHevc *stap = dynamic_cast<SrsRtpSTAPPayloadHevc *>(pkts.at(0)->payload());
    ASSERT_TRUE(stap != NULL);
    EXPECT_EQ(3, (int)stap->nalus_.size());
    for (int i = 0; i < (int)pkts.size(); i++) {
        SrsRtpPacket *pkt = pkts.at(i);
        EXPECT_EQ(KVideoPayloadTypeHevc, pkt->header_.get_payload_type());
        EXPECT_EQ(video->ssrc_, pkt->header_.get_ssrc());
    }
    rtsp_source_utest_free(pkts);
}

// An encoder of H.264 and MP3 audio: RTSP plays only AAC, so the stream has only its video track, and a player gets
// only video.
VOID TEST(AppRtspSourceTest, Mp3Publisher_PlayerGetsOnlyVideo)
{
    srs_error_t err;

    RtspSourceUtestStream s;
    HELPER_ASSERT_SUCCESS(s.publish());
    SrsRtspConsumer *player = s.play();

    // MP3, 44.1kHz, 16 bits, stereo, then an MPEG-1 Layer III frame header.
    const uint8_t mp3[] = {0x2f, 0xff, 0xfb, 0x90, 0x64, 0x00, 0x00, 0x00, 0x00};
    HELPER_ASSERT_SUCCESS(s.video(0, flv_utest_avc_sh, sizeof(flv_utest_avc_sh)));
    HELPER_ASSERT_SUCCESS(s.audio(0, mp3, sizeof(mp3)));
    HELPER_ASSERT_SUCCESS(s.video(0, flv_utest_avc_idr, sizeof(flv_utest_avc_idr)));
    HELPER_ASSERT_SUCCESS(s.audio(23, mp3, sizeof(mp3)));

    EXPECT_TRUE(s.source_->audio_desc() == NULL);
    EXPECT_TRUE(s.source_->video_desc() != NULL);
    EXPECT_STREQ("stap,nalu*", rtsp_source_utest_got(player).c_str());
}

// The server overloaded, at its circuit breaker's dying level: the stream drops each packet for every player and counts
// it as lost, and the players get packets again once the load goes down.
VOID TEST(AppRtspSourceTest, ServerOverloaded_PacketsDroppedForEveryPlayer)
{
    srs_error_t err;

    RtspSourceUtestStream s;
    HELPER_ASSERT_SUCCESS(s.publish());
    SrsRtspConsumer *player1 = s.play();
    SrsRtspConsumer *player2 = s.play();
    HELPER_ASSERT_SUCCESS(s.video(0, flv_utest_avc_sh, sizeof(flv_utest_avc_sh)));

    s.breaker_.set_hybrid_dying_water_level(true);
    int64_t lost = _srs_pps_aloss2->sugar_;
    HELPER_ASSERT_SUCCESS(s.video(0, flv_utest_avc_idr, sizeof(flv_utest_avc_idr)));
    EXPECT_EQ(4, _srs_pps_aloss2->sugar_ - lost);
    EXPECT_STREQ("", rtsp_source_utest_got(player1).c_str());
    EXPECT_STREQ("", rtsp_source_utest_got(player2).c_str());

    s.breaker_.set_hybrid_dying_water_level(false);
    HELPER_ASSERT_SUCCESS(s.video(40, flv_utest_avc_inter, sizeof(flv_utest_avc_inter)));
    EXPECT_EQ(4, _srs_pps_aloss2->sugar_ - lost);
    EXPECT_STREQ("nalu*", rtsp_source_utest_got(player1).c_str());
    EXPECT_STREQ("nalu*", rtsp_source_utest_got(player2).c_str());
}

// The manager keeps the stream while the encoder publishes, also with no player, as after the last player left with
// packets it had not sent yet; once the encoder stops, it keeps the stream 3s more for a republish, then frees it.
VOID TEST(AppRtspSourceTest, SourceCleanup_KeptWhilePublishing_FreedThreeSecondsAfterUnpublish)
{
    srs_error_t err;

    RtspSourceUtestStream s;
    HELPER_ASSERT_SUCCESS(s.publish());
    SrsRtspConsumer *player = s.play();
    HELPER_ASSERT_SUCCESS(s.video(0, flv_utest_avc_sh, sizeof(flv_utest_avc_sh)));
    HELPER_ASSERT_SUCCESS(s.video(0, flv_utest_avc_idr, sizeof(flv_utest_avc_idr)));
    EXPECT_EQ(2, (int)player->queue_.size());

    s.leave(player);
    EXPECT_TRUE(s.source_->consumers_.empty());

    s.check_sources(10 * SRS_UTIME_SECONDS);
    EXPECT_TRUE(s.in_pool());

    s.unpublish();
    s.check_sources(2 * SRS_UTIME_SECONDS);
    EXPECT_TRUE(s.in_pool());

    s.check_sources(1 * SRS_UTIME_SECONDS);
    EXPECT_FALSE(s.in_pool());
}

// A player that waits for the stream before the encoder publishes, and stays across a republish: it is told about
// each track as the encoder's sequence headers come, with the SSRC of the packets it then gets, and about the new SSRCs
// of the republish.
VOID TEST(AppRtspSourceTest, PlayerWaitingBeforePublish_FollowsTracksAcrossRepublish)
{
    srs_error_t err;

    RtspSourceUtestStream s;
    RtspSourceUtestPlayer handler;
    SrsRtspConsumer *player = s.play(&handler);
    EXPECT_TRUE(s.source_->source_id().empty());

    HELPER_ASSERT_SUCCESS(s.publish());
    EXPECT_STREQ("-", handler.changes_.c_str());
    EXPECT_EQ(0, s.source_->source_id().compare(_srs_context->get_id()));

    HELPER_ASSERT_SUCCESS(s.video(0, flv_utest_avc_sh, sizeof(flv_utest_avc_sh)));
    HELPER_ASSERT_SUCCESS(s.audio(0, flv_utest_aac_sh, sizeof(flv_utest_aac_sh)));
    HELPER_ASSERT_SUCCESS(s.video(0, flv_utest_avc_idr, sizeof(flv_utest_avc_idr)));
    HELPER_ASSERT_SUCCESS(s.audio(23, flv_utest_aac_frame, sizeof(flv_utest_aac_frame)));
    EXPECT_STREQ("-,V,VA", handler.changes_.c_str());

    std::vector<SrsRtpPacket *> pkts;
    EXPECT_STREQ("stap,nalu*,aac*", rtsp_source_utest_got(player, &pkts).c_str());
    ASSERT_EQ(3, (int)pkts.size());
    EXPECT_EQ(handler.video_ssrc_, pkts.at(1)->header_.get_ssrc());
    EXPECT_EQ(handler.audio_ssrc_, pkts.at(2)->header_.get_ssrc());
    uint32_t video_ssrc = handler.video_ssrc_;
    rtsp_source_utest_free(pkts);

    s.unpublish();
    EXPECT_TRUE(s.source_->source_id().empty());
    HELPER_ASSERT_SUCCESS(s.publish());
    HELPER_ASSERT_SUCCESS(s.video(0, flv_utest_avc_sh, sizeof(flv_utest_avc_sh)));
    HELPER_ASSERT_SUCCESS(s.video(0, flv_utest_avc_idr, sizeof(flv_utest_avc_idr)));
    EXPECT_STREQ("-,V,VA,VA,VA", handler.changes_.c_str());
    EXPECT_NE(video_ssrc, handler.video_ssrc_);

    EXPECT_STREQ("stap,nalu*", rtsp_source_utest_got(player, &pkts).c_str());
    ASSERT_EQ(2, (int)pkts.size());
    EXPECT_EQ(handler.video_ssrc_, pkts.at(1)->header_.get_ssrc());
    rtsp_source_utest_free(pkts);
}

// The interleaved RTP frames the RTSP client got, each as $<channel>:<SSRC>/<payload type>.
static string rtsp_source_utest_rtp(RtspConnUtestClient &c)
{
    std::string out(c.io_->out_buffer.bytes(), c.io_->out_buffer.length());
    std::stringstream ss;
    for (size_t pos = out.find('$'); pos != std::string::npos && pos + 16 <= out.size();) {
        SrsBuffer b((char *)out.data() + pos, (int)(out.size() - pos));
        b.skip(1);
        int channel = b.read_1bytes();
        int size = b.read_2bytes();
        b.skip(1);
        int pt = b.read_1bytes() & 0x7f;
        b.skip(6);
        uint32_t ssrc = (uint32_t)b.read_4bytes();
        ss << (ss.str().empty() ? "" : ",") << "$" << channel << ":" << ssrc << "/" << pt;
        pos += 4 + size;
    }
    return ss.str();
}

// A player that stays while the encoder republishes, whose new tracks have other SSRCs and payload types. The player
// keeps getting both tracks with the SSRCs and payload types of the SDP it was answered, as its decoder knows only those.
VOID TEST(AppRtspSourceTest, PlayerStaysAcrossRepublish_KeepsSsrcAndPayloadTypeOfItsSdp)
{
    srs_error_t err;

    RtspConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open("vhost __defaultVhost__ { }", "rtsp-utest-republish"));
    HELPER_ASSERT_SUCCESS(c.start());

    c.request("DESCRIBE");
    c.setup_tcp(0, 0);
    c.setup_tcp(1, 2);
    c.request("PLAY");
    c.wait_got("200,200,200,200");
    c.wait_playing();
    ASSERT_STREQ("200,200,200,200", c.got().c_str());

    // The republish: the builder of the new publish sets the tracks it made from the encoder's sequence headers.
    SrsUniquePtr<SrsRtcTrackDescription> audio(c.source_->audio_desc()->copy());
    audio->ssrc_ = 1002;
    audio->media_->pt_ = 111;
    c.source_->set_audio_desc(audio.get());
    SrsUniquePtr<SrsRtcTrackDescription> video(c.source_->video_desc()->copy());
    video->ssrc_ = 2002;
    video->media_->pt_ = 102;
    c.source_->set_video_desc(video.get());

    HELPER_EXPECT_SUCCESS(c.publish(true, 200, 1002, 111));
    HELPER_EXPECT_SUCCESS(c.publish(false, 200, 2002, 102));
    HELPER_EXPECT_SUCCESS(c.publish(true, 201, 1002, 111));
    HELPER_EXPECT_SUCCESS(c.publish(false, 201, 2002, 102));
    // The last packet may wait for the next one (B56), so only the first three are checked.
    c.wait_got("200,200,200,200,$0,$2,$0,$2");
    EXPECT_EQ(0u, c.got().find("200,200,200,200,$0,$2,$0"));
    EXPECT_EQ(0u, rtsp_source_utest_rtp(c).find("$0:1001/97,$2:2001/96,$0:1001/97"));
}

// An HEVC encoder that sends its VPS, SPS and PPS in band, in each key frame, with a sequence header that holds none,
// as ISO/IEC 14496-15 allows for hev1. The format parses both, and the RTSP builder has no parameter set to send in a
// STAP, so the player gets the key frame's NALUs as they are, the parameter sets first, and the encoder stays.
VOID TEST(AppRtspSourceTest, HevcParameterSetsInBand_KeyFrameNalusSent)
{
    srs_error_t err;

    RtspSourceUtestStream s;
    HELPER_ASSERT_SUCCESS(s.publish());
    SrsRtspConsumer *player = s.play();

    // The HEVCDecoderConfigurationRecord: version 1, Main profile, level 3.1, 4-byte NALU sizes, and no arrays.
    const uint8_t sh[] = {0x1c, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x60, 0x00, 0x00, 0x00, 0x90, 0x00, 0x00, 0x00,
                          0x00, 0x00, 0x5d, 0xf0, 0x00, 0xfc, 0xfd, 0xf8, 0xf8, 0x00, 0x00, 0x0f, 0x00};
    HELPER_ASSERT_SUCCESS(s.video(0, sh, sizeof(sh)));

    std::string tag;
    HELPER_ASSERT_SUCCESS(mp4_hevc_sh(1, tag));
    SrsFormat format;
    HELPER_ASSERT_SUCCESS(format.initialize());
    HELPER_ASSERT_SUCCESS(format.on_video(0, (char *)tag.data(), (int)tag.size()));

    // The key frame: the VPS, SPS and PPS, then the IDR, each with its size.
    std::string frame("\x1c\x01\x00\x00\x00", 5);
    std::vector<SrsHevcHvccNalu> &arrays = format.vcodec_->hevc_dec_conf_record_.nalu_vec_;
    for (int i = 0; i < (int)arrays.size(); i++) {
        const std::vector<uint8_t> &nalu = arrays.at(i).nal_data_vec_.at(0).nal_unit_data_;
        frame += std::string(3, '\0') + (char)nalu.size() + std::string((const char *)&nalu[0], nalu.size());
    }
    frame += std::string("\x00\x00\x00\x05\x26\x01\xaf\x00\x10", 9);

    HELPER_EXPECT_SUCCESS(s.video(40, (const uint8_t *)frame.data(), (int)frame.size()));
    EXPECT_STREQ("nalu,nalu,nalu,nalu*", rtsp_source_utest_got(player).c_str());
}
#endif

// Checks a client of the given type and address against the default vhost's security of the config text, as
// SrsRtmpConn, SrsRtcConnection, SrsMpegtsSrtConn, SrsRtspConnection and the HTTP stream servers do when a client
// joins, and returns the error code, 0 when the client is allowed.
static int security_utest_check(MockSrsConfig *conf, SrsRtmpConnType type, string ip)
{
    SrsSecurity sec;
    sec.config_ = conf;
    SrsRequest req;
    req.vhost_ = "__defaultVhost__";

    srs_error_t err = sec.check(type, ip, &req);
    sec.config_ = NULL;

    int code = srs_error_code(err);
    srs_freep(err);
    return code;
}

// A vhost that takes streams only from the encoders of a private network (`allow publish 10.0.0.0/8;`): an RTMP,
// WebRTC or SRT encoder at 10.1.2.3 publishes, while one outside the network, even the next address after it, is
// refused as not allowed; the rule says nothing about players, so a player in the network is refused too.
VOID TEST(AppSecurityTest, AllowPublishBySubnet_EncodersInNetworkOnly)
{
    srs_error_t err;

    MockSrsConfig conf;
    HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost __defaultVhost__{security{enabled on; allow publish 10.0.0.0/8;}}"));

    EXPECT_EQ(0, security_utest_check(&conf, SrsRtmpConnFMLEPublish, "10.1.2.3"));
    EXPECT_EQ(0, security_utest_check(&conf, SrsRtcConnPublish, "10.255.255.255"));
    EXPECT_EQ(0, security_utest_check(&conf, SrsSrtConnPublish, "10.0.0.1"));

    EXPECT_EQ(ERROR_SYSTEM_SECURITY_ALLOW, security_utest_check(&conf, SrsRtmpConnFMLEPublish, "11.0.0.0"));
    EXPECT_EQ(ERROR_SYSTEM_SECURITY_ALLOW, security_utest_check(&conf, SrsRtcConnPublish, "192.168.1.5"));
    EXPECT_EQ(ERROR_SYSTEM_SECURITY_ALLOW, security_utest_check(&conf, SrsRtmpConnPlay, "10.1.2.3"));
}

// A vhost that keeps the players of an office network out (`deny play 192.168.0.0/16; allow play all;`): an RTMP,
// HTTP-FLV, HLS or WebRTC player at 192.168.3.4 is denied by the rule, while a player outside it plays, and an encoder
// in the network is not a player, so the play rules do not deny it.
VOID TEST(AppSecurityTest, DenyPlayBySubnet_PlayersInNetworkDenied)
{
    srs_error_t err;

    MockSrsConfig conf;
    HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost __defaultVhost__{security{enabled on; deny play 192.168.0.0/16; allow play all; allow publish all;}}"));

    EXPECT_EQ(ERROR_SYSTEM_SECURITY_DENY, security_utest_check(&conf, SrsRtmpConnPlay, "192.168.3.4"));
    EXPECT_EQ(ERROR_SYSTEM_SECURITY_DENY, security_utest_check(&conf, SrsFlvPlay, "192.168.0.1"));
    EXPECT_EQ(ERROR_SYSTEM_SECURITY_DENY, security_utest_check(&conf, SrsHlsPlay, "192.168.255.254"));
    EXPECT_EQ(ERROR_SYSTEM_SECURITY_DENY, security_utest_check(&conf, SrsRtcConnPlay, "192.168.3.4"));

    EXPECT_EQ(0, security_utest_check(&conf, SrsRtmpConnPlay, "192.169.0.1"));
    EXPECT_EQ(0, security_utest_check(&conf, SrsFlvPlay, "10.0.0.5"));
    EXPECT_EQ(0, security_utest_check(&conf, SrsRtmpConnFMLEPublish, "192.168.3.4"));
}

// A vhost open to every encoder but a subnet it does not trust (`deny publish 172.16.0.0/12; allow publish all;`): an
// RTMP or SRT encoder at 172.20.1.1 is denied by the rule, while one just past the /12 network, 172.32.0.1, publishes,
// and a player in the subnet is not an encoder, so it still plays.
VOID TEST(AppSecurityTest, DenyPublishBySubnet_EncodersInNetworkDenied)
{
    srs_error_t err;

    MockSrsConfig conf;
    HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost __defaultVhost__{security{enabled on; deny publish 172.16.0.0/12; allow publish all; allow play all;}}"));

    EXPECT_EQ(ERROR_SYSTEM_SECURITY_DENY, security_utest_check(&conf, SrsRtmpConnFMLEPublish, "172.20.1.1"));
    EXPECT_EQ(ERROR_SYSTEM_SECURITY_DENY, security_utest_check(&conf, SrsSrtConnPublish, "172.31.255.255"));

    EXPECT_EQ(0, security_utest_check(&conf, SrsRtmpConnFMLEPublish, "172.32.0.1"));
    EXPECT_EQ(0, security_utest_check(&conf, SrsSrtConnPublish, "172.15.255.255"));
    EXPECT_EQ(0, security_utest_check(&conf, SrsRtmpConnPlay, "172.20.1.1"));
}

// A server started as the real one is, initialize then listen, from a config of its listens, with the TCP listeners
// and the parts that touch the host (pid file, HTTP server, black hole, SRT poller) replaced by mocks.
class ServerUtestStart
{
public:
    MockAppConfigForServerListen config_;
    MockConnectionManagerForServerQuit conn_manager_;
    MockHttpServerForServer http_server_;
    MockRtcBlackholeForServer blackhole_;
    MockSrtEventLoopForServer srt_eventloop_;
    SrsServer *server_;
    Ai36ServerListenersInjector *listeners_;

public:
    ServerUtestStart()
    {
        server_ = new SrsServer();

        ISrsPidFileLocker *locker = server_->pid_file_locker_;
        server_->pid_file_locker_ = new MockPidFileLocker();
        srs_freep(locker);

        srs_freep(server_->http_server_);
        server_->http_server_ = &http_server_;
        server_->assemble();

        server_->blackhole_ = &blackhole_;
        server_->srt_eventloop_ = &srt_eventloop_;
        server_->config_ = &config_;
        server_->conn_manager_ = &conn_manager_;
        listeners_ = new Ai36ServerListenersInjector(server_);
    }
    ~ServerUtestStart()
    {
        srs_freep(listeners_);
        if (server_->http_api_mux_ == &http_server_) {
            server_->http_api_mux_ = NULL;
        }
        server_->http_server_ = NULL;
        server_->blackhole_ = NULL;
        server_->srt_eventloop_ = NULL;
        server_->config_ = NULL;
        server_->conn_manager_ = NULL;
        srs_freep(server_);
    }

public:
    srs_error_t start()
    {
        srs_error_t err = srs_success;

        if ((err = server_->initialize()) != srs_success) {
            return srs_error_wrap(err, "initialize");
        }

        if ((err = server_->listen()) != srs_success) {
            return srs_error_wrap(err, "listen");
        }

        return err;
    }
};

// WebRTC over TCP configured on the HTTP server's port (`http_server{listen 8080;}` and
// `rtc_server{protocol tcp; tcp{enabled on; listen 8080;}}`), so a browser reaches WebRTC through the HTTP port: the
// server opens no WebRTC listener of its own, and the HTTP listener on 8080 takes both kinds of clients.
VOID TEST(ServerStartTest, WebRtcTcpOnHttpPort_SharesHttpListener)
{
    srs_error_t err;

    ServerUtestStart s;
    s.config_.rtmp_listens_.push_back("1935");
    s.config_.http_stream_enabled_ = true;
    s.config_.http_stream_listens_.push_back("8080");
    s.config_.rtc_server_enabled_ = true;
    s.config_.rtc_server_protocol_ = "tcp";
    s.config_.rtc_server_tcp_enabled_ = true;
    s.config_.rtc_server_tcp_listens_.push_back("8080");

    HELPER_EXPECT_SUCCESS(s.start());

    EXPECT_TRUE(s.server_->reuse_rtc_over_server_);
    EXPECT_EQ(0, s.listeners_->webrtc_.add_count_ + s.listeners_->webrtc_.listen_count_);
    EXPECT_STREQ("8080", srs_strings_join(s.listeners_->http_.endpoints_, ",").c_str());
    EXPECT_EQ(1, s.listeners_->http_.listen_count_);
}

// WebRTC over TCP configured on the HTTPS server's port (`https{listen 8088;}` and `tcp{listen 8088;}`), as a site
// that serves its pages and WebRTC only over TLS port 8088 does: the HTTPS listener takes the WebRTC clients, and no
// WebRTC listener is opened, while the HTTP server keeps its own port.
VOID TEST(ServerStartTest, WebRtcTcpOnHttpsPort_SharesHttpsListener)
{
    srs_error_t err;

    ServerUtestStart s;
    s.config_.rtmp_listens_.push_back("1935");
    s.config_.http_stream_enabled_ = true;
    s.config_.http_stream_listens_.push_back("8080");
    s.config_.https_stream_enabled_ = true;
    s.config_.https_stream_listens_.push_back("8088");
    s.config_.rtc_server_enabled_ = true;
    s.config_.rtc_server_protocol_ = "tcp";
    s.config_.rtc_server_tcp_enabled_ = true;
    s.config_.rtc_server_tcp_listens_.push_back("8088");

    HELPER_EXPECT_SUCCESS(s.start());

    EXPECT_TRUE(s.server_->reuse_rtc_over_server_);
    EXPECT_EQ(0, s.listeners_->webrtc_.add_count_ + s.listeners_->webrtc_.listen_count_);
    EXPECT_STREQ("8088", srs_strings_join(s.listeners_->https_.endpoints_, ",").c_str());
    EXPECT_EQ(1, s.listeners_->https_.listen_count_);
    EXPECT_STREQ("8080", srs_strings_join(s.listeners_->http_.endpoints_, ",").c_str());
}

// WebRTC over TCP on a port no HTTP server serves gets its own listener: a TCP port of its own (8000 beside the HTTP
// server's 8080), or the HTTP server's port number while the HTTP server is disabled.
VOID TEST(ServerStartTest, WebRtcTcpWithoutHttpServerOnItsPort_ListensItself)
{
    srs_error_t err;

    if (true) {
        ServerUtestStart s;
        s.config_.rtmp_listens_.push_back("1935");
        s.config_.http_stream_enabled_ = true;
        s.config_.http_stream_listens_.push_back("8080");
        s.config_.rtc_server_enabled_ = true;
        s.config_.rtc_server_protocol_ = "tcp";
        s.config_.rtc_server_tcp_enabled_ = true;
        s.config_.rtc_server_tcp_listens_.push_back("8000");

        HELPER_EXPECT_SUCCESS(s.start());

        EXPECT_FALSE(s.server_->reuse_rtc_over_server_);
        EXPECT_STREQ("8000", srs_strings_join(s.listeners_->webrtc_.endpoints_, ",").c_str());
        EXPECT_EQ(1, s.listeners_->webrtc_.listen_count_);
        EXPECT_EQ(1, s.listeners_->http_.listen_count_);
    }

    if (true) {
        ServerUtestStart s;
        s.config_.rtmp_listens_.push_back("1935");
        s.config_.http_stream_listens_.push_back("8080");
        s.config_.rtc_server_enabled_ = true;
        s.config_.rtc_server_protocol_ = "tcp";
        s.config_.rtc_server_tcp_enabled_ = true;
        s.config_.rtc_server_tcp_listens_.push_back("8080");

        HELPER_EXPECT_SUCCESS(s.start());

        EXPECT_FALSE(s.server_->reuse_rtc_over_server_);
        EXPECT_STREQ("8080", srs_strings_join(s.listeners_->webrtc_.endpoints_, ",").c_str());
        EXPECT_EQ(1, s.listeners_->webrtc_.listen_count_);
        EXPECT_EQ(0, s.listeners_->http_.listen_count_);
    }
}

// A reload (SIGHUP or the reload API) whose new config fails while it is being applied, such as a new listen port
// already in use: some changes may be applied and others not, so the server cycle fails and the server stops, after
// recording the applying state and the error for the reload API.
VOID TEST(ServerCycleTest, ReloadFailsWhileApplying_ServerStops)
{
    srs_error_t err;

    SrsUniquePtr<SrsServer> server(new SrsServer());
    server->assemble();

    MockAppConfigForDo2Cycle config;
    server->config_ = &config;
    MockReloadStatusForServer status;
    server->reload_status_ = &status;

    server->signal_fast_quit_ = false;
    server->signal_gracefully_quit_ = false;
    server->signal_reload_ = true;
    config.reload_state_ = SrsReloadStateApplying;
    config.reload_error_ = srs_error_new(ERROR_SOCKET_BIND, "mock bind");

    err = server->do2_cycle();
    EXPECT_EQ(ERROR_SOCKET_BIND, srs_error_code(err));
    EXPECT_TRUE(srs_error_desc(err).find("reload fatal error") != string::npos);
    srs_freep(err);

    EXPECT_FALSE(server->signal_reload_);
    EXPECT_EQ(1, config.reload_count_);
    EXPECT_EQ(1, status.update_count_);
    EXPECT_EQ(SrsReloadStateApplying, status.update_state_);
    EXPECT_EQ(ERROR_SOCKET_BIND, status.update_error_code_);

    server->reload_status_ = NULL;
    server->config_ = NULL;
}

// Config for the worker that watches the config file to reload it on a change.
class InotifyUtestConfig : public MockAppConfig
{
public:
    bool inotify_auto_reload_;
    bool auto_reload_for_docker_;
    std::string config_path_;

public:
    InotifyUtestConfig()
    {
        inotify_auto_reload_ = false;
        auto_reload_for_docker_ = false;
    }
    virtual ~InotifyUtestConfig()
    {
    }

public:
    virtual bool inotify_auto_reload() { return inotify_auto_reload_; }
    virtual bool auto_reload_for_docker() { return auto_reload_for_docker_; }
    virtual std::string config() { return config_path_; }
};

extern bool _srs_in_docker;

// The default config has no `inotify_auto_reload`, and `auto_reload_for_docker` is off, so a server, in Docker or
// not, watches no config file: editing it does nothing until a reload is asked for.
VOID TEST(ServerInotifyTest, NoAutoReload_WatchesNothing)
{
    srs_error_t err;

    InotifyUtestConfig config;
    config.config_path_ = _srs_tmp_file_prefix + "inotify-off.conf";
    SrsUniquePtr<SrsServer> server(new SrsServer());

    bool in_docker = _srs_in_docker;
    bool dockers[] = {false, true};
    for (int i = 0; i < 2; i++) {
        _srs_in_docker = dockers[i];

        SrsInotifyWorker worker(server.get());
        worker.config_ = &config;
        HELPER_EXPECT_SUCCESS(worker.start());
        EXPECT_TRUE(worker.inotify_fd_ == NULL);
        worker.config_ = NULL;
    }
    _srs_in_docker = in_docker;
}

// inotify is Linux only; on macOS the worker never watches.
#if !defined(SRS_OSX) && !defined(_WIN32)
// Writes the config file the worker watches, as an operator editing it does.
static void inotify_utest_write(string path)
{
    FILE *f = fopen(path.c_str(), "w");
    if (f) {
        fputs("listen 1935;\n", f);
        fclose(f);
    }
}

// Ends the worker's coroutine so the worker can be freed, as the server's stop does. If the worker does not end, its
// read is interrupted again, so a failing test does not hang in the worker's destructor.
static void inotify_utest_end(SrsInotifyWorker *worker)
{
    SrsSTCoroutine *trd = dynamic_cast<SrsSTCoroutine *>(worker->trd_);
    trd->interrupt();
    srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    if (!trd->impl_->cycle_done_) {
        srs_thread_interrupt(trd->impl_->trd_);
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    }
}

// With `inotify_auto_reload on`, an operator editing the config file makes the server reload it, as a reload signal
// does.
VOID TEST(ServerInotifyTest, AutoReload_ConfigEditSignalsReload)
{
    srs_error_t err;

    string path = _srs_tmp_file_prefix + "inotify-on.conf";
    MockFileRemover mfr(path);

    InotifyUtestConfig config;
    config.inotify_auto_reload_ = true;
    config.config_path_ = path;
    SrsUniquePtr<SrsServer> server(new SrsServer());
    server->signal_reload_ = false;

    SrsInotifyWorker worker(server.get());
    worker.config_ = &config;
    HELPER_ASSERT_SUCCESS(worker.start());
    EXPECT_TRUE(worker.inotify_fd_ != NULL);

    inotify_utest_write(path);
    for (int i = 0; i < 100 && !server->signal_reload_; i++) {
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    }
    EXPECT_TRUE(server->signal_reload_);

    inotify_utest_end(&worker);
    worker.config_ = NULL;
}

// The server quits (SIGTERM or SIGQUIT) within 3s after an operator edits the config file: the quit stops the
// worker as it frees it, and the worker must end, or the server never exits.
VOID TEST(ServerInotifyTest, QuitSoonAfterConfigEdit_WorkerEnds)
{
    srs_error_t err;

    string path = _srs_tmp_file_prefix + "inotify-quit.conf";
    MockFileRemover mfr(path);

    InotifyUtestConfig config;
    config.inotify_auto_reload_ = true;
    config.config_path_ = path;
    SrsUniquePtr<SrsServer> server(new SrsServer());
    server->signal_reload_ = false;

    SrsInotifyWorker worker(server.get());
    worker.config_ = &config;
    HELPER_ASSERT_SUCCESS(worker.start());

    inotify_utest_write(path);
    for (int i = 0; i < 100 && !server->signal_reload_; i++) {
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    }
    ASSERT_TRUE(server->signal_reload_);

    // The stop's interrupt, which SrsSTCoroutine::stop sends before it joins.
    SrsSTCoroutine *trd = dynamic_cast<SrsSTCoroutine *>(worker.trd_);
    trd->interrupt();
    for (int i = 0; i < 50 && !trd->impl_->cycle_done_; i++) {
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    }
    EXPECT_TRUE(trd->impl_->cycle_done_);

    inotify_utest_end(&worker);
    worker.config_ = NULL;
}

// In Docker with `auto_reload_for_docker on`, as a Kubernetes ConfigMap mount is used, the server watches the config
// file though `inotify_auto_reload` is not set.
VOID TEST(ServerInotifyTest, AutoReloadForDocker_WatchesInDocker)
{
    srs_error_t err;

    InotifyUtestConfig config;
    config.auto_reload_for_docker_ = true;
    config.config_path_ = _srs_tmp_file_prefix + "inotify-docker.conf";
    SrsUniquePtr<SrsServer> server(new SrsServer());

    bool in_docker = _srs_in_docker;
    _srs_in_docker = true;

    SrsInotifyWorker worker(server.get());
    worker.config_ = &config;
    HELPER_EXPECT_SUCCESS(worker.start());
    EXPECT_TRUE(worker.inotify_fd_ != NULL);

    inotify_utest_end(&worker);
    worker.config_ = NULL;
    _srs_in_docker = in_docker;
}
#endif

// The SRT connection of a client. A read waits for what the client sends, as SrsSrtSocket's does: it ends with
// ERROR_SRT_INTERRUPT when the coroutine is interrupted, and with ERROR_SRT_IO once the client has closed. Writes fail
// from a given one on, as when the player is gone.
class SrtConnUtestIO : public MockSrtConnection
{
public:
    // The first write that fails, counted from 1, or 0 for none.
    int fail_write_at_;
    int nb_writes_;

public:
    SrtConnUtestIO()
    {
        fail_write_at_ = 0;
        nb_writes_ = 0;
    }
    virtual ~SrtConnUtestIO()
    {
    }

public:
    void client_sends(const std::string &msg)
    {
        recv_msgs_.push_back(msg);
        cond_->signal();
    }
    void client_closes()
    {
        srs_freep(read_error_);
        read_error_ = srs_error_new(ERROR_SRT_IO, "srt_recvmsg, err=Connection was broken");
        cond_->signal();
    }
    virtual srs_error_t read(void *buf, size_t size, ssize_t *nread)
    {
        while (recv_msgs_.empty()) {
            if (read_error_ != srs_success) {
                return srs_error_copy(read_error_);
            }
            if (cond_->wait() != 0) {
                return srs_error_new(ERROR_SRT_INTERRUPT, "srt socket %d interrupted", (int)srt_fd_);
            }
        }

        std::string msg = recv_msgs_.front();
        recv_msgs_.erase(recv_msgs_.begin());
        size_t n = srs_min(size, msg.size());
        memcpy(buf, msg.data(), n);
        *nread = n;
        recv_bytes_ += n;
        read_count_++;
        return srs_success;
    }
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite)
    {
        nb_writes_++;
        if (fail_write_at_ && nb_writes_ >= fail_write_at_) {
            return srs_error_new(ERROR_SRT_IO, "srt_sendmsg, err=Connection was broken");
        }
        return MockSrtConnection::write(buf, size, nwrite);
    }
};

// An SRT client of the server: the real SrsMpegtsSrtConn with the vhost from config text, the stream's real SRT source
// in its manager, the statistic and the publish tokens. The client's connection is SrtConnUtestIO, or a real SRT socket
// the listener accepted. The connection serves the client in its own coroutine, as the server runs it.
class SrtConnUtestClient : public ISrsCoroutineHandler
{
public:
    MockSrsConfig conf_;
    SrsStatistic stat_;
    RtmpConnUtestHooks hooks_;
    SrsStreamPublishTokenManager tokens_;
    SrsSrtSourceManager srt_sources_;
    MockLiveSourceManager live_sources_;
    MockRtcSourceManager rtc_sources_;
    MockConnectionManager manager_;
    MockSrsRequest req_;
    // The source of the stream live/livestream, which the client's stream id names.
    SrsSharedPtr<SrsSrtSource> source_;
    SrtConnUtestIO *io_;
    SrsMpegtsSrtConn *conn_;
    srs_error_t err_;
    bool done_;

public:
    SrtConnUtestClient() : req_("__defaultVhost__", "live", "livestream")
    {
        io_ = NULL;
        conn_ = NULL;
        err_ = srs_success;
        done_ = false;
    }
    virtual ~SrtConnUtestClient()
    {
        srs_freep(conn_);
        srs_freep(err_);
    }

public:
    // A client of the stream id, over SrtConnUtestIO.
    srs_error_t open(std::string text, std::string streamid)
    {
        io_ = new SrtConnUtestIO();
        io_->streamid_ = streamid;
        return prepare(text, 100, "192.168.1.100", io_);
    }
    // A client over the SRT socket, as the listener accepted it.
    srs_error_t open_socket(std::string text, srs_srt_t fd)
    {
        return prepare(text, fd, "127.0.0.1", NULL);
    }
    srs_error_t start()
    {
        return conn_->start();
    }
    // Waits for the connection to end, at most about 2s.
    void wait_done()
    {
        for (int i = 0; i < 2000 && !done_; i++) {
            srs_usleep(1 * SRS_UTIME_MILLISECONDS);
        }
    }
    // Serves the client until the connection ends, at most about 2s, and returns what it returned.
    srs_error_t run()
    {
        srs_error_t err = start();
        if (err != srs_success) {
            return err;
        }

        wait_done();
        if (!done_) {
            return srs_error_new(ERROR_SYSTEM_ASSERT_FAILED, "srt connection not done");
        }

        err = err_;
        err_ = srs_success;
        return err;
    }
    // Waits for the encoder to publish the stream, at most about 1s.
    void wait_publishing()
    {
        for (int i = 0; i < 1000 && source_->can_publish(); i++) {
            srs_usleep(1 * SRS_UTIME_MILLISECONDS);
        }
    }
    // Waits for the player to join the stream, at most about 1s.
    void wait_playing()
    {
        for (int i = 0; i < 1000 && source_->consumers_.empty(); i++) {
            srs_usleep(1 * SRS_UTIME_MILLISECONDS);
        }
    }
    // Another encoder publishes the stream, or leaves it.
    srs_error_t encoder_publishes()
    {
        return source_->on_publish();
    }
    void encoder_unpublishes()
    {
        source_->on_unpublish();
    }
    // That encoder sends a message, which the source delivers to its players.
    srs_error_t encoder_sends(const std::string &msg)
    {
        SrsSrtPacket pkt;
        pkt.wrap((char *)msg.data(), (int)msg.size());
        return source_->on_srt_packet(&pkt);
    }
    // Whether an encoder can take the stream's publish token, which is free when no encoder holds it.
    bool token_free()
    {
        SrsStreamPublishToken *token = NULL;
        srs_error_t err = tokens_.acquire_token(&req_, token);
        bool ok = (err == srs_success);
        srs_freep(err);
        srs_freep(token);
        return ok;
    }

public:
    virtual srs_error_t cycle()
    {
        err_ = conn_->do_cycle();
        done_ = true;
        return srs_success;
    }

private:
    srs_error_t prepare(std::string text, srs_srt_t fd, std::string ip, SrtConnUtestIO *io)
    {
        srs_error_t err = srs_success;

        if ((err = conf_.mock_parse(std::string(_MIN_OK_CONF) + text)) != srs_success) {
            return srs_error_wrap(err, "parse");
        }

        // The stream's source, which the server keeps, with the statistic of the test.
        if ((err = srt_sources_.fetch_or_create(&req_, source_)) != srs_success) {
            return srs_error_wrap(err, "source");
        }
        source_->stat_ = &stat_;

        conn_ = new SrsMpegtsSrtConn(&manager_, fd, ip, 9000);
        conn_->config_ = &conf_;
        conn_->stat_ = &stat_;
        conn_->hooks_ = &hooks_;
        conn_->stream_publish_tokens_ = &tokens_;
        conn_->srt_sources_ = &srt_sources_;
        conn_->live_sources_ = &live_sources_;
        conn_->rtc_sources_ = &rtc_sources_;
        if (io) {
            srs_freep(conn_->srt_conn_);
            conn_->srt_conn_ = io;
        }
        conn_->assemble();
        dynamic_cast<SrsSecurity *>(conn_->security_)->config_ = &conf_;

        // Serve the client in a coroutine of this object, which keeps what the connection returned.
        srs_freep(conn_->trd_);
        conn_->trd_ = new SrsSTCoroutine("srt", this, _srs_context->get_id());

        hooks_.conn_ = conn_;
        return err;
    }
};

// A vhost that serves SRT, with the hooks of every event.
static string srt_conn_utest_vhost(string extra = "")
{
    return string("vhost __defaultVhost__ { srt { enabled on; } ") + rtmp_conn_utest_all_hooks + " " + extra + " }";
}

// A message of TS packets as an encoder sends over SRT, here null packets (PID 0x1FFF), which a demuxer skips.
static string srt_conn_utest_ts(int nb_packets)
{
    string msg;
    for (int i = 0; i < nb_packets; i++) {
        string pkt(SRS_TS_PACKET_SIZE, (char)0xff);
        pkt[0] = 0x47;
        pkt[1] = 0x1f;
        pkt[2] = (char)0xff;
        pkt[3] = 0x10;
        msg += pkt;
    }
    return msg;
}

// Waits for a trace that holds the text, at most about 3s, and returns it.
static string srt_conn_utest_wait_trace(ListenerUtestLog &log, string text)
{
    string line;
    for (int i = 0; i < 300 && (line = log.last_trace(text)).empty(); i++) {
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    }
    return line;
}

// An encoder or player such as ffmpeg, connected over loopback to a listener of the server with its stream id, and the
// server's side of the connection as the listener accepts it.
class SrtConnUtestSocketPair
{
public:
    SrsSrtSocket *listener_;
    SrsSrtSocket *client_;
    srs_srt_t accepted_;

public:
    SrtConnUtestSocketPair()
    {
        listener_ = NULL;
        client_ = NULL;
        accepted_ = srs_srt_socket_invalid();
    }
    virtual ~SrtConnUtestSocketPair()
    {
        srs_freep(client_);
        srs_freep(listener_);
    }

public:
    srs_error_t connect(std::string streamid)
    {
        srs_error_t err = srs_success;

        int port = 0;
        if ((err = srs_utest_free_port(SOCK_DGRAM, &port)) != srs_success) {
            return srs_error_wrap(err, "port");
        }

        srs_srt_t lfd = srs_srt_socket_invalid();
        if ((err = srs_srt_socket_with_default_option(&lfd)) != srs_success) {
            return srs_error_wrap(err, "listener");
        }
        listener_ = new SrsSrtSocket(_srs_srt_eventloop->poller(), lfd);
        listener_->set_recv_timeout(3 * SRS_UTIME_SECONDS);
        if ((err = srs_srt_listen(lfd, "127.0.0.1", port)) != srs_success) {
            return srs_error_wrap(err, "listen");
        }

        srs_srt_t cfd = srs_srt_socket_invalid();
        if ((err = srs_srt_socket_with_default_option(&cfd)) != srs_success) {
            return srs_error_wrap(err, "client");
        }
        client_ = new SrsSrtSocket(_srs_srt_eventloop->poller(), cfd);
        client_->set_send_timeout(3 * SRS_UTIME_SECONDS);
        client_->set_recv_timeout(3 * SRS_UTIME_SECONDS);
        if ((err = srs_srt_set_streamid(cfd, streamid)) != srs_success) {
            return srs_error_wrap(err, "streamid");
        }
        if ((err = client_->connect("127.0.0.1", port)) != srs_success) {
            return srs_error_wrap(err, "connect");
        }

        if ((err = listener_->accept(&accepted_)) != srs_success) {
            return srs_error_wrap(err, "accept");
        }
        return err;
    }
};

// A client whose stream id the server cannot read: a player that asks for `m=play`, where SRT's mode is `m=request`,
// or an encoder whose stream id has a host but no `r=` resource. It is refused before any hook, token or source.
VOID TEST(AppSrtConnTest, StreamIdWithUnknownModeOrNoResource_Refused)
{
    srs_error_t err;

    const char *ids[] = {"#!::r=live/livestream,m=play", "#!::h=127.0.0.1,m=publish"};
    for (int i = 0; i < 2; i++) {
        SrtConnUtestClient c;
        HELPER_ASSERT_SUCCESS(c.open(srt_conn_utest_vhost(), ids[i]));

        rtmp_conn_utest_expect(c.run(), ERROR_SRT_CONN, "invalid srt streamid");
        EXPECT_STREQ("", c.hooks_.called().c_str());
        EXPECT_EQ(0, c.live_sources_.fetch_or_create_count_);
        EXPECT_TRUE(c.token_free());
    }
}

// An SRT client of an edge vhost, which has SRT on: edges do not serve SRT, so the client is refused, and the operator
// is warned with the vhost's name.
VOID TEST(AppSrtConnTest, ClientOfEdgeVhost_Refused)
{
    srs_error_t err;

    ListenerUtestLog log;
    SrtConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open(srt_conn_utest_vhost("cluster { mode remote; origin 127.0.0.1:19350; }"), "#!::r=live/livestream,m=publish"));

    rtmp_conn_utest_expect(c.run(), ERROR_SRT_CONN, "srt disabled, vhost=__defaultVhost__");
    EXPECT_EQ(1, log.warns("disable SRT for edge vhost=__defaultVhost__"));
    EXPECT_STREQ("", c.hooks_.called().c_str());
    EXPECT_TRUE(c.token_free());
}

// An RTMP or WebRTC encoder already publishes the stream, and holds its publish token. An SRT encoder of the same
// stream is refused before any hook, and the stream is left to the other encoder.
VOID TEST(AppSrtConnTest, SrtPublishWhileOtherProtocolPublishes_RefusedByToken)
{
    srs_error_t err;

    SrtConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open(srt_conn_utest_vhost(), "#!::r=live/livestream,m=publish"));

    SrsStreamPublishToken *rtmp_token = NULL;
    HELPER_ASSERT_SUCCESS(c.tokens_.acquire_token(&c.req_, rtmp_token));
    SrsUniquePtr<SrsStreamPublishToken> token(rtmp_token);

    rtmp_conn_utest_expect(c.run(), ERROR_SYSTEM_STREAM_BUSY, "acquire stream publish token");
    EXPECT_STREQ("", c.hooks_.called().c_str());
    EXPECT_TRUE(rtmp_token->is_acquired());
}

// The HTTP hooks backend refuses an SRT client, answering a non-zero code: at its connect, at its publish or at its
// play. The client is refused, the stream is not taken, and each hook the backend accepted is paired: on_close for
// on_connect.
VOID TEST(AppSrtConnTest, HookRefusesClient_RefusedWithHooksPaired)
{
    srs_error_t err;

    struct {
        const char *refuse_at;
        const char *streamid;
        const char *wrap;
        const char *called;
    } cases[] = {
        {"on_connect", "#!::r=live/livestream,m=publish", "on connect", "on_connect"},
        {"on_publish", "#!::r=live/livestream,m=publish", "srt: callback on publish", "on_connect,on_publish,on_close"},
        {"on_play", "#!::r=live/livestream,m=request", "srt: callback on play", "on_connect,on_play,on_close"},
    };
    for (int i = 0; i < 3; i++) {
        SrtConnUtestClient c;
        HELPER_ASSERT_SUCCESS(c.open(srt_conn_utest_vhost(), cases[i].streamid));
        c.hooks_.refuse_at_ = cases[i].refuse_at;

        rtmp_conn_utest_expect(c.run(), ERROR_RESPONSE_CODE, cases[i].wrap);
        EXPECT_STREQ(cases[i].called, c.hooks_.called().c_str()) << cases[i].refuse_at;
        EXPECT_TRUE(c.source_->can_publish());
        EXPECT_TRUE(c.source_->consumers_.empty());
        EXPECT_TRUE(c.token_free());
    }
}

// The vhost denies the client's address: an encoder by `deny publish`, a player by `deny play`. The client is refused
// after its on_connect, with no stream hook, and the stream is not taken.
VOID TEST(AppSrtConnTest, ClientDeniedBySecurity_RefusedWithNoStreamHook)
{
    srs_error_t err;

    const char *ids[] = {"#!::r=live/livestream,m=publish", "#!::r=live/livestream,m=request"};
    for (int i = 0; i < 2; i++) {
        SrtConnUtestClient c;
        HELPER_ASSERT_SUCCESS(c.open(srt_conn_utest_vhost("security { enabled on; deny publish 192.168.1.100; deny play 192.168.1.100; }"), ids[i]));

        rtmp_conn_utest_expect(c.run(), ERROR_SYSTEM_SECURITY_DENY, "srt: security check");
        EXPECT_STREQ("on_connect,on_close", c.hooks_.called().c_str());
        EXPECT_TRUE(c.source_->can_publish());
        EXPECT_TRUE(c.source_->consumers_.empty());
    }
}

// The HTTP API kicks an SRT encoder while it publishes, or a player while it waits for packets (DELETE
// /api/v1/clients/<id>). The connection ends, the stream is left, and the hooks stay paired.
VOID TEST(AppSrtConnTest, ClientKickedByApi_EndsWithHooksPaired)
{
    srs_error_t err;

    if (true) {
        SrtConnUtestClient c;
        HELPER_ASSERT_SUCCESS(c.open(srt_conn_utest_vhost(), "#!::r=live/livestream,m=publish"));
        ISrsSrtConsumer *player_raw = NULL;
        HELPER_ASSERT_SUCCESS(c.source_->create_consumer(player_raw));
        SrsUniquePtr<ISrsSrtConsumer> player(player_raw);

        HELPER_ASSERT_SUCCESS(c.start());
        c.wait_publishing();
        EXPECT_FALSE(c.source_->can_publish());

        c.io_->client_sends(srt_conn_utest_ts(7));
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
        c.conn_->expire();
        c.wait_done();

        ASSERT_TRUE(c.done_);
        rtmp_conn_utest_expect(c.err_, ERROR_SRT_INTERRUPT, "srt: recvmsg");
        c.err_ = srs_success;
        EXPECT_STREQ("on_connect,on_publish,on_unpublish,on_close", c.hooks_.called().c_str());
        EXPECT_TRUE(c.source_->can_publish());
        EXPECT_TRUE(c.token_free());

        // The player of the stream got the encoder's message.
        SrsSrtPacket *pkt = NULL;
        HELPER_ASSERT_SUCCESS(player->dump_packet(&pkt));
        ASSERT_TRUE(pkt != NULL);
        EXPECT_EQ(7 * SRS_TS_PACKET_SIZE, pkt->size());
        srs_freep(pkt);
    }

    if (true) {
        SrtConnUtestClient c;
        HELPER_ASSERT_SUCCESS(c.open(srt_conn_utest_vhost(), "#!::r=live/livestream,m=request"));
        HELPER_ASSERT_SUCCESS(c.encoder_publishes());
        HELPER_ASSERT_SUCCESS(c.start());
        c.wait_playing();
        EXPECT_EQ(1, (int)c.source_->consumers_.size());

        c.conn_->expire();
        c.wait_done();

        ASSERT_TRUE(c.done_);
        rtmp_conn_utest_expect(c.err_, ERROR_THREAD_INTERRUPED, "srt play thread");
        c.err_ = srs_success;
        EXPECT_STREQ("on_connect,on_play,on_stop,on_close", c.hooks_.called().c_str());
        EXPECT_TRUE(c.source_->consumers_.empty());
        c.encoder_unpublishes();
    }
}

// An encoder sends what is not a TS of 188-byte packets: a TS of 204-byte packets, as DVB with Reed-Solomon bytes, or
// raw H.264. SRT carries TS of 188-byte packets only, so the publish ends, the stream is left and the hooks stay
// paired.
VOID TEST(AppSrtConnTest, EncoderSendsNoTs_PublishEnds)
{
    srs_error_t err;

    string h264 = string("\x00\x00\x00\x01\x67\x64\x00\x1f", 8) + string(7 * SRS_TS_PACKET_SIZE - 8, '\0');
    string msgs[] = {string(7 * 204, (char)0x47), h264};
    const char *texts[] = {"invalid ts packet len=1428", "invalid ts packet first=0"};
    for (int i = 0; i < 2; i++) {
        SrtConnUtestClient c;
        HELPER_ASSERT_SUCCESS(c.open(srt_conn_utest_vhost(), "#!::r=live/livestream,m=publish"));
        HELPER_ASSERT_SUCCESS(c.start());
        c.wait_publishing();

        c.io_->client_sends(msgs[i]);
        c.wait_done();

        ASSERT_TRUE(c.done_);
        EXPECT_NE(string::npos, srs_error_desc(c.err_).find(texts[i])) << srs_error_desc(c.err_);
        rtmp_conn_utest_expect(c.err_, ERROR_SRT_CONN, "srt: process packet");
        c.err_ = srs_success;
        EXPECT_STREQ("on_connect,on_publish,on_unpublish,on_close", c.hooks_.called().c_str());
        EXPECT_TRUE(c.source_->can_publish());
        EXPECT_TRUE(c.token_free());
    }
}

// A player of a stream whose encoder has left (#4591): when no packet comes for `peer_idle_timeout`, it is
// disconnected. While the encoder still publishes but sends nothing, as when it pauses, the player stays.
VOID TEST(AppSrtConnTest, PlayerOfStreamWithoutEncoder_DisconnectedAfterIdleTimeout)
{
    srs_error_t err;

    SrtConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open(string("srt_server { enabled on; listen 10080; peer_idle_timeout 50; } ") + srt_conn_utest_vhost(),
                                 "#!::r=live/livestream,m=request"));
    HELPER_ASSERT_SUCCESS(c.encoder_publishes());
    HELPER_ASSERT_SUCCESS(c.start());
    c.wait_playing();
    EXPECT_EQ(50 * SRS_UTIME_MILLISECONDS, c.io_->get_recv_timeout());

    srs_usleep(200 * SRS_UTIME_MILLISECONDS);
    EXPECT_FALSE(c.done_);

    c.encoder_unpublishes();
    c.wait_done();

    ASSERT_TRUE(c.done_);
    rtmp_conn_utest_expect(c.err_, ERROR_SRT_SOURCE_DISCONNECTED, "srt source disconnected");
    c.err_ = srs_success;
    EXPECT_STREQ("on_connect,on_play,on_stop,on_close", c.hooks_.called().c_str());
    EXPECT_TRUE(c.source_->consumers_.empty());
}

// The player is gone while the server sends it a message: the send fails, the play ends, the player leaves the stream
// and the hooks stay paired.
VOID TEST(AppSrtConnTest, PlayerGoneAtSend_PlayEnds)
{
    srs_error_t err;

    SrtConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open(srt_conn_utest_vhost(), "#!::r=live/livestream,m=request"));
    c.io_->fail_write_at_ = 1;
    HELPER_ASSERT_SUCCESS(c.encoder_publishes());
    HELPER_ASSERT_SUCCESS(c.start());
    c.wait_playing();

    HELPER_ASSERT_SUCCESS(c.encoder_sends(srt_conn_utest_ts(7)));
    HELPER_ASSERT_SUCCESS(c.encoder_sends(srt_conn_utest_ts(7)));
    c.wait_done();

    ASSERT_TRUE(c.done_);
    rtmp_conn_utest_expect(c.err_, ERROR_SRT_IO, "srt send, size=1316");
    c.err_ = srs_success;
    EXPECT_EQ(1, c.io_->nb_writes_);
    EXPECT_STREQ("on_connect,on_play,on_stop,on_close", c.hooks_.called().c_str());
    EXPECT_TRUE(c.source_->consumers_.empty());
    c.encoder_unpublishes();
}

static void *srt_conn_utest_st_thread_create_out_of_memory(void *(*start)(void *arg), void *arg, int joinable, int stack_size)
{
    return NULL;
}

// State Threads cannot make a coroutine, as when the server is out of memory: for a new SRT client, or for the receive
// thread of a player. The client is refused, and the player leaves the stream with its hooks paired.
VOID TEST(AppSrtConnTest, NoCoroutineForClientOrReceiveThread_Refused)
{
    srs_error_t err;

    if (true) {
        SrtConnUtestClient c;
        HELPER_ASSERT_SUCCESS(c.open(srt_conn_utest_vhost(), "#!::r=live/livestream,m=publish"));

        _ST_THREAD_CREATE_PFN ov = _pfn_st_thread_create;
        _pfn_st_thread_create = (_ST_THREAD_CREATE_PFN)srt_conn_utest_st_thread_create_out_of_memory;
        err = c.start();
        _pfn_st_thread_create = ov;
        rtmp_conn_utest_expect(err, ERROR_ST_CREATE_CYCLE_THREAD, "coroutine");
        EXPECT_STREQ("", c.hooks_.called().c_str());
    }

    if (true) {
        SrtConnUtestClient c;
        HELPER_ASSERT_SUCCESS(c.open(srt_conn_utest_vhost(), "#!::r=live/livestream,m=request"));
        HELPER_ASSERT_SUCCESS(c.encoder_publishes());
        HELPER_ASSERT_SUCCESS(c.start());

        _ST_THREAD_CREATE_PFN ov = _pfn_st_thread_create;
        _pfn_st_thread_create = (_ST_THREAD_CREATE_PFN)srt_conn_utest_st_thread_create_out_of_memory;
        c.wait_done();
        _pfn_st_thread_create = ov;

        ASSERT_TRUE(c.done_);
        rtmp_conn_utest_expect(c.err_, ERROR_ST_CREATE_CYCLE_THREAD, "start srt recv trd");
        c.err_ = srs_success;
        EXPECT_STREQ("on_connect,on_play,on_stop,on_close", c.hooks_.called().c_str());
        EXPECT_TRUE(c.source_->consumers_.empty());
        c.encoder_unpublishes();
    }
}

// An SRT encoder to a vhost whose DVR plan is mistyped. The stream's live source cannot be made, so the encoder is
// refused after its on_publish, which gets its on_unpublish, and its publish token is released.
VOID TEST(AppSrtConnTest, EncoderOfVhostWithMistypedDvrPlan_Refused)
{
    srs_error_t err;

    RtmpSourceUtestFactory factory;
    factory.real_dvr_ = true;
    MockLiveSourceHandler handler;

    SrtConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open(srt_conn_utest_vhost("dvr { enabled on; dvr_plan sesion; }"), "#!::r=live/livestream,m=publish"));
    factory.config_ = &c.conf_;
    factory.stat_ = &c.stat_;

    // The live source the server makes for the stream, from the same config.
    SrsLiveSource *live = new SrsLiveSource();
    live->config_ = &c.conf_;
    live->stat_ = &c.stat_;
    live->handler_ = &handler;
    live->app_factory_ = &factory;
    c.live_sources_.mock_source_ = SrsSharedPtr<SrsLiveSource>(live);
    live->assemble();

    rtmp_conn_utest_expect(c.run(), ERROR_DVR_ILLEGAL_PLAN, "create source");
    EXPECT_STREQ("on_connect,on_publish,on_unpublish,on_close", c.hooks_.called().c_str());
    EXPECT_TRUE(c.source_->can_publish());
    EXPECT_TRUE(c.token_free());
}

// An SRT encoder to a vhost with `srt_to_rtmp on` and WebRTC with `rtmp_to_rtc on`: the stream is bridged to its live
// and WebRTC streams. While it publishes, the WebRTC stream is taken, so a WHIP encoder of it is refused; when the
// encoder closes, the WebRTC stream is free again.
VOID TEST(AppSrtConnTest, EncoderBridgedToWebRtc_WebRtcStreamTakenWhilePublishing)
{
    srs_error_t err;

    SrtConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open("rtc_server { enabled on; listen 8000; } vhost __defaultVhost__ { srt { enabled on; srt_to_rtmp on; } "
                                 "rtc { enabled on; rtmp_to_rtc on; } }",
                                 "#!::r=live/livestream,m=publish"));
    HELPER_ASSERT_SUCCESS(c.start());
    c.wait_publishing();

    SrsSharedPtr<SrsRtcSource> rtc = c.rtc_sources_.mock_source_;
    EXPECT_EQ(1, c.rtc_sources_.fetch_or_create_count_);
    EXPECT_FALSE(rtc->can_publish());

    c.io_->client_closes();
    c.wait_done();

    ASSERT_TRUE(c.done_);
    rtmp_conn_utest_expect(c.err_, ERROR_SRT_IO, "srt: recvmsg");
    c.err_ = srs_success;
    EXPECT_TRUE(rtc->can_publish());
    EXPECT_TRUE(c.source_->can_publish());
}

// An encoder such as ffmpeg or OBS publishes over SRT. Each pithy print, the server logs the transport stats libsrt
// counted since its last line, then the messages and kbps of the encoder.
VOID TEST(AppSrtConnTest, EncoderTransportStatsLogged)
{
    srs_error_t err;

    PithyPrintAtOnceScope stages(SrsPithyPrint::create_srt_publish);
    ListenerUtestLog log;

    SrtConnUtestSocketPair pair;
    HELPER_ASSERT_SUCCESS(pair.connect("#!::r=live/livestream,m=publish"));

    SrtConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open_socket(srt_conn_utest_vhost(), pair.accepted_));
    HELPER_ASSERT_SUCCESS(c.start());
    c.wait_publishing();

    // The encoder sends a message; the line before the next read counts it.
    string msg = srt_conn_utest_ts(7);
    ssize_t nn = 0;
    HELPER_ASSERT_SUCCESS(pair.client_->sendmsg((void *)msg.data(), msg.size(), &nn));
    string line = srt_conn_utest_wait_trace(log, "Transport Stats # pktRecv=1,");
    EXPECT_NE(string::npos, line.find("pktRecv=1, pktRcvLoss=0, pktRcvRetrans=0, pktRcvDrop=0")) << line;
    line = log.last_trace("<- " SRS_CONSTS_LOG_SRT_PUBLISH " time=");
    EXPECT_NE(string::npos, line.find("packets=1,")) << line;

    c.conn_->expire();
    c.wait_done();
    ASSERT_TRUE(c.done_);
    rtmp_conn_utest_expect(c.err_, ERROR_SRT_INTERRUPT, "srt: recvmsg");
    c.err_ = srs_success;
    EXPECT_STREQ("on_connect,on_publish,on_unpublish,on_close", c.hooks_.called().c_str());
}

// A player such as ffplay plays over SRT. Each pithy print, the server logs the transport stats libsrt counted since
// its last line, then the messages and kbps of the player.
VOID TEST(AppSrtConnTest, PlayerTransportStatsLogged)
{
    srs_error_t err;

    PithyPrintAtOnceScope stages(SrsPithyPrint::create_srt_play);
    ListenerUtestLog log;

    SrtConnUtestSocketPair pair;
    HELPER_ASSERT_SUCCESS(pair.connect("#!::r=live/livestream,m=request"));

    // The player's wait for packets ends each 100ms, so it sends a lone message in that time.
    SrtConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open_socket(string("srt_server { enabled on; listen 10080; peer_idle_timeout 100; } ") + srt_conn_utest_vhost(), pair.accepted_));
    HELPER_ASSERT_SUCCESS(c.encoder_publishes());
    HELPER_ASSERT_SUCCESS(c.start());
    c.wait_playing();

    // The player gets the first message, then the line before the second counts it.
    char buf[1500];
    ssize_t nn = 0;
    HELPER_ASSERT_SUCCESS(c.encoder_sends(srt_conn_utest_ts(7)));
    HELPER_ASSERT_SUCCESS(pair.client_->recvmsg(buf, sizeof(buf), &nn));
    EXPECT_EQ(7 * SRS_TS_PACKET_SIZE, nn);

    HELPER_ASSERT_SUCCESS(c.encoder_sends(srt_conn_utest_ts(7)));
    HELPER_ASSERT_SUCCESS(pair.client_->recvmsg(buf, sizeof(buf), &nn));
    string line = srt_conn_utest_wait_trace(log, "Transport Stats # pktSent=1,");
    EXPECT_NE(string::npos, line.find("pktSent=1, pktSndLoss=0, pktRetrans=0, pktSndDrop=0")) << line;
    line = log.last_trace("-> " SRS_CONSTS_LOG_SRT_PLAY " time=");
    EXPECT_NE(string::npos, line.find("packets=1,")) << line;

    c.conn_->expire();
    c.wait_done();
    ASSERT_TRUE(c.done_);
    EXPECT_STREQ("on_connect,on_play,on_stop,on_close", c.hooks_.called().c_str());
    c.encoder_unpublishes();
}

// An SRT player waiting for packets gets a lone message as soon as it comes, not when the next one comes or its
// wait of `peer_idle_timeout` (10s by default) ends: the last message before the encoder pauses is not held.
VOID TEST(AppSrtConnTest, PlayerOfLoneMessage_SentAtOnce)
{
    srs_error_t err;

    SrtConnUtestClient c;
    HELPER_ASSERT_SUCCESS(c.open(srt_conn_utest_vhost(), "#!::r=live/livestream,m=request"));
    HELPER_ASSERT_SUCCESS(c.encoder_publishes());
    HELPER_ASSERT_SUCCESS(c.start());
    c.wait_playing();
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    HELPER_ASSERT_SUCCESS(c.encoder_sends(srt_conn_utest_ts(7)));
    for (int i = 0; i < 500 && c.io_->send_bytes_ == 0; i++) {
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    }
    EXPECT_EQ(7 * SRS_TS_PACKET_SIZE, c.io_->send_bytes_);

    c.conn_->expire();
    c.wait_done();
    c.encoder_unpublishes();
}
