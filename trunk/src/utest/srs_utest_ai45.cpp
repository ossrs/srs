//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_ai45.hpp>

#include <srs_app_edge.hpp>
#include <srs_app_rtc_server.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_security.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_stream_token.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_resource.hpp>
#include <srs_protocol_sdp.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_utest_ai05.hpp>
#include <srs_utest_ai07.hpp>
#include <srs_utest_ai08.hpp>
#include <srs_utest_ai09.hpp>
#include <srs_utest_ai15.hpp>
#include <srs_utest_ai18.hpp>
#include <srs_utest_ai32.hpp>
#include <srs_utest_ai36.hpp>
#include <srs_utest_ai41.hpp>
#include <srs_utest_ai42.hpp>
#include <srs_utest_ai44.hpp>
#include <srs_utest_manual_config.hpp>
#include <srs_utest_manual_coworkers.hpp>
#include <srs_utest_manual_mock.hpp>
#include <srs_utest_manual_service.hpp>
#ifdef SRS_RTSP
#include <srs_utest_ai22.hpp>
#endif

#include <set>
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
// section 8.1), whose base64 may end in '=': the publish negotiation takes the payload, as srs_parse_h264_fmtp skips
// what it does not know, but set_h264_param_desc refuses the fmtp, and the negotiator drops the error without freeing
// it, so each such publish leaks an error (B51).
VOID TEST(AppRtcSourceTest, DISABLED_WhipH264FmtpWithSprop_ParamDescRefused)
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

// The hooks of an RTMP client, in the order the connection calls them. A test can kick the client, as the HTTP API
// does, while one of them waits for its backend.
class RtmpConnUtestHooks : public MockHttpHooksForCoworkers
{
public:
    std::vector<std::string> calls_;
    std::string kick_at_;
    SrsRtmpConn *conn_;

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
    virtual srs_error_t on_connect(std::string url, ISrsRequest *req)
    {
        call("on_connect");
        return srs_success;
    }
    virtual void on_close(std::string url, ISrsRequest *req, int64_t send_bytes, int64_t recv_bytes)
    {
        call("on_close");
    }
    virtual srs_error_t on_publish(std::string url, ISrsRequest *req, SrsRtmpConnType type)
    {
        call("on_publish");
        return srs_success;
    }
    virtual void on_unpublish(std::string url, ISrsRequest *req, SrsRtmpConnType type, const SrsStatisticStreamSnapshot &stream)
    {
        call("on_unpublish");
        MockHttpHooks::on_unpublish(url, req, type, stream);
    }
    virtual srs_error_t on_play(std::string url, ISrsRequest *req, SrsRtmpConnType type)
    {
        call("on_play");
        return srs_success;
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
// but the play loop is not waiting yet, so the wakeup is lost; the loop then waits for media with no timeout. The
// connection should end at the close, but stays, with its consumer on the stream, until the next frame.
VOID TEST(AppRtmpConnTest, DISABLED_PlayerClosesDuringSendInterval_ConnectionStays)
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
