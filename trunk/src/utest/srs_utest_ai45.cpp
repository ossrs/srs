//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_ai45.hpp>

#include <srs_app_rtc_server.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_resource.hpp>
#include <srs_protocol_sdp.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_utest_ai05.hpp>
#include <srs_utest_ai07.hpp>
#include <srs_utest_ai09.hpp>
#include <srs_utest_ai15.hpp>
#include <srs_utest_ai18.hpp>
#include <srs_utest_ai32.hpp>
#include <srs_utest_manual_config.hpp>
#include <srs_utest_manual_mock.hpp>

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
